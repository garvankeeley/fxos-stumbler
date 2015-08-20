#include "UploadStumbleRunnable.h"
#include "StumblerLogging.h"
#include "mozilla/dom/Event.h"
#include "nsIScriptSecurityManager.h"
#include "nsIURLFormatter.h"
#include "nsIVariant.h"
#include "nsIXMLHttpRequest.h"
#include "nsNetUtil.h"

UploadStumbleRunnable::UploadStumbleRunnable(const nsACString& aUploadData)
: mUploadData(aUploadData)
{
}

NS_IMETHODIMP
UploadStumbleRunnable::Run()
{
  MOZ_ASSERT(NS_IsMainThread());

  nsresult rv = Upload();
  if (NS_FAILED(rv)) {
    WriteStumbleOnThread::UploadEnded(false);
  }
  return NS_OK;
}

nsresult
UploadStumbleRunnable::Upload()
{

  nsresult rv;
  nsCOMPtr<nsIWritableVariant> variant =
    do_CreateInstance("@mozilla.org/variant;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = variant->SetAsACString(mUploadData);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIXMLHttpRequest> xhr = do_CreateInstance(NS_XMLHTTPREQUEST_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  NS_NAMED_LITERAL_CSTRING(postString, "POST");

  nsCOMPtr<nsIScriptSecurityManager> secman =
    do_GetService(NS_SCRIPTSECURITYMANAGER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIPrincipal> systemPrincipal;
  rv = secman->GetSystemPrincipal(getter_AddRefs(systemPrincipal));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = xhr->Init(systemPrincipal, nullptr, nullptr, nullptr, nullptr);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIURLFormatter> formatter =
    do_CreateInstance("@mozilla.org/toolkit/URLFormatterService;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  nsString url;
  rv = formatter->FormatURLPref(NS_LITERAL_STRING("geo.stumbler.url"), url);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = xhr->Open(postString, NS_ConvertUTF16toUTF8(url), false, EmptyString(), EmptyString());
  NS_ENSURE_SUCCESS(rv, rv);

  xhr->SetRequestHeader(NS_LITERAL_CSTRING("Content-Type"), NS_LITERAL_CSTRING("application/json"));
  xhr->SetRequestHeader(NS_LITERAL_CSTRING("Content-Encoding"), NS_LITERAL_CSTRING("gzip"));
  xhr->SetMozBackgroundRequest(true);
  // 60s timeout
  xhr->SetTimeout(60 * 1000);

  nsCOMPtr<EventTarget> target(do_QueryInterface(xhr));
  nsCOMPtr<nsIDOMEventListener> listener = new UploadEventListener(xhr, mUploadData.Length());
  rv = target->AddEventListener(NS_LITERAL_STRING("timeout"), listener, false);
  NS_ENSURE_SUCCESS(rv, rv);
  // loadend catches abort, load, and error
  rv = target->AddEventListener(NS_LITERAL_STRING("load"), listener, false);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = target->AddEventListener(NS_LITERAL_STRING("loadend"), listener, false);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = xhr->Send(variant);
  NS_ENSURE_SUCCESS(rv, rv);

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
    WriteStumbleOnThread::UploadEnded(false);
    return NS_ERROR_FAILURE;
  }

  uint32_t statusCode = 0;
  bool doDelete = false;
  if (type.EqualsLiteral("load")) {
    STUMBLER_DBG("Got load Event : size %lld", mFileSize);
    // I suspect that the file upload is finish when we got load event.
    // Will record the amount of upload and the time of upload
    mXHR->GetStatus(&statusCode);
    STUMBLER_DBG("statuscode %d \n", statusCode);

    nsString responseText;
    nsresult rv = mXHR->GetResponseText(responseText);
    if (NS_SUCCEEDED(rv)) {
      STUMBLER_DBG("response %s", NS_ConvertUTF16toUTF8(responseText).get());
    }
  } else if (type.EqualsLiteral("error") && mXHR) {
    STUMBLER_ERR("Upload Error");
    mXHR->GetStatus(&statusCode);

  } else {
    STUMBLER_DBG("Receive %s Event", NS_ConvertUTF16toUTF8(type).get());
  }

  if (200 == statusCode || 400 == statusCode) {
    doDelete = true;
  }

  WriteStumbleOnThread::UploadEnded(doDelete);

  mXHR = nullptr;

  return NS_OK;
}
