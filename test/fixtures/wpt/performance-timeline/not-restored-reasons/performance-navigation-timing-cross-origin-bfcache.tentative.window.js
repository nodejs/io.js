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

// Ensure that cross-origin subtree's reasons are not exposed to
// notRestoredReasons.
promise_test(async t => {
  const rcHelper = new RemoteContextHelper();
  // Open a window with noopener so that BFCache will work.
  const rc1 = await rcHelper.addWindow(
      /*config=*/ null, /*options=*/ {features: 'noopener'});
  const rc1_url = await rc1.executeScript(() => {
    return location.href;
  });
  // Add a cross-origin iframe.
  const rc1_child = await rc1.addIframe(
      /*extraConfig=*/ {
        origin: 'HTTP_REMOTE_ORIGIN',
        scripts: [],
        headers: [],
      },
      /*attributes=*/ {id: 'test-id'},
  );
  // Use WebSocket to block BFCache.
  await useWebSocket(rc1_child);
  const rc1_child_url = await rc1_child.executeScript(() => {
    return location.href;
  });
  // Add a child to the iframe.
  const rc1_grand_child = await rc1_child.addIframe();
  const rc1_grand_child_url = await rc1_grand_child.executeScript(() => {
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
      /*reasons=*/[{'reason': "masked"}],
      /*children=*/[{
        'url': null,
        'src': rc1_child_url,
        'id': 'test-id',
        'name': null,
        'reasons': null,
        'children': null
      }]);
});