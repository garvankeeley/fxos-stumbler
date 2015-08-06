#include "UploadStumbleRunnable.h"
#include "nsNetUtil.h"
#include "nsIXMLHttpRequest.h"
#include "nsIURLFormatter.h"
#include "nsIScriptSecurityManager.h"
#include "StumblerLogging.h"

UploadStumbleRunnable::UploadStumbleRunnable(const nsACString* aUploadData)
: mUploadData(aUploadData)
{
}

NS_IMETHODIMP
UploadStumbleRunnable::Run()
{
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIWritableVariant> variant =
  do_CreateInstance("@mozilla.org/variant;1", &rv);
  NS_ENSURE_SUCCESS_VOID(rv);
  rv = variant->SetAsACString(mUploadData);
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
  // 60s timeout
  xhr->SetTimeout(60 * 1000);

  nsCOMPtr<EventTarget> target(do_QueryInterface(xhr));
  nsCOMPtr<nsIDOMEventListener> listener = new UploadEventListener(xhr, fileSize);
  rv = target->AddEventListener(NS_LITERAL_STRING("timeout"), listener, false);
  NS_ENSURE_SUCCESS_VOID(rv);
  // loadend catches abort, load, and error
  rv = target->AddEventListener(NS_LITERAL_STRING("loadend"), listener, false);
  NS_ENSURE_SUCCESS_VOID(rv);

  rv = xhr->Send(variant);
  NS_ENSURE_SUCCESS_VOID(rv);

  return NS_OK;
}

NS_IMPL_ISUPPORTS(UploadEventListener, nsIDOMEventListener)

UploadEventListener::UploadEventListener(nsCOMPtr<nsIXMLHttpRequest> aXHR, int64_t aFileSize)
: mXHR(aXHR), mFileSize(aFileSize)
{
}

NS_IMETHODIMP
UploadEventListener::HandleEvent(nsIDOMEvent* aEvent)
{
  nsString type;

  if (NS_FAILED(aEvent->GetType(type))) {
    STUMBLER_ERR("Failed to get event type");
    return NS_ERROR_FAILURE;
  }

  bool doDelete = false;
  if (type.EqualsLiteral("load")) {
    STUMBLER_DBG("Got load Event : size %lld", mFileSize);
    // I suspect that the file upload is finish when we got load event.
    // Will record the amount of upload and the time of upload
    doDelete = true;
  } else if (type.EqualsLiteral("error") && mXHR) {
    STUMBLER_ERR("Upload Error");
    int errorCode = mXHR->Status();
    if (400 == errorCode) {
      doDelete = true;
    }
  } else {
    STUMBLER_DBG("Receive %s Event", type.get());
  }

  WriteStumble::UploadEnded(doDelete);

  return NS_OK;
}
