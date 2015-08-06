#include "WriteStumble.cpp"
#include "nsPrintfCString.h"
#include "nsDumpUtils.h"
#include "nsGZFileWriter.h"
#include "nsIFileStreams.h"
#include "nsIInputStream.h"
#include "../StumblerLogging.h"

#define OLDEST_FILE_NUMBER 1
#define MAXFILENUM 4
#define MAXUPLOADFILENUM 15
#define MAXFILESIZE_KB 12.5 * 1024

int WriteStumble::sCurrentFileNumber = 1;
int WriteStumble::sUploadFileNumber = 0;

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
SetUploadFileNum()
{
  MOZ_ASSERT(!NS_IsMainThread());

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
      WriteStumble::sUploadFileNumber = i - 1;
      tmpFile->Remove(true);
      return;
    }
  }
  WriteStumble::sUploadFileNumber = MAXUPLOADFILENUM;
  STUMBLER_DBG("SetUploadFile filenum = %d (End)\n", WriteStumble::sUploadFileNumber);
}

nsresult
RemoveOldestUploadFile(int aFileCount)
{
  MOZ_ASSERT(!NS_IsMainThread());

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
  WriteStumble::sUploadFileNumber--;
  return NS_OK;
}

nsresult
WriteStumble::MoveOldestFileAsUploadFile()
{
  MOZ_ASSERT(!NS_IsMainThread());

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
WriteStumble::WriteJSON(Partition aPart, int aFileNum)
{
  MOZ_ASSERT(!NS_IsMainThread());

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

WriteStumble::Partition
WriteStumble::SetCurrentFile()
{
  MOZ_ASSERT(!NS_IsMainThread());

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

NS_IMETHODIMP
WriteStumble::Run()
{
  MOZ_ASSERT(!NS_IsMainThread());
  
  STUMBLER_DBG("In WriteStumble\n");
  Partition partition = SetCurrentFile();
  if (partition == Unknown) {
    STUMBLER_ERR("SetCurrentFile failed, skip once");
    return NS_OK;
  } else {
    WriteJSON(partition, sCurrentFileNumber);
    return NS_OK;
  }
}

