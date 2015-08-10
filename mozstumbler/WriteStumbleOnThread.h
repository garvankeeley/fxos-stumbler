#ifndef WriteStumbleOnThread_H
#define WriteStumbleOnThread_H

#include "mozilla/Atomics.h"

class WriteStumbleOnThread : public nsRunnable
{
public:
  explicit WriteStumbleOnThread(const nsCString& aDesc)
  : mDesc(aDesc)
  {}

  NS_IMETHODIMP Run() override;

  static void UploadEnded(bool deleteUploadFile);

private:

  enum class Partition {
    Begining,
    Middle,
    End,
    Unknown
  };

  enum class UploadFileStatus {
    NoFile, Exists, ExistsAndReadyToUpload
  };

  ~WriteStumbleOnThread() {}

  Partition GetWritePosition();
  UploadFileStatus GetUploadFileStatus();
  void WriteJSON(Partition aPart);
  void Upload();
  
  nsCString mDesc;

  // Don't write while uploading is happening
  static mozilla::Atomic<bool> sIsUploading;
  // Only run one instance of this
  static mozilla::Atomic<bool> sIsAlreadyRunning;
};

#endif
