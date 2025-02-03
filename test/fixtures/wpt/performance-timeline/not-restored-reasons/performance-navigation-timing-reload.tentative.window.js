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
  const rc1_url = await rc1.executeScript(() => {
    return location.href;
  });
  // Use WebSocket to block BFCache.
  await useWebSocket(rc1);

  // Check the BFCache result and the reported reasons.
  await assertBFCacheEligibility(rc1, /*shouldRestoreFromBFCache=*/ false);
  await assertNotRestoredReasonsEquals(
        rc1,
        /*url=*/ rc1_url,
        /*src=*/ null,
        /*id=*/ null,
        /*name=*/ null,
        /*reasons=*/[{'reason': 'websocket'}],
        /*children=*/ []);

  // Reload.
  await rc1.navigate(() => {
    location.reload();
  }, []);

  // Becauase of the reload, notRestoredReasons is reset.
  const navigation_entry = await rc1.executeScript(() => {
    return performance.getEntriesByType('navigation')[0];
  });

  assert_equals(
      navigation_entry.notRestoredReasons, null,
      'Expected notRestoredReasons is null.');
});
