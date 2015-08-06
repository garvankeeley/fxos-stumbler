#ifndef WriteStumbleOnThread_H
#define WriteStumbleOnThread_H

#include <atomic>

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
  }

  ~WriteStumbleOnThread() {}

  Partition GetWritePosition();
  UploadFileStatus GetUploadFileStatus();
  void WriteJSON(Partition aPart);

  nsCString mDesc;
  // Don't write while uploading is happening
  std::atomic_flag sIsUploading;
  // Only run one instance of this
  std::atomic_flag sIsAlreadyRunning;
};

#endif
