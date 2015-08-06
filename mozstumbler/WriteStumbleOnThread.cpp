#include "WriteStumbleOnThread.cpp"
#include "nsPrintfCString.h"
#include "nsDumpUtils.h"
#include "nsGZFileWriter.h"
#include "nsIFileStreams.h"
#include "nsIInputStream.h"
#include "StumblerLogging.h"

#define MAXFILESIZE_KB 15 * 1024

NS_NAMED_LITERAL_CSTRING(kOutputFileNameInProgress, "stumbles.json.gz");
NS_NAMED_LITERAL_CSTRING(kOutputFileNameCompleted, "stumbles.done.json.gz");
NS_NAMED_LITERAL_CSTRING(kOutputDirName, "mozstumbler");

void
WriteStumbleOnThread::UploadEnded(bool deleteUploadFile)
{
  if (!deleteUploadFile) {
    sIsUploading = false;
    return;
  }

  class DeleteRunnable : public nsRunnable
  {
  public:
    DeleteRunnable() {}

    NS_IMETHODIMP
    Run() override
    {
      nsCOMPtr<nsIFile> tmpFile;
      rv = nsDumpUtils::OpenTempFile(kOutputFileNameCompleted,
                                     getter_AddRefs(tmpFile),
                                     kOutputDirName,
                                     nsDumpUtils::CREATE);
      if (!NS_FAILED(rv)) {
        tmpFile->Remove(true);
      }
      // critically, this sets this flag to false so writing can happen again
      sIsUploading = false;
    }

  private:
    ~DeleteRunnable() {}
  };

  nsCOMPtr<nsIEventTarget> target = do_GetService(NS_STREAMTRANSPORTSERVICE_CONTRACTID);
  MOZ_ASSERT(target);
  nsCOMPtr<nsIRunnable> event = new DeleteRunnable();
  target->Dispatch(event, NS_DISPATCH_NORMAL);
}

void
WriteStumbleOnThread::WriteJSON(Partition aPart)
{
  MOZ_ASSERT(!NS_IsMainThread());

  nsCOMPtr<nsIFile> tmpFile;
  nsresult rv;
  rv = nsDumpUtils::OpenTempFile(kOutputFileNameInProgress, getter_AddRefs(tmpFile),
                                 kOutputDirName, nsDumpUtils::CREATE);
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
  if (aPart == Partition::End) {
    gzWriter->Write("]}");
    rv = gzWriter->Finish();
    if (NS_WARN_IF(NS_FAILED(rv))) {
      STUMBLER_ERR("gzWriter finish failed");
    }

    tmpFile->rename(kOutputFileNameCompleted);

    return;
  }

  // Need to add "{items:[" before the first item
  if (aPart == Partition::Begining) {
    gzWriter->Write("{\"items\":[{");
  } else if (aPart == Partition::Middle) {
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
    WriteJSON(Partition::End);
    return;
  }
}

WriteStumbleOnThread::Partition
WriteStumbleOnThread::GetWritePosition()
{
  MOZ_ASSERT(!NS_IsMainThread());

  nsCOMPtr<nsIFile> tmpFile;
  rv = nsDumpUtils::OpenTempFile(kOutputFileNameInProgress, getter_AddRefs(tmpFile),
                                 kOutputDirName, nsDumpUtils::CREATE);
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
    return Partition::Begining;
  } else if (fileSize >= MAXFILESIZE_KB) {
    return Partition::End;
  } else {
    return Partition::Middle;
  }
}

NS_IMETHODIMP
WriteStumbleOnThread::Run()
{
  MOZ_ASSERT(!NS_IsMainThread());

  bool b = sIsAlreadyRunning.test_and_set();
  if (b) {
    return NS_OK;
  }

  STUMBLER_DBG("In WriteStumbleOnThread\n");

  UploadFileStatus status = GetUploadFileStatus();

  if (UploadFileStatus::NoFile != status) {
    if (UploadFileStatus::ExistsAndReadyToUpload == status) {
     Upload();
    }
  }
  else {
    Partition partition = GetWritePosition();
    if (partition == Unknown) {
      STUMBLER_ERR("GetWritePosition failed, skip once");
    } else {
      WriteJSON(partition);
    }
  }

  sIsAlreadyRunning = false;
  return NS_OK;
}


/*
 If the upload file exists, then check if it is one day old.
 • if it is a day old -> ExistsAndReadyToUpload
 • if it is less than the current day old -> Exists
 • otherwise -> NoFile
 
 The Exists case means that the upload and the stumbling is rate limited
 per-day to the size of the one file.
 */
UploadFileStatus
WriteStumbleOnThread::GetUploadFileStatus()
{
  // TODO
  // Alphen can you help fill this in?
  if (file_exists(kOutputFileNameCompleted)) {
    if (file_is_at_least_one_day_old(kOutputFileNameCompleted)) {
     return UploadFileStatus::ExistsAndReadyToUpload;
    }
    return UploadFileStatus::Exists;
  }
  return UploadFileStatus::NoFile;
}

void
WriteStumbleOnThread::Upload()
{
  MOZ_ASSERT(!NS_IsMainThread());

  bool b = sIsUploading.test_and_set();
  if (b) {
    return;
  }

  nsCOMPtr<nsIFile> tmpFile;
  nsresult rv = nsDumpUtils::OpenTempFile(kOutputFileNameCompleted, getter_AddRefs(tmpFile),
                                          kOutputDirName, nsDumpUtils::CREATE);
  int64_t fileSize;
  rv = tmpFile->GetFileSize(&fileSize);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    STUMBLER_ERR("GetFileSize failed");
    sIsUploading = false;
    return;
  }
  STUMBLER_LOG("size : %lld", fileSize);
  if (fileSize <= 0) {
    sIsUploading = false;
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

  nsCOMPtr<UploadStumbleRunnable> uploader = new UploadStumbleRunnable(bufStr);
  NS_DispatchToMainThread(uploader);
}
