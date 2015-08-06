/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MozStumbler.h"
#include "nsGeoPosition.h"
#include "nsDumpUtils.h"
#include "nsPrintfCString.h"
#include "nsGZFileWriter.h"
#include "mozilla/Logging.h"
#include "nsNetUtil.h"
#include "nsIXMLHttpRequest.h"
#include "nsIURLFormatter.h"
#include "nsIScriptSecurityManager.h"
#include "nsIFileStreams.h"
#include "nsIInputStream.h"

using mozilla::LogLevel;
static PRLogModuleInfo* GetLog()
{
  static PRLogModuleInfo* log = PR_NewLogModule("mozstumbler");
  return log;
}
#define STUMBLER_DBG(arg, ...)  MOZ_LOG(GetLog(), mozilla::LogLevel::Debug, ("STUMBLER - %s: " arg, __func__, ##__VA_ARGS__))
#define STUMBLER_LOG(arg, ...)  MOZ_LOG(GetLog(), mozilla::LogLevel::Info, ("STUMBLER - %s: " arg, __func__, ##__VA_ARGS__))
#define STUMBLER_ERR(arg, ...)  MOZ_LOG(GetLog(), mozilla::LogLevel::Error, ("STUMBLER -%s: " arg, __func__, ##__VA_ARGS__))

using namespace mozilla;
using namespace mozilla::dom;

#define OLDEST_FILE_NUMBER 1
#define MAXFILENUM 4
#define MAXUPLOADFILENUM 15
#define MAXFILESIZE_KB 12.5 * 1024

int DumpStumblerFeedingEvent::sCurrentFileNumber = 1;
int DumpStumblerFeedingEvent::sUploadFileNumber = 0;

nsCString
Filename(int aFileNum)
{
  return nsPrintfCString("feeding-%d.json.gz", aFileNum);
}

nsCString
UploadFilename(int aFileNum)
{
  return nsPrintfCString("upload-%.2d.json.gz", aFileNum);
}

nsCString
GetDir()
{
  return NS_LITERAL_CSTRING("stumble");
}

void
SetUploadFileNum() {
  for (int i = 1; i <= MAXUPLOADFILENUM; i++) {
    nsresult rv;
    nsCOMPtr<nsIFile> tmpFile;
    rv = nsDumpUtils::OpenTempFile(UploadFilename(i),
                                     getter_AddRefs(tmpFile), GetDir(), nsDumpUtils::CREATE);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      STUMBLER_ERR("OpenFile failed");
      return;
    }

    int64_t fileSize = 0;
    rv = tmpFile->GetFileSize(&fileSize);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      STUMBLER_ERR("GetFileSize failed");
      return;
    }
    if (fileSize == 0) {
      DumpStumblerFeedingEvent::sUploadFileNumber = i - 1;
      tmpFile->Remove(true);
      return;
    }
  }
  DumpStumblerFeedingEvent::sUploadFileNumber = MAXUPLOADFILENUM;
  STUMBLER_DBG("SetUploadFile filenum = %d (End)\n", DumpStumblerFeedingEvent::sUploadFileNumber);
}

nsresult
RemoveOldestUploadFile(int aFileCount)
{
  // remove oldest upload file
  nsCOMPtr<nsIFile> tmpFile;
  nsresult rv = nsDumpUtils::OpenTempFile(UploadFilename(OLDEST_FILE_NUMBER), getter_AddRefs(tmpFile),
                                          GetDir(), nsDumpUtils::CREATE);
  nsAutoString targetFilename;
  rv = tmpFile->GetLeafName(targetFilename);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  rv = tmpFile->Remove(true);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // Rename the upload files. (keep the oldest file as 'upload-1.json.gz')
  for (int idx = 0; idx < aFileCount; idx++) {
    nsCOMPtr<nsIFile> file;
    nsDumpUtils::OpenTempFile(UploadFilename(idx+1), getter_AddRefs(file),
                              GetDir(), nsDumpUtils::CREATE);
    nsAutoString currentFilename;
    rv = file->GetLeafName(currentFilename);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
    rv = file->MoveTo(/* directory */ nullptr, targetFilename);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
    targetFilename = currentFilename; // keep current leafname for next iteration usage
  }
  DumpStumblerFeedingEvent::sUploadFileNumber--;
  return NS_OK;
}

nsresult
DumpStumblerFeedingEvent::MoveOldestFileAsUploadFile()
{
  nsresult rv;
  // make sure that uploadfile number is less than MAXUPLOADFILENUM
  if (sUploadFileNumber == MAXUPLOADFILENUM) {
    rv = RemoveOldestUploadFile(MAXUPLOADFILENUM);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  }
  sUploadFileNumber++;

  // Find out the target name of upload file we need
  nsCOMPtr<nsIFile> tmpFile;
  rv = nsDumpUtils::OpenTempFile(UploadFilename(sUploadFileNumber), getter_AddRefs(tmpFile),
                                          GetDir(), nsDumpUtils::CREATE);
  nsAutoString targetFilename;
  rv = tmpFile->GetLeafName(targetFilename);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // Rename the feeding file. (keep the oldest file as 'feeding-1.json.gz')
  // feeding-1.json.gz to upload-%d.json.gz
  // feeding-4.json.jz -> feeding-3.json.jz -> feeding-2.json.jz -> feeding-1.json.jz
  for (int idx = 0; idx < MAXFILENUM; idx++) {
    nsCOMPtr<nsIFile> file;
    nsDumpUtils::OpenTempFile(Filename(idx+1), getter_AddRefs(file),
                              GetDir(), nsDumpUtils::CREATE);
    nsAutoString currentFilename;
    rv = file->GetLeafName(currentFilename);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
    rv = file->MoveTo(/* directory */ nullptr, targetFilename);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
    targetFilename = currentFilename; // keep current leafname for next iteration usage
  }
  return NS_OK;
}

void
DumpStumblerFeedingEvent::WriteJSON(Partition aPart, int aFileNum) {
  nsCOMPtr<nsIFile> tmpFile;
  nsresult rv;
  rv = nsDumpUtils::OpenTempFile(Filename(aFileNum), getter_AddRefs(tmpFile),
                                   GetDir(), nsDumpUtils::CREATE);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    STUMBLER_ERR("Open a file for stumble failed");
    return;
  }

  nsRefPtr<nsGZFileWriter> gzWriter = new nsGZFileWriter(nsGZFileWriter::Append);
  rv = gzWriter->Init(tmpFile);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    STUMBLER_ERR("gzWriter init failed");
    return;
  }

  /*
      The json format is like below.
       {items:[
          {item},
          {item},
          {item}
       ]}
   */

  // Need to add "]}" after the last item
  if (aPart == End) {
    gzWriter->Write("]}");
    rv = gzWriter->Finish();
    if (NS_WARN_IF(NS_FAILED(rv))) {
      STUMBLER_ERR("gzWriter finish failed");
    }
    sCurrentFileNumber++;
    return;
  }

  // Need to add "{items:[" before the first item
  if (aPart == Begining) {
    gzWriter->Write("{\"items\":[{");
  } else if (aPart == Middle) {
    gzWriter->Write(",{");
  }
  gzWriter->Write(mDesc.get());
  //  one item is end with '}' (e.g. {item})
  gzWriter->Write("}");
  rv = gzWriter->Finish();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    STUMBLER_ERR("gzWriter finish failed");
  }

  // check if it is the end of this file
  int64_t fileSize = 0;
  rv = tmpFile->GetFileSize(&fileSize);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    STUMBLER_ERR("GetFileSize failed");
    return;
  }
  if (fileSize >= MAXFILESIZE_KB) {
    WriteJSON(End, sCurrentFileNumber);
    return;
  }
}

DumpStumblerFeedingEvent::Partition
DumpStumblerFeedingEvent::SetCurrentFile() {
  nsresult rv;
  if (sCurrentFileNumber > MAXFILENUM) {
      rv = MoveOldestFileAsUploadFile();
      if (NS_WARN_IF(NS_FAILED(rv))) {
        STUMBLER_ERR("Remove oldest file failed");
        return Unknown;
      }
      sCurrentFileNumber = MAXFILENUM;
  }

  nsCOMPtr<nsIFile> tmpFile;
  rv = nsDumpUtils::OpenTempFile(Filename(sCurrentFileNumber), getter_AddRefs(tmpFile),
                                   GetDir(), nsDumpUtils::CREATE);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    STUMBLER_ERR("Open a file for stumble failed");
    return Unknown;
  }

  int64_t fileSize = 0;
  rv = tmpFile->GetFileSize(&fileSize);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    STUMBLER_ERR("GetFileSize failed");
    return Unknown;
  }

  if (fileSize == 0) {
    return Begining;
  } else if (fileSize >= MAXFILESIZE_KB) {
    sCurrentFileNumber++;
    return SetCurrentFile();
  } else {
    return Middle;
  }
}

void
DumpStumblerFeedingEvent::Run() {
  STUMBLER_DBG("In DumpStumblerFeedingEvent\n");
  Partition partition = SetCurrentFile();
  if (partition == Unknown) {
    STUMBLER_ERR("SetCurrentFile failed, skip once");
    return;
  } else {
    WriteJSON(partition, sCurrentFileNumber);
    return;
  }
}

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

  STUMBLER_DBG("dispatch dump event to IO thread\n");
  XRE_GetIOMessageLoop()->PostTask(FROM_HERE, new DumpStumblerFeedingEvent(desc));
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

void
UploadStumbleRunnable::TryToUploadFile()
{
  MOZ_ASSERT(NS_IsMainThread());

  SetUploadFileNum();
  if (DumpStumblerFeedingEvent::sUploadFileNumber==0) {
    STUMBLER_DBG("No Upload File\n");
    return;
  }

  nsCOMPtr<nsIFile> tmpFile;
  nsresult rv = nsDumpUtils::OpenTempFile(UploadFilename(OLDEST_FILE_NUMBER), getter_AddRefs(tmpFile),
                                          GetDir(), nsDumpUtils::CREATE);

  int64_t fileSize;
  rv = tmpFile->GetFileSize(&fileSize);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    STUMBLER_ERR("GetFileSize failed");
    return;
  }
  STUMBLER_LOG("size : %lld", fileSize);
  if (fileSize <= 0) {
    return;
  }

  // prepare json into nsIInputStream
  nsCOMPtr<nsIInputStream> inStream;
  rv = NS_NewLocalFileInputStream(getter_AddRefs(inStream), tmpFile, -1, -1,
                                  nsIFileInputStream::DEFER_OPEN);

  NS_ENSURE_TRUE_VOID(inStream);

  nsAutoCString bufStr;
  rv = NS_ReadInputStreamToString(inStream, bufStr, fileSize);
  NS_ENSURE_SUCCESS_VOID(rv);

  nsCOMPtr<nsIWritableVariant> variant =
    do_CreateInstance("@mozilla.org/variant;1", &rv);
  NS_ENSURE_SUCCESS_VOID(rv);
  rv = variant->SetAsACString(bufStr);
  NS_ENSURE_SUCCESS_VOID(rv);

  nsCOMPtr<nsIXMLHttpRequest> xhr = do_CreateInstance(NS_XMLHTTPREQUEST_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS_VOID(rv);

  NS_NAMED_LITERAL_CSTRING(postString, "POST");

  nsCOMPtr<nsIScriptSecurityManager> secman =
    do_GetService(NS_SCRIPTSECURITYMANAGER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS_VOID(rv);

  nsCOMPtr<nsIPrincipal> systemPrincipal;
  rv = secman->GetSystemPrincipal(getter_AddRefs(systemPrincipal));
  NS_ENSURE_SUCCESS_VOID(rv);

  rv = xhr->Init(systemPrincipal, nullptr, nullptr, nullptr, nullptr);
  NS_ENSURE_SUCCESS_VOID(rv);

  nsCOMPtr<nsIURLFormatter> formatter =
    do_CreateInstance("@mozilla.org/toolkit/URLFormatterService;1", &rv);
  NS_ENSURE_SUCCESS_VOID(rv);
  nsString url;
  rv = formatter->FormatURLPref(NS_LITERAL_STRING("geo.stumbler.url"), url);
  NS_ENSURE_SUCCESS_VOID(rv);

  rv = xhr->Open(postString, NS_ConvertUTF16toUTF8(url), false, EmptyString(), EmptyString());
  NS_ENSURE_SUCCESS_VOID(rv);

  xhr->SetRequestHeader(NS_LITERAL_CSTRING("Content-Type"), NS_LITERAL_CSTRING("application/json"));
  xhr->SetRequestHeader(NS_LITERAL_CSTRING("Content-Encoding"), NS_LITERAL_CSTRING("gzip"));
  xhr->SetMozBackgroundRequest(true);
  //Add timeout to 60 seconds !!!
  xhr->SetTimeout(60*1000);

  nsCOMPtr<EventTarget> target(do_QueryInterface(xhr));
  nsCOMPtr<nsIDOMEventListener> listener = new UploadEventListener(fileSize);
  rv = target->AddEventListener(NS_LITERAL_STRING("load"), listener, false);
  NS_ENSURE_SUCCESS_VOID(rv);
  rv = target->AddEventListener(NS_LITERAL_STRING("error"), listener, false);
  NS_ENSURE_SUCCESS_VOID(rv);
  rv = target->AddEventListener(NS_LITERAL_STRING("timeout"), listener, false);
  NS_ENSURE_SUCCESS_VOID(rv);
  rv = target->AddEventListener(NS_LITERAL_STRING("loadstart"), listener, false);
  NS_ENSURE_SUCCESS_VOID(rv);
  rv = target->AddEventListener(NS_LITERAL_STRING("progress"), listener, false);
  NS_ENSURE_SUCCESS_VOID(rv);
  rv = target->AddEventListener(NS_LITERAL_STRING("abort"), listener, false);
  NS_ENSURE_SUCCESS_VOID(rv);
  rv = target->AddEventListener(NS_LITERAL_STRING("loadend"), listener, false);
  NS_ENSURE_SUCCESS_VOID(rv);

  rv = xhr->Send(variant);
  NS_ENSURE_SUCCESS_VOID(rv);
}

NS_IMPL_ISUPPORTS(UploadEventListener, nsIDOMEventListener)

NS_IMETHODIMP
UploadEventListener::HandleEvent(nsIDOMEvent* aEvent)
{
  nsString type;

  if (NS_FAILED(aEvent->GetType(type))) {
    STUMBLER_ERR("Failed to get event type");
    return NS_ERROR_FAILURE;
  }

  if (type.EqualsLiteral("load")) {
    STUMBLER_DBG("Got load Event : size %lld", mFileSize);
    // I suspect that the file upload is finish when we got load event.
    // Will record the amount of upload and the time of upload
  } else if (type.EqualsLiteral("error")) {
    STUMBLER_ERR("Upload Error");
  } else {
    STUMBLER_DBG("Receive %s Event", type.get());
  }
  return NS_OK;
}

