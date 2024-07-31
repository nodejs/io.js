function getOrCreateID(key) {
  if (!localStorage.getItem(key)) {
    const newID = +new Date() + "-" + Math.random();
    localStorage.setItem(key, newID);
  }
  return localStorage.getItem(key);
}

function addIframePromise(url) {
  return new Promise(resolve => {
    const iframe = document.createElement("iframe");
    iframe.style.display = "none";
    iframe.src = url;
    iframe.addEventListener("load", (e) => {
      resolve(iframe);
    }, {once: true});

    document.body.appendChild(iframe);
  });
}
