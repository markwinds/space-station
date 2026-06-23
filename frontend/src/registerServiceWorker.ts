const registerServiceWorker = () => {
  if (!import.meta.env.PROD || !("serviceWorker" in navigator)) {
    return;
  }

  window.addEventListener("load", () => {
    const baseUrl = import.meta.env.BASE_URL;
    navigator.serviceWorker
      .register(`${baseUrl}sw.js`, { scope: baseUrl, updateViaCache: "none" })
      .then((registration) => registration.update())
      .catch(() => {
        // The app remains usable without offline support.
      });
  });
};

registerServiceWorker();
