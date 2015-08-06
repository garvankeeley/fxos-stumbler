#ifndef UPLOADSTUMBLERUNNABLE_H
#define UPLOADSTUMBLERUNNABLE_H

class UploadStumbleRunnable final : public nsRunnable
{
public:
  UploadStumbleRunnable() {}

  NS_IMETHOD Run();
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

#endif