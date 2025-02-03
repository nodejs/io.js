// META: script=/common/subset-tests-by-key.js
// META: timeout=long
// META: variant=?include=file
// META: variant=?include=javascript
// META: variant=?include=mailto
// META: variant=?exclude=(file|javascript|mailto)

function runURLTests(urlTests) {
  for (const expected of urlTests) {
    // Skip comments
    if (typeof expected === "string")
      continue;

    const base = expected.base !== null ? expected.base : undefined;

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
      if (expected.failure) {
        assert_throws_js(TypeError, function() {
          new URL(expected.input, base);
        });
        return;
      }

      const url = new URL(expected.input, base);
      assert_equals(url.href, expected.href, "href")
      assert_equals(url.protocol, expected.protocol, "protocol")
      assert_equals(url.username, expected.username, "username")
      assert_equals(url.password, expected.password, "password")
      assert_equals(url.host, expected.host, "host")
      assert_equals(url.hostname, expected.hostname, "hostname")
      assert_equals(url.port, expected.port, "port")
      assert_equals(url.pathname, expected.pathname, "pathname")
      assert_equals(url.search, expected.search, "search")
      if ("searchParams" in expected) {
        assert_true("searchParams" in url)
        assert_equals(url.searchParams.toString(), expected.searchParams, "searchParams")
      }
      assert_equals(url.hash, expected.hash, "hash")
    }, `Parsing: <${expected.input}> ${base ? "against <" + base + ">" : "without base"}`)
  }
}

promise_test(() => Promise.all([
  fetch("resources/urltestdata.json").then(res => res.json()),
  fetch("resources/urltestdata-javascript-only.json").then(res => res.json()),
]).then((tests) => tests.flat()).then(runURLTests), "Loading data…");
