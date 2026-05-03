var CACHE_NAME = "v10";

var urlsToCache = [
  "/",
  "/ide-shell",
  "/manifest.json",
  "/icon-192.png",
  "/icon-512.png",
  "/apple-touch-icon.png",
  "/og-image.jpg",
  "/sw-register.js",
  "/ide-resources.js",
  "/runtime.js",
  "/editor.js",
  "/wabt.js",
  "/tcc.wasm",
  "/libc.wasm"
];

self.addEventListener("install", function (event) {
  postMsg({ msg: "Updating..." });
  var urlsAddVersion = urlsToCache.map(function (url) {
    return url + "?ver=" + CACHE_NAME;
  });

  event.waitUntil(
    caches.open(CACHE_NAME)
      .then(function (cache) {
        console.log("Opened cache");
        return cache.addAll(urlsAddVersion);
      })
      .then(function () {
        console.log("Cache downloaded");
        return self.skipWaiting();
      })
  );
});

self.addEventListener("fetch", function (event) {
  if (event.request.method !== "GET")
    return;

  event.respondWith(
    caches.match(event.request, {
      ignoreSearch: true
    }).then(function (response) {
      if (response)
        return response;
      console.log("cache miss", event.request.url);
      return fetch(event.request);
    })
  );
});

self.addEventListener("activate", function (event) {
  console.log("activated, remove unused cache...");
  var cacheAllowlist = [CACHE_NAME];
  event.waitUntil(
    caches.keys().then(function (cacheNames) {
      return Promise.all(
        cacheNames.map(function (cacheName) {
          if (cacheAllowlist.indexOf(cacheName) === -1) {
            console.log(cacheName);
            return caches.delete(cacheName);
          }
          return undefined;
        })
      );
    }).then(function () {
      return self.clients.claim();
    })
  );
  postMsg({ msg: "Updated!" });
});

function postMsg(obj) {
  self.clients.matchAll({ includeUncontrolled: true, type: "window" }).then(function (arr) {
    for (var i = 0; i < arr.length; i += 1)
      arr[i].postMessage(obj);
  });
}
