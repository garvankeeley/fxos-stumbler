#include "UploadStumbleRunnable.h"
#include "StumblerLogging.h"
#include "mozilla/dom/Event.h"
#include "nsIScriptSecurityManager.h"
#include "nsIURLFormatter.h"
#include "nsIVariant.h"
#include "nsIXMLHttpRequest.h"
#include "nsNetUtil.h"
#include "nsIInputStream.h"

UploadStumbleRunnable::UploadStumbleRunnable(nsIInputStream* aUploadData)
: mUploadInputStream(aUploadData)
{
}

NS_IMETHODIMP
UploadStumbleRunnable::Run()
{
  MOZ_ASSERT(NS_IsMainThread());

  STUMBLER_DBG("In UploadStumbleRunnable •••••••••• -------- \n");

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

  nsCOMPtr<nsIWritableVariant> variant = do_CreateInstance("@mozilla.org/variant;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

 //trying to set binary data, this fails immediately on Send()
  rv = variant->SetAsISupports(mUploadInputStream);

//  rv = variant->SetAsACString(mUploadData);
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

  // ignore for now
  // rv = formatter->FormatURLPref(NS_LITERAL_STRING("geo.stumbler.url"), url);
  // NS_ENSURE_SUCCESS(rv, rv);

  xhr->SetMozBackgroundRequest(true);
  // 60s timeout
  xhr->SetTimeout(60 * 1000);

  nsCOMPtr<nsIXMLHttpRequestUpload> target2;
  rv = xhr->GetUpload(getter_AddRefs(target2));

  nsCOMPtr<nsIDOMEventListener> listener = new UploadEventListener(xhr);

  nsCOMPtr<EventTarget> target(do_QueryInterface(xhr));

  const char* const sEventStrings[] = {
    // nsIXMLHttpRequestEventTarget event types, supported by both XHR and Upload.
    "abort",
    "error",
    "load",
    "timeout"};

  for (uint32_t index = 0; index < MOZ_ARRAY_LENGTH(sEventStrings); index++) {
    nsAutoString eventType = NS_ConvertASCIItoUTF16(sEventStrings[index]);
    rv = target->AddEventListener(eventType, listener, false);
    NS_ENSURE_SUCCESS(rv, rv);
    // the following doesn't get any events
    rv = target2->AddEventListener(eventType, listener, false);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = xhr->Open(postString, NS_LITERAL_CSTRING("https://location.services.mozilla.com/v1/geosubmit"), false, EmptyString(), EmptyString());
  NS_ENSURE_SUCCESS(rv, rv);

  xhr->SetRequestHeader(NS_LITERAL_CSTRING("Content-Type"), NS_LITERAL_CSTRING("application/json"));

  // gzip not working
   xhr->SetRequestHeader(NS_LITERAL_CSTRING("Content-Encoding"), NS_LITERAL_CSTRING("gzip"));

  rv = xhr->Send(variant);
  NS_ENSURE_SUCCESS(rv, rv);
  
  return NS_OK;
}

NS_IMPL_ISUPPORTS(UploadEventListener, nsIDOMEventListener)

UploadEventListener::UploadEventListener(nsCOMPtr<nsIXMLHttpRequest> aXHR)
: mXHR(aXHR)
{
}

UploadEventListener::~UploadEventListener()
{
  STUMBLER_DBG("UploadEventListener destroyed \n");
}

NS_IMETHODIMP
UploadEventListener::HandleEvent(nsIDOMEvent* aEvent)
{
  STUMBLER_DBG("HandleEvent ++++++++++++++++++++++++++++++++++++++ \n");
  nsString type;
  if (NS_FAILED(aEvent->GetType(type))) {
    STUMBLER_ERR("Failed to get event type");
    WriteStumbleOnThread::UploadEnded(false);
    return NS_ERROR_FAILURE;
  }

  uint32_t statusCode = 0;
  bool doDelete = false;
  if (type.EqualsLiteral("load")) {
    STUMBLER_DBG("Got load Event ");
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
