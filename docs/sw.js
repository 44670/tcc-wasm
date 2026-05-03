const CACHE_NAME = "tcc-wasm-v8";
const STATIC_ASSETS = [
  "./manifest.json",
  "./icon-192.png",
  "./icon-512.png",
  "./apple-touch-icon.png",
  "./sw-register.js",
  "./ide-resources.js",
  "./runtime.js",
  "./editor.js",
  "./wabt.js",
  "./tcc.wasm",
  "./libc.wasm",
  "./og-image.jpg"
];
const ROUTES = [
  {
    canonical: "./index.html",
    clean: "./",
    aliases: ["./", "./index.html"]
  },
  {
    canonical: "./ide-shell.html",
    clean: "./ide-shell",
    aliases: ["./ide-shell", "./ide-shell.html"]
  }
];

function scopeUrl(path) {
  return new URL(path, self.registration.scope).toString();
}

function versionedUrl(url) {
  const resolved = new URL(url, self.registration.scope);
  resolved.searchParams.set("ver", CACHE_NAME);
  return resolved.toString();
}

function samePath(left, right) {
  const leftUrl = new URL(left);
  const rightUrl = new URL(right);
  return leftUrl.origin === rightUrl.origin && leftUrl.pathname === rightUrl.pathname;
}

function routeFor(url) {
  for (const route of ROUTES) {
    if (route.aliases.some(alias => samePath(url, scopeUrl(alias))))
      return route;
  }
  return null;
}

function cacheStorageRequest(request) {
  const route = routeFor(request.url);
  if (route)
    return new Request(versionedUrl(route.canonical));
  return request;
}

function postMsg(obj) {
  self.clients.matchAll({ includeUncontrolled: true, type: "window" }).then(clients => {
    for (const client of clients)
      client.postMessage(obj);
  });
}

async function cacheRoute(cache, route) {
  const request = new Request(versionedUrl(route.canonical), {
    cache: "reload",
    redirect: "follow"
  });
  const response = await fetch(request);
  if (!response.ok)
    throw new Error(`HTTP ${response.status} for ${route.canonical}`);
  await cache.put(new Request(versionedUrl(route.canonical)), response);
}

async function installCache() {
  postMsg({ type: "offline-cache", msg: "Updating offline cache..." });
  const cache = await caches.open(CACHE_NAME);
  await cache.addAll(STATIC_ASSETS.map(versionedUrl));
  await Promise.all(ROUTES.map(route => cacheRoute(cache, route)));
  postMsg({ type: "offline-cache", msg: "Offline cache ready." });
  await self.skipWaiting();
}

async function deleteOldCaches() {
  const cacheNames = await caches.keys();
  await Promise.all(cacheNames.map(cacheName => {
    if (cacheName !== CACHE_NAME)
      return caches.delete(cacheName);
    return undefined;
  }));
  await self.clients.claim();
}

function shouldRuntimeCache(request, response) {
  const url = new URL(request.url);
  if (url.origin === self.location.origin)
    return response.ok;
  return false;
}

function cleanUrlRedirect(request) {
  if (request.mode !== "navigate")
    return null;

  const requestUrl = new URL(request.url);
  for (const route of ROUTES) {
    if (!samePath(requestUrl, scopeUrl(route.canonical)))
      continue;
    const redirectUrl = new URL(scopeUrl(route.clean));
    redirectUrl.search = requestUrl.search;
    return Response.redirect(redirectUrl.toString(), 308);
  }
  return null;
}

function cacheLookupUrls(request) {
  const requestUrl = new URL(request.url);
  const urls = [requestUrl.toString()];
  const route = routeFor(requestUrl);

  if (route)
    urls.push(scopeUrl(route.canonical));

  return Array.from(new Set(urls));
}

async function matchCached(cache, request) {
  for (const url of cacheLookupUrls(request)) {
    const cached = await cache.match(url, { ignoreSearch: true });
    if (cached)
      return cached;
  }
  return null;
}

async function cacheFirst(request) {
  const cache = await caches.open(CACHE_NAME);
  const cached = await matchCached(cache, request);
  if (cached)
    return cached;

  try {
    const response = await fetch(request);
    if (shouldRuntimeCache(request, response))
      await cache.put(cacheStorageRequest(request), response.clone());
    return response;
  } catch (err) {
    if (request.mode === "navigate") {
      const fallback = await cache.match(scopeUrl("./index.html"), { ignoreSearch: true });
      if (fallback)
        return fallback;
    }
    throw err;
  }
}

self.addEventListener("install", event => {
  event.waitUntil(installCache());
});

self.addEventListener("activate", event => {
  event.waitUntil(deleteOldCaches());
});

self.addEventListener("fetch", event => {
  if (event.request.method !== "GET")
    return;

  const url = new URL(event.request.url);
  if (url.protocol !== "http:" && url.protocol !== "https:")
    return;

  const redirect = cleanUrlRedirect(event.request);
  if (redirect) {
    event.respondWith(redirect);
    return;
  }

  event.respondWith(cacheFirst(event.request));
});
