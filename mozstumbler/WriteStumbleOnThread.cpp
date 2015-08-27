#include "WriteStumbleOnThread.h"
#include "StumblerLogging.h"
#include "UploadStumbleRunnable.h"
#include "nsDumpUtils.h"
#include "nsGZFileWriter.h"
#include "nsIFileStreams.h"
#include "nsIInputStream.h"
#include "nsPrintfCString.h"

#define MAXFILESIZE_KB (15 * 1024)
#define ONEDAY_IN_MSEC (24 * 60 * 60 * 1000)
#define MAX_UPLOAD_ATTEMPTS 20

mozilla::Atomic<bool> WriteStumbleOnThread::sIsUploading(false);
mozilla::Atomic<bool> WriteStumbleOnThread::sIsAlreadyRunning(false);
WriteStumbleOnThread::UploadFreqGuard WriteStumbleOnThread::sUploadFreqGuard = {0};

#define FILENAME_INPROGRESS NS_LITERAL_CSTRING("stumbles.json")
#define FILENAME_COMPLETED NS_LITERAL_CSTRING("stumbles.done.json")
#define OUTPUT_DIR NS_LITERAL_CSTRING("mozstumbler")

class DeleteRunnable : public nsRunnable
{
  public:
    DeleteRunnable() {}

    NS_IMETHODIMP
    Run() override
    {
      nsCOMPtr<nsIFile> tmpFile;
      nsresult rv = nsDumpUtils::OpenTempFile(FILENAME_COMPLETED,
                                              getter_AddRefs(tmpFile),
                                              OUTPUT_DIR,
                                              nsDumpUtils::CREATE);
      if (NS_SUCCEEDED(rv)) {
        tmpFile->Remove(true);
      }
      // critically, this sets this flag to false so writing can happen again
      WriteStumbleOnThread::sIsUploading = false;
      return NS_OK;
    }

  private:
    ~DeleteRunnable() {}
};

void
WriteStumbleOnThread::UploadEnded(bool deleteUploadFile)
{
  if (!deleteUploadFile) {
    sIsUploading = false;
    return;
  }

  nsCOMPtr<nsIEventTarget> target = do_GetService(NS_STREAMTRANSPORTSERVICE_CONTRACTID);
  MOZ_ASSERT(target);
  nsCOMPtr<nsIRunnable> event = new DeleteRunnable();
  target->Dispatch(event, NS_DISPATCH_NORMAL);
}

#define DUMP(o, s) \
  do { \
    const char* s2 = (s); \
    uint32_t dummy; \
    nsresult rv = (o)->Write((s2), strlen(s2), &dummy); \
    if (NS_WARN_IF(NS_FAILED(rv))) \
    STUMBLER_ERR("write err"); \
  } while (0)

void
WriteStumbleOnThread::WriteJSON(Partition aPart)
{
  MOZ_ASSERT(!NS_IsMainThread());

  nsCOMPtr<nsIFile> tmpFile;
  nsresult rv;
  rv = nsDumpUtils::OpenTempFile(FILENAME_INPROGRESS, getter_AddRefs(tmpFile),
                                 OUTPUT_DIR, nsDumpUtils::CREATE);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    STUMBLER_ERR("Open a file for stumble failed");
    return;
  }

  nsCOMPtr<nsIFileOutputStream> ostream = do_CreateInstance("@mozilla.org/network/file-output-stream;1");
  rv = ostream->Init(tmpFile, PR_WRONLY | PR_APPEND, 0666, 0);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    STUMBLER_ERR("Open a file for stumble failed");
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
    DUMP(ostream, "]}");
    rv = ostream->Close();
    if (NS_WARN_IF(NS_FAILED(rv))) {
      STUMBLER_ERR("ostream finish failed");
    }

    nsCOMPtr<nsIFile> targetFile;
    nsresult rv = nsDumpUtils::OpenTempFile(FILENAME_COMPLETED, getter_AddRefs(targetFile),
                                            OUTPUT_DIR, nsDumpUtils::CREATE);
    nsAutoString targetFilename;
    rv = targetFile->GetLeafName(targetFilename);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      STUMBLER_ERR("Get Filename failed");
      return;
    }
    rv = targetFile->Remove(true);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      STUMBLER_ERR("Remove File failed");
      return;
    }
    // Rename tmpfile
    rv = tmpFile->MoveTo(/* directory */ nullptr, targetFilename);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      STUMBLER_ERR("Rename File failed");
      return;
    }
    return;
  }

  // Need to add "{items:[" before the first item
  if (aPart == Partition::Begining) {
    DUMP(ostream, "{\"items\":[{");
  } else if (aPart == Partition::Middle) {
    DUMP(ostream, ",{");
  }
  DUMP(ostream, mDesc.get());
  //  one item is ended with '}' (e.g. {item})
  DUMP(ostream, "}");
  rv = ostream->Close();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    STUMBLER_ERR("ostream finish failed");
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
  nsresult rv = nsDumpUtils::OpenTempFile(FILENAME_INPROGRESS, getter_AddRefs(tmpFile),
                                          OUTPUT_DIR, nsDumpUtils::CREATE);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    STUMBLER_ERR("Open a file for stumble failed");
    return Partition::Unknown;
  }

  int64_t fileSize = 0;
  rv = tmpFile->GetFileSize(&fileSize);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    STUMBLER_ERR("GetFileSize failed");
    return Partition::Unknown;
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

  bool b = sIsAlreadyRunning.exchange(true);
  if (b) {
    return NS_OK;
  }

  UploadFileStatus status = GetUploadFileStatus();

  if (UploadFileStatus::NoFile != status) {
    if (UploadFileStatus::ExistsAndReadyToUpload == status) {
      Upload();
    }
  } else {
    Partition partition = GetWritePosition();
    if (partition == Partition::Unknown) {
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
WriteStumbleOnThread::UploadFileStatus
WriteStumbleOnThread::GetUploadFileStatus()
{
  nsCOMPtr<nsIFile> tmpFile;
  nsresult rv = nsDumpUtils::OpenTempFile(FILENAME_COMPLETED, getter_AddRefs(tmpFile),
                                          OUTPUT_DIR, nsDumpUtils::CREATE);
  int64_t fileSize;
  rv = tmpFile->GetFileSize(&fileSize);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    STUMBLER_ERR("GetFileSize failed");
    return UploadFileStatus::NoFile;
  }
  if (fileSize <= 0) {
    tmpFile->Remove(true);
    return UploadFileStatus::NoFile;
  }

  PRTime lastModifiedTime;
  tmpFile->GetLastModifiedTime(&lastModifiedTime);
  if ((PR_Now() / PR_USEC_PER_MSEC) - lastModifiedTime >= ONEDAY_IN_MSEC) {
    return UploadFileStatus::ExistsAndReadyToUpload;
  }
  return UploadFileStatus::Exists;
}

void
WriteStumbleOnThread::Upload()
{
  MOZ_ASSERT(!NS_IsMainThread());

  bool b = sIsUploading.exchange(true);
  if (b) {
    return;
  }

  time_t seconds = time(0);
  int day = seconds / (60 * 60 * 24);

  if (sUploadFreqGuard.daySinceEpoch < day) {
    sUploadFreqGuard.daySinceEpoch = day;
    sUploadFreqGuard.attempts = 0;
  }

  sUploadFreqGuard.attempts++;
  if (sUploadFreqGuard.attempts > MAX_UPLOAD_ATTEMPTS) {
    STUMBLER_ERR("Too many upload attempts today");
    sIsUploading = false;
    return;
  }

  nsCOMPtr<nsIFile> tmpFile;
  nsresult rv = nsDumpUtils::OpenTempFile(FILENAME_COMPLETED, getter_AddRefs(tmpFile),
                                          OUTPUT_DIR, nsDumpUtils::CREATE);
  int64_t fileSize;
  rv = tmpFile->GetFileSize(&fileSize);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    STUMBLER_ERR("GetFileSize failed");
    sIsUploading = false;
    return;
  }

  if (fileSize <= 0) {
    sIsUploading = false;
    return;
  }

  // prepare json into nsIInputStream
  nsCOMPtr<nsIInputStream> inStream;
  rv = NS_NewLocalFileInputStream(getter_AddRefs(inStream), tmpFile, -1, -1,
                                  nsIFileInputStream::DEFER_OPEN);
  if (NS_FAILED(rv)) {
    sIsUploading = false;
    return;
  }

  nsCString bufStr;
  rv = NS_ReadInputStreamToString(inStream, bufStr, fileSize);

  if (NS_FAILED(rv)) {
    sIsUploading = false;
    return;
  }

  NS_ENSURE_SUCCESS_VOID(rv);

  nsCOMPtr<nsIRunnable> uploader = new UploadStumbleRunnable(bufStr);
  NS_DispatchToMainThread(uploader);
}
