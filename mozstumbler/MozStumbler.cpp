/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MozStumbler.h"
#include "nsGeoPosition.h"
#include "nsPrintfCString.h"
#include "StumblerLogging.h"
#include "WriteStumbleOnThread.h"
#include "nsNetCID.h"

using namespace mozilla;
using namespace mozilla::dom;


NS_IMPL_ISUPPORTS(StumblerInfo, nsICellInfoListCallback, nsIWifiScanResultsReady)

void
StumblerInfo::SetWifiInfoResponseReceived()
{
  mIsWifiInfoResponseReceived = 1;

  if (mIsWifiInfoResponseReceived && (mCellInfoResponsesReceived == mCellInfoResponsesExpected)) {
    STUMBLER_DBG("Call DumpStumblerInfo from SetWifiInfoResponseReceived\n");
    DumpStumblerInfo();
  }
}

void
StumblerInfo::SetCellInfoResponsesExpected(int count)
{
  mCellInfoResponsesExpected = count;
  STUMBLER_DBG("SetCellInfoNum (%d)\n", count);

  if (mIsWifiInfoResponseReceived && (mCellInfoResponsesReceived == mCellInfoResponsesExpected)) {
    STUMBLER_DBG("Call DumpStumblerInfo from SetCellInfoResponsesExpected\n");
    DumpStumblerInfo();
  }
}

nsresult
StumblerInfo::LocationInfoToString(nsCString& aLocDesc)
{
  nsCOMPtr<nsIDOMGeoPositionCoords> coords;
  mPosition->GetCoords(getter_AddRefs(coords));
  if (!coords) {
    return NS_ERROR_FAILURE;
  }

  NS_NAMED_LITERAL_CSTRING(lat, "latitude");
  NS_NAMED_LITERAL_CSTRING(lon, "longitude");
  NS_NAMED_LITERAL_CSTRING(acc, "accuracy");
  NS_NAMED_LITERAL_CSTRING(alt, "altitude");
  NS_NAMED_LITERAL_CSTRING(altacc, "altitudeAccuracy");
  NS_NAMED_LITERAL_CSTRING(head, "heading");
  NS_NAMED_LITERAL_CSTRING(spd, "speed");

  std::map<nsLiteralCString, double> info;
  coords->GetLatitude(&info[lat]);
  coords->GetLongitude(&info[lon]);
  coords->GetAccuracy(&info[acc]);
  coords->GetAltitude(&info[alt]);
  coords->GetAltitudeAccuracy(&info[altacc]);
  coords->GetHeading(&info[head]);
  coords->GetSpeed(&info[spd]);

  for (auto it = info.begin(); it != info.end(); ++it) {
    double val = it->second;
    if (!IsNaN(val)){
      aLocDesc += nsPrintfCString("\"%s\":%f,", it->first.get(), it->second);
    }
  }
  aLocDesc += nsPrintfCString("\"timestamp\":%lld,", PR_Now() / PR_USEC_PER_MSEC).get();
  return NS_OK;
}

NS_NAMED_LITERAL_CSTRING(keyRadioType, "radioType");
NS_NAMED_LITERAL_CSTRING(keyMcc, "mobileCountryCode");
NS_NAMED_LITERAL_CSTRING(keyMnc, "mobileNetworkCode");
NS_NAMED_LITERAL_CSTRING(keyLac, "locationAreaCode");
NS_NAMED_LITERAL_CSTRING(keyCid, "cellId");
NS_NAMED_LITERAL_CSTRING(keyPsc, "psc");
NS_NAMED_LITERAL_CSTRING(keyStrengthAsu, "asu");
NS_NAMED_LITERAL_CSTRING(keyStrengthDbm, "signalStrength");
NS_NAMED_LITERAL_CSTRING(keyRegistered, "serving");
NS_NAMED_LITERAL_CSTRING(keyTimingAdvance, "timingAdvance");

template <class T> void
ExtractCommonNonCDMACellInfoItems(nsCOMPtr<T>& cell, std::map<nsLiteralCString, int32_t>& info)
{
  int32_t mcc, mnc, cid, sig;

  cell->GetMcc(&mcc);
  cell->GetMnc(&mnc);
  cell->GetCid(&cid);
  cell->GetSignalStrength(&sig);

  info[keyMcc] = mcc;
  info[keyMnc] = mnc;
  info[keyCid] = cid;
  info[keyStrengthAsu] = sig;
}

nsresult
StumblerInfo::CellNetworkInfoToString(nsCString& aCellDesc)
{
  aCellDesc += "\"cellTowers\": [";

  for (uint32_t idx = 0; idx < mCellInfo.Length() ; idx++) {
    const char* radioType = 0;
    int32_t type;
    mCellInfo[idx]->GetType(&type);
    bool registered;
    mCellInfo[idx]->GetRegistered(&registered);
    if (idx) {
      aCellDesc += ",{";
    } else {
      aCellDesc += "{";
    }

    STUMBLER_DBG("type=%d\n", type);

    std::map<nsLiteralCString, int32_t> info;
    info[keyRegistered] = registered;

    if(type == nsICellInfo::CELL_INFO_TYPE_GSM) {
      radioType = "gsm";
      nsCOMPtr<nsIGsmCellInfo> gsmCellInfo = do_QueryInterface(mCellInfo[idx]);
      ExtractCommonNonCDMACellInfoItems(gsmCellInfo, info);
      int32_t lac;
      gsmCellInfo->GetLac(&lac);
      info[keyLac] = lac;
    } else if (type == nsICellInfo::CELL_INFO_TYPE_WCDMA) {
      radioType = "wcdma";
      nsCOMPtr<nsIWcdmaCellInfo> wcdmaCellInfo = do_QueryInterface(mCellInfo[idx]);
      ExtractCommonNonCDMACellInfoItems(wcdmaCellInfo, info);
      int32_t lac, psc;
      wcdmaCellInfo->GetLac(&lac);
      wcdmaCellInfo->GetPsc(&psc);
      info[keyLac] = lac;
      info[keyPsc] = psc;
    } else if (type == nsICellInfo::CELL_INFO_TYPE_CDMA) {
      radioType = "cdma";
      nsCOMPtr<nsICdmaCellInfo> cdmaCellInfo = do_QueryInterface(mCellInfo[idx]);
      int32_t mnc, lac, cid, sig;
      cdmaCellInfo->GetSystemId(&mnc);
      cdmaCellInfo->GetNetworkId(&lac);
      cdmaCellInfo->GetBaseStationId(&cid);
      info[keyMnc] = mnc;
      info[keyLac] = lac;
      info[keyCid] = cid;

      cdmaCellInfo->GetEvdoDbm(&sig);
      if (sig < 0 || sig == nsICellInfo::UNKNOWN_VALUE) {
        cdmaCellInfo->GetCdmaDbm(&sig);
      }
      if (sig > -1 && sig != nsICellInfo::UNKNOWN_VALUE)  {
        sig *= -1;
        info[keyStrengthDbm] = sig;
      }
    } else if (type == nsICellInfo::CELL_INFO_TYPE_LTE) {
      radioType = "lte";
      nsCOMPtr<nsILteCellInfo> lteCellInfo = do_QueryInterface(mCellInfo[idx]);
      ExtractCommonNonCDMACellInfoItems(lteCellInfo, info);
      int32_t lac, timingAdvance, pcid, rsrp;
      lteCellInfo->GetTac(&lac);
      lteCellInfo->GetTimingAdvance(&timingAdvance);
      lteCellInfo->GetPcid(&pcid);
      lteCellInfo->GetRsrp(&rsrp);
      info[keyLac] = lac;
      info[keyTimingAdvance] = timingAdvance;
      info[keyPsc] = pcid;
      if (rsrp != nsICellInfo::UNKNOWN_VALUE) {
        info[keyStrengthDbm] = rsrp * -1;
      }
    }

    aCellDesc += nsPrintfCString("\"%s\":\"%s\"", keyRadioType.get(), radioType);
    for (auto iter = info.begin(); iter != info.end(); ++iter) {
      int32_t value = iter->second;
      if (value != nsICellInfo::UNKNOWN_VALUE) {
        aCellDesc += nsPrintfCString(",\"%s\":%d", iter->first.get(), value);
      }
    }
    aCellDesc += "}";
  }
  aCellDesc += "]";
  return NS_OK;
}

void
StumblerInfo::DumpStumblerInfo()
{
  nsAutoCString desc;
  nsresult rv = LocationInfoToString(desc);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    STUMBLER_ERR("LocationInfoToString failed, skip this dump");
    return;
  }

  CellNetworkInfoToString(desc);
  desc += mWifiDesc;

  STUMBLER_DBG("dispatch write event to thread\n");
  nsCOMPtr<nsIEventTarget> target = do_GetService(NS_STREAMTRANSPORTSERVICE_CONTRACTID);
  MOZ_ASSERT(target);

  nsCOMPtr<nsIRunnable> event = new WriteStumbleOnThread(desc);
  target->Dispatch(event, NS_DISPATCH_NORMAL);
  return;
}

/* void notifyGetCellInfoList (in uint32_t count, [array, size_is (count)] in nsICellInfo result); */
NS_IMETHODIMP
StumblerInfo::NotifyGetCellInfoList(uint32_t count, nsICellInfo** aCellInfos)
{
  MOZ_ASSERT(NS_IsMainThread());
  STUMBLER_DBG("There are %d cellinfo in the result\n",count);

  for (uint32_t i = 0; i < count; i++) {
    mCellInfo.AppendElement(aCellInfos[i]);
  }
  mCellInfoResponsesReceived++;
  STUMBLER_DBG("NotifyGetCellInfoList mCellInfoResponsesReceived=%d,mCellInfoResponsesExpected=%d, mIsWifiInfoResponseReceived=%d\n",
                mCellInfoResponsesReceived, mCellInfoResponsesExpected, mIsWifiInfoResponseReceived);
  if (mIsWifiInfoResponseReceived && (mCellInfoResponsesReceived == mCellInfoResponsesExpected)) {
    STUMBLER_DBG("Call DumpStumblerInfo from NotifyGetCellInfoList\n");
    DumpStumblerInfo();
  }
  return  NS_OK;
}

/* void notifyGetCellInfoListFailed (in DOMString error); */
NS_IMETHODIMP StumblerInfo::NotifyGetCellInfoListFailed(const nsAString& error)
{
  MOZ_ASSERT(NS_IsMainThread());
  mCellInfoResponsesReceived++;
  STUMBLER_ERR("NotifyGetCellInfoListFailedm CellInfoReadyNum=%d, mCellInfoResponsesExpected=%d, mIsWifiInfoResponseReceived=%d",
                mCellInfoResponsesReceived, mCellInfoResponsesExpected, mIsWifiInfoResponseReceived);
  if (mIsWifiInfoResponseReceived && (mCellInfoResponsesReceived == mCellInfoResponsesExpected)) {
    STUMBLER_DBG("Call DumpStumblerInfo from NotifyGetCellInfoListFailed\n");
    DumpStumblerInfo();
  }
  return  NS_OK;
}

NS_IMETHODIMP
StumblerInfo::Onready(uint32_t count, nsIWifiScanResult** results)
{
  MOZ_ASSERT(NS_IsMainThread());
  STUMBLER_DBG("There are %d wifiAPinfo in the result\n",count);

  mWifiDesc += ",\"wifiAccessPoints\": [";
  bool firstItem = true;
  for (uint32_t i = 0 ; i < count ; i++) {
    nsString ssid;
    results[i]->GetSsid(ssid);
    if (ssid.IsEmpty()) {
      STUMBLER_DBG("no ssid, skip this AP\n");
      continue;
    }

    if (ssid.Length() >= 6) {
      if (ssid.Find("_nomap", false, ssid.Length()-6, 6) != -1) {
        STUMBLER_DBG("end with _nomap. skip this AP(ssid :%s)\n", ssid.get());
        continue;
      }
    }

    if (firstItem) {
      mWifiDesc += "{";
      firstItem = false;
    } else {
      mWifiDesc += ",{";
    }

    // mac address
    nsString bssid;
    results[i]->GetBssid(bssid);
    //   00:00:00:00:00:00 --> 000000000000
    bssid.StripChars(":");
    mWifiDesc += "\"macAddress\":\"";
    mWifiDesc += NS_ConvertUTF16toUTF8(bssid);

    uint32_t signal;
    results[i]->GetSignalStrength(&signal);
    mWifiDesc += "\",\"signalStrength\":";
    mWifiDesc += nsPrintfCString("%d", signal).get();

    mWifiDesc += "}";
  }
  mWifiDesc += "]";

  if (mCellInfoResponsesReceived == mCellInfoResponsesExpected) {
    STUMBLER_DBG("Call DumpStumblerInfo from Onready:\n");
    DumpStumblerInfo();
  } else {
    mIsWifiInfoResponseReceived = 1;
  }
  return  NS_OK;
}

NS_IMETHODIMP
StumblerInfo::Onfailure()
{
  MOZ_ASSERT(NS_IsMainThread());
  STUMBLER_ERR("GetWifiScanResults Onfailure\n");
  if (mCellInfoResponsesReceived == mCellInfoResponsesExpected) {
    STUMBLER_DBG("Call DumpStumblerInfo from Onfailure:\n");
    DumpStumblerInfo();
  } else {
    mIsWifiInfoResponseReceived = 1;
  }
  return  NS_OK;
}

