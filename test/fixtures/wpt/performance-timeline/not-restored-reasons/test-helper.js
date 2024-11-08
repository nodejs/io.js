// META: script=../../html/browsers/browsing-the-web/back-forward-cache/resources/rc-helper.js

async function assertNotRestoredReasonsEquals(
    remoteContextHelper, url, src, id, name, reasons, children) {
  let result = await remoteContextHelper.executeScript(() => {
    return performance.getEntriesByType('navigation')[0].notRestoredReasons;
  });
  assertReasonsStructEquals(
      result, url, src, id, name, reasons, children);
}

function assertReasonsStructEquals(
    result, url, src, id, name, reasons, children) {
  assert_equals(result.url, url);
  assert_equals(result.src, src);
  assert_equals(result.id, id);
  assert_equals(result.name, name);

  // Reasons should match.
  let expected = new Set(reasons);
  let actual = new Set(result.reasons);
  matchReasons(extractReason(expected), extractReason(actual));

  // Children should match.
  if (children == null) {
    assert_equals(result.children, children);
  } else {
    for (let j = 0; j < children.length; j++) {
      assertReasonsStructEquals(
          result.children[j], children[j].url,
          children[j].src, children[j].id, children[j].name, children[j].reasons,
          children[j].children);
    }
  }
}

function ReasonsInclude(reasons, targetReason) {
  for (const reason of reasons) {
    if (reason.reason == targetReason) {
      return true;
    }
  }
  return false;
}

// Requires:
// - /websockets/constants.sub.js in the test file and pass the domainPort
// constant here.
async function useWebSocket(remoteContextHelper) {
  let return_value = await remoteContextHelper.executeScript((domain) => {
    return new Promise((resolve) => {
      var webSocketInNotRestoredReasonsTests = new WebSocket(domain + '/echo');
      webSocketInNotRestoredReasonsTests.onopen = () => { resolve(42); };
    });
  }, [SCHEME_DOMAIN_PORT]);
  assert_equals(return_value, 42);
}