#ifndef UPLOADSTUMBLERUNNABLE_H
#define UPLOADSTUMBLERUNNABLE_H

class UploadStumbleRunnable final : public nsRunnable
{
public:
  explicit UploadStumbleRunnable(const nsACString& aUploadData);

  NS_IMETHOD Run() override;
private:
  virtual ~UploadStumbleRunnable() {}
  const nsCString mUploadData;
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
  int64_t mFileSize;
  nsCOMPtr<nsIXMLHttpRequest> mXHR;
};

#endif