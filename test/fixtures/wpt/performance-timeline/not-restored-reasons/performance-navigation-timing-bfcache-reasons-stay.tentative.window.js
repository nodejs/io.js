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

// Ensure that notRestoredReasons are only updated after non BFCache navigation.
promise_test(async t => {
  const rcHelper = new RemoteContextHelper();
  // Open a window with noopener so that BFCache will work.
  const rc1 = await rcHelper.addWindow(
      /*config=*/ null, /*options=*/ {features: 'noopener'});
  // Use WebSocket to block BFCache.
  await useWebSocket(rc1);
  const rc1_url = await rc1.executeScript(() => {
    return location.href;
  });

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

  // This time no blocking feature is used, so the page is restored
  // from BFCache. Ensure that the previous reasons stay there.
  await assertBFCacheEligibility(rc1, /*shouldRestoreFromBFCache=*/ true);
  await assertNotRestoredReasonsEquals(
      rc1,
      /*url=*/ rc1_url,
      /*src=*/ null,
      /*id=*/ null,
      /*name=*/ null,
      /*reasons=*/[{'reason': 'websocket'}],
      /*children=*/ []);
});
