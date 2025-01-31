promise_test(() => Promise.all([
  fetch("resources/urltestdata.json").then(res => res.json()),
  fetch("resources/urltestdata-javascript-only.json").then(res => res.json()),
]).then((tests) => tests.flat()).then(runURLTests), "Loading data…");

function setBase(base) {
  document.getElementById("base").href = base
}

function bURL(url, base) {
  setBase(base);
  const a = document.createElement("a");
  a.setAttribute("href", url);
  return a;
}

function runURLTests(urlTests) {
  for (const expected of urlTests) {
    // Skip comments and tests without "origin" expectation
    if (typeof expected === "string" || !("origin" in expected))
      continue;

    // Fragments are relative against "about:blank" (this might always be redundant due to requiring "origin" in expected)
    if (expected.base === null && expected.input.startsWith("#"))
      continue;

    // HTML special cases data: and javascript: URLs in <base>
    if (expected.base !== null && (expected.base.startsWith("data:") || expected.base.startsWith("javascript:")))
      continue;

    // We cannot use a null base for HTML tests
    const base = expected.base === null ? "about:blank" : expected.base;

    test(function() {
      var url = bURL(expected.input, base)
      assert_equals(url.origin, expected.origin, "origin")
    }, "Parsing origin: <" + expected.input + "> against <" + base + ">")
  }
}
