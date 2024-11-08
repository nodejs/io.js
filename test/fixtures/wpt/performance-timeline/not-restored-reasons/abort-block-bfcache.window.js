// META: title=Aborting a parser should block bfcache
// META: script=./test-helper.js
// META: timeout=long


async_test(t => {
  if (!sessionStorage.getItem("pageVisited")) {
    // This is the first time loading the page.
    sessionStorage.setItem("pageVisited", 1);
    t.step_timeout(() => {
        // Go to another page and instantly come back to this page.
        location.href = new URL("../resources/going-back.html", window.location);
    }, 0);
    // Abort parsing in the middle of loading the page.
    window.stop();
  } else {
    const nrr = performance.getEntriesByType('navigation')[0].notRestoredReasons;
    assert_true(ReasonsInclude(nrr.reasons, "parser-aborted"));
    t.done();
  }
}, "aborting a parser should block bfcache.");
