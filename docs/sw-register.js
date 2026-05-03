(function () {
  "use strict";

  if (!("serviceWorker" in navigator))
    return;

  const statusEl = document.getElementById("status");

  function showServiceWorkerMessage(event) {
    const data = event.data || {};
    if (data.type !== "offline-cache" || !data.msg)
      return;
    console.log(data.msg);
    if (statusEl && !statusEl.classList.contains("error"))
      statusEl.textContent = data.msg;
  }

  navigator.serviceWorker.addEventListener("message", showServiceWorkerMessage);

  window.addEventListener("load", () => {
    navigator.serviceWorker.register("./sw.js").catch(err => {
      console.warn("Service worker registration failed", err);
    });
  });
})();
