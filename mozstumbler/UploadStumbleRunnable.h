#ifndef UPLOADSTUMBLERUNNABLE_H
#define UPLOADSTUMBLERUNNABLE_H

#include "nsIDOMEventListener.h"

class nsIXMLHttpRequest;

/*
 This runnable is managed by WriteStumbleOnThread only, see that class
 for how this is scheduled.
 */
class UploadStumbleRunnable final : public nsRunnable
{
public:
  explicit UploadStumbleRunnable(const nsACString& aUploadData);

  NS_IMETHOD Run() override;
private:
  virtual ~UploadStumbleRunnable() {}
  const nsCString mUploadData;
  nsresult Upload();
};


class UploadEventListener : public nsIDOMEventListener
{
public:
  UploadEventListener(nsCOMPtr<nsIXMLHttpRequest> aXHR, int64_t aFileSize);

  /*interfaces for addref and release and queryinterface*/
  NS_DECL_ISUPPORTS
  NS_DECL_NSIDOMEVENTLISTENER

protected:
  virtual ~UploadEventListener() {}
  nsCOMPtr<nsIXMLHttpRequest> mXHR;
  int64_t mFileSize;
};

#endif