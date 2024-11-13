// META: title=RemoteContextHelper navigation using BFCache
// META: script=./test-helper.js
// META: script=/common/dispatcher/dispatcher.js
// META: script=/common/get-host-info.sub.js
// META: script=/common/utils.js
// META: script=/html/browsers/browsing-the-web/back-forward-cache/resources/rc-helper.js
// META: script=/html/browsers/browsing-the-web/remote-context-helper/resources/remote-context-helper.js
// META: script=/websockets/constants.sub.js
// META: timeout=long

'use strict';
const {ORIGIN, REMOTE_ORIGIN} = get_host_info();

// Ensure that notRestoredReasons reset after the server redirect.
promise_test(async t => {
  const rcHelper = new RemoteContextHelper();
  // Open a window with noopener so that BFCache will work.
  const rc1 = await rcHelper.addWindow(
      /*config=*/ null, /*options=*/ {features: 'noopener'});
  // Use WebSocket to block BFCache.
  await useWebSocket(rc1);

  // Create a remote context with the redirected URL.
  let rc1_redirected =
      await rcHelper.createContext(/*extraConfig=*/ {
        origin: 'HTTP_ORIGIN',
        scripts: [],
        headers: [],
      });

  const redirectUrl =
      `${ORIGIN}/common/redirect.py?location=${encodeURIComponent(rc1_redirected.url)}`;
  // Replace the history state.
  await rc1.executeScript((url) => {
    window.history.replaceState(null, '', url);
  }, [redirectUrl]);

  // Navigate away.
  const newRemoteContextHelper = await rc1.navigateToNew();

  // Go back.
  await newRemoteContextHelper.historyBack();

  const navigation_entry = await rc1_redirected.executeScript(() => {
    return performance.getEntriesByType('navigation')[0];
  });
  assert_equals(
      navigation_entry.redirectCount, 1, 'Expected redirectCount is 1.');
  // Becauase of the redirect, notRestoredReasons is reset.
  assert_equals(
      navigation_entry.notRestoredReasons, null,
      'Expected notRestoredReasons is null.');
});
