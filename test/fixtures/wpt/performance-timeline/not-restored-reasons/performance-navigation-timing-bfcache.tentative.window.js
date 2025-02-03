// META: title=RemoteContextHelper navigation using BFCache
// META: script=./test-helper.js
// META: script=/common/dispatcher/dispatcher.js
// META: script=/common/get-host-info.sub.js
// META: script=/common/utils.js
// META: script=/html/browsers/browsing-the-web/back-forward-cache/resources/rc-helper.js
// META: script=/html/browsers/browsing-the-web/remote-context-helper/resources/remote-context-helper.js
// META: timeout=long

'use strict';

// Ensure that notRestoredReasons is empty for successful BFCache restore.
promise_test(async t => {
  const rcHelper = new RemoteContextHelper();

  // Open a window with noopener so that BFCache will work.
  const rc1 = await rcHelper.addWindow(
      /*config=*/ null, /*options=*/ {features: 'noopener'});

  // Check the BFCache result and verify that no reasons are recorded
  // for successful restore.
  await assertBFCacheEligibility(rc1, /*shouldRestoreFromBFCache=*/ true);
  assert_true(await rc1.executeScript(() => {
    let reasons =
        performance.getEntriesByType('navigation')[0].notRestoredReasons;
    return reasons === null;
  }));
});