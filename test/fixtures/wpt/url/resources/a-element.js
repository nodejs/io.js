promise_test(() => Promise.all([
  fetch("resources/urltestdata.json").then(res => res.json()),
  fetch("resources/urltestdata-javascript-only.json").then(res => res.json()),
]).then((tests) => tests.flat()).then(runURLTests), "Loading data…");

function setBase(base) {
  document.getElementById("base").href = base;
}

function bURL(url, base) {
  setBase(base);
  const a = document.createElement("a");
  a.setAttribute("href", url);
  return a;
}

function runURLTests(urlTests) {
  for (const expected of urlTests) {
    // Skip comments
    if (typeof expected === "string")
      continue;

    // Fragments are relative against "about:blank"
    if (expected.relativeTo === "any-base")
      continue;

    // HTML special cases data: and javascript: URLs in <base>
    if (expected.base !== null && (expected.base.startsWith("data:") || expected.base.startsWith("javascript:")))
      continue;

    // We cannot use a null base for HTML tests
    const base = expected.base === null ? "about:blank" : expected.base;

    function getKey(expected) {
      if (expected.protocol) {
        return expected.protocol.replace(":", "");
      }
      if (expected.failure) {
        return expected.input.split(":")[0];
      }
      return "other";
    }

    subsetTestByKey(getKey(expected), test, function() {
      var url = bURL(expected.input, base)
      if(expected.failure) {
        if(url.protocol !== ':') {
          assert_unreached("Expected URL to fail parsing")
        }
        assert_equals(url.href, expected.input, "failure should set href to input")
        return
      }

      assert_equals(url.href, expected.href, "href")
      assert_equals(url.protocol, expected.protocol, "protocol")
      assert_equals(url.username, expected.username, "username")
      assert_equals(url.password, expected.password, "password")
      assert_equals(url.host, expected.host, "host")
      assert_equals(url.hostname, expected.hostname, "hostname")
      assert_equals(url.port, expected.port, "port")
      assert_equals(url.pathname, expected.pathname, "pathname")
      assert_equals(url.search, expected.search, "search")
      assert_equals(url.hash, expected.hash, "hash")
    }, "Parsing: <" + expected.input + "> against <" + base + ">")
  }
}
