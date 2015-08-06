#ifndef WRITESTUMBLE_H
#define WRITESTUMBLE_H

#include <atomic>

class WriteStumble : public nsRunnable
{
public:
  explicit WriteStumble(const nsCString& aDesc)
  : mDesc(aDesc)
  {}

  NS_IMETHODIMP Run() override;

  static void UploadEnded(bool deleteUploadFile);

private:
  ~WriteStumble() {}
//  nsresult MoveOldestFileAsUploadFile();
  Partition GetWritePosition();
  void WriteJSON(Partition aPart, int aFileNum);
  bool IsFileReadyForUpload();

  nsCString mDesc;
  // Don't write while uploading is happening
  std::atomic_flag sIsUploading;
  // Only run one instance of this
  std::atomic_flag sIsAlreadyRunning;

  enum Partition {
    Begining,
    Middle,
    End,
    Unknown
  };
};

#endif
