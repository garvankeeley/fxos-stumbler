 /* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_system_mozstumbler_h__
#define mozilla_system_mozstumbler_h__

#include "nsICellInfo.h"
#include "nsIWifi.h"

#define STUMBLE_INTERVAL_MS 3000

class nsGeoPosition;

class StumblerInfo final : public nsICellInfoListCallback,
                           public nsIWifiScanResultsReady
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSICELLINFOLISTCALLBACK
  NS_DECL_NSIWIFISCANRESULTSREADY

  explicit StumblerInfo(nsGeoPosition* position)
    : mPosition(position), mCellInfoResponsesExpected(0), mCellInfoResponsesReceived(0), mIsWifiInfoResponseReceived(0)
  {}
  void SetWifiInfoResponseReceived();
  void SetCellInfoResponsesExpected(int count);

private:
  ~StumblerInfo() {}
  void DumpStumblerInfo();
  nsresult LocationInfoToString(nsCString& aLocDesc);
  nsresult CellNetworkInfoToString(nsCString& aCellDesc);
  nsTArray<nsRefPtr<nsICellInfo>> mCellInfo;
  nsCString mWifiDesc;
  nsRefPtr<nsGeoPosition> mPosition;
  int mCellInfoResponsesExpected;
  int mCellInfoResponsesReceived;
  bool mIsWifiInfoResponseReceived;
};

NS_IMPL_ISUPPORTS(StumblerInfo, nsICellInfoListCallback, nsIWifiScanResultsReady)


class UploadStumbleRunnable final : public nsRunnable
{
public:
  UploadStumbleRunnable()
  {}

  void TryToUploadFile();

  NS_IMETHOD Run()
  {
    MOZ_ASSERT(NS_IsMainThread());
    nsContentUtils::LogMessageToConsole("UploadStumbleRunnable\n");
    TryToUploadFile();
    return NS_OK;
  }

private:
  virtual ~UploadStumbleRunnable() {}
};


class UploadEventListener : public nsIDOMEventListener
{
public:
  explicit UploadEventListener(int64_t aFileSize)
  : mFileSize(aFileSize)
  {}

/*interfaces for addref and release and queryinterface*/
  NS_DECL_ISUPPORTS
  NS_DECL_NSIDOMEVENTLISTENER

 protected:
  virtual ~UploadEventListener() {}
  int64_t mFileSize;
};

#endif // mozilla_system_mozstumbler_h__

