(function () {
  let now = window.performance.now();
  while (window.performance.now() < now + 60);
}());