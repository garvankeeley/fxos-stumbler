#include "UploadStumblerRunnable.h"
#include "nsNetUtil.h"
#include "nsIXMLHttpRequest.h"
#include "nsIURLFormatter.h"
#include "nsIScriptSecurityManager.h"
#include "StumblerLogging.h"

NS_IMETHODIMP
UploadStumbleRunnable::Run()
{
  MOZ_ASSERT(NS_IsMainThread());

  SetUploadFileNum();
  if (DumpStumblerFeedingEvent::sUploadFileNumber == 0) {
    STUMBLER_DBG("No Upload File\n");
    return NS_OK;
  }

  nsCOMPtr<nsIFile> tmpFile;
  nsresult rv = nsDumpUtils::OpenTempFile(UploadFilename(OLDEST_FILE_NUMBER), getter_AddRefs(tmpFile),
                                          GetDir(), nsDumpUtils::CREATE);

  int64_t fileSize;
  rv = tmpFile->GetFileSize(&fileSize);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    STUMBLER_ERR("GetFileSize failed");
    return NS_OK;
  }
  STUMBLER_LOG("size : %lld", fileSize);
  if (fileSize <= 0) {
    return NS_OK;
  }

  // prepare json into nsIInputStream
  nsCOMPtr<nsIInputStream> inStream;
  rv = NS_NewLocalFileInputStream(getter_AddRefs(inStream), tmpFile, -1, -1,
                                  nsIFileInputStream::DEFER_OPEN);

  NS_ENSURE_TRUE_VOID(inStream);

  nsAutoCString bufStr;
  rv = NS_ReadInputStreamToString(inStream, bufStr, fileSize);
  NS_ENSURE_SUCCESS_VOID(rv);

  nsCOMPtr<nsIWritableVariant> variant =
  do_CreateInstance("@mozilla.org/variant;1", &rv);
  NS_ENSURE_SUCCESS_VOID(rv);
  rv = variant->SetAsACString(bufStr);
  NS_ENSURE_SUCCESS_VOID(rv);

  nsCOMPtr<nsIXMLHttpRequest> xhr = do_CreateInstance(NS_XMLHTTPREQUEST_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS_VOID(rv);

  NS_NAMED_LITERAL_CSTRING(postString, "POST");

  nsCOMPtr<nsIScriptSecurityManager> secman =
  do_GetService(NS_SCRIPTSECURITYMANAGER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS_VOID(rv);

  nsCOMPtr<nsIPrincipal> systemPrincipal;
  rv = secman->GetSystemPrincipal(getter_AddRefs(systemPrincipal));
  NS_ENSURE_SUCCESS_VOID(rv);

  rv = xhr->Init(systemPrincipal, nullptr, nullptr, nullptr, nullptr);
  NS_ENSURE_SUCCESS_VOID(rv);

  nsCOMPtr<nsIURLFormatter> formatter =
  do_CreateInstance("@mozilla.org/toolkit/URLFormatterService;1", &rv);
  NS_ENSURE_SUCCESS_VOID(rv);
  nsString url;
  rv = formatter->FormatURLPref(NS_LITERAL_STRING("geo.stumbler.url"), url);
  NS_ENSURE_SUCCESS_VOID(rv);

  rv = xhr->Open(postString, NS_ConvertUTF16toUTF8(url), false, EmptyString(), EmptyString());
  NS_ENSURE_SUCCESS_VOID(rv);

  xhr->SetRequestHeader(NS_LITERAL_CSTRING("Content-Type"), NS_LITERAL_CSTRING("application/json"));
  xhr->SetRequestHeader(NS_LITERAL_CSTRING("Content-Encoding"), NS_LITERAL_CSTRING("gzip"));
  xhr->SetMozBackgroundRequest(true);
  //Add timeout to 60 seconds !!!
  xhr->SetTimeout(60*1000);

  nsCOMPtr<EventTarget> target(do_QueryInterface(xhr));
  nsCOMPtr<nsIDOMEventListener> listener = new UploadEventListener(fileSize);
  rv = target->AddEventListener(NS_LITERAL_STRING("load"), listener, false);
  NS_ENSURE_SUCCESS_VOID(rv);
  rv = target->AddEventListener(NS_LITERAL_STRING("error"), listener, false);
  NS_ENSURE_SUCCESS_VOID(rv);
  rv = target->AddEventListener(NS_LITERAL_STRING("timeout"), listener, false);
  NS_ENSURE_SUCCESS_VOID(rv);
  rv = target->AddEventListener(NS_LITERAL_STRING("loadstart"), listener, false);
  NS_ENSURE_SUCCESS_VOID(rv);
  rv = target->AddEventListener(NS_LITERAL_STRING("progress"), listener, false);
  NS_ENSURE_SUCCESS_VOID(rv);
  rv = target->AddEventListener(NS_LITERAL_STRING("abort"), listener, false);
  NS_ENSURE_SUCCESS_VOID(rv);
  rv = target->AddEventListener(NS_LITERAL_STRING("loadend"), listener, false);
  NS_ENSURE_SUCCESS_VOID(rv);

  rv = xhr->Send(variant);
  NS_ENSURE_SUCCESS_VOID(rv);

  return NS_OK;
}

NS_IMPL_ISUPPORTS(UploadEventListener, nsIDOMEventListener)

NS_IMETHODIMP
UploadEventListener::HandleEvent(nsIDOMEvent* aEvent)
{
  nsString type;

  if (NS_FAILED(aEvent->GetType(type))) {
    STUMBLER_ERR("Failed to get event type");
    return NS_ERROR_FAILURE;
  }

  if (type.EqualsLiteral("load")) {
    STUMBLER_DBG("Got load Event : size %lld", mFileSize);
    // I suspect that the file upload is finish when we got load event.
    // Will record the amount of upload and the time of upload
  } else if (type.EqualsLiteral("error")) {
    STUMBLER_ERR("Upload Error");
  } else {
    STUMBLER_DBG("Receive %s Event", type.get());
  }
  return NS_OK;
}
