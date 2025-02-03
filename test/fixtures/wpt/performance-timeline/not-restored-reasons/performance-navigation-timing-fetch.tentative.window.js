// META: title=Ensure that ongoing fetch upon entering bfcache blocks bfcache and recorded.
// META: script=./test-helper.js
// META: script=/common/dispatcher/dispatcher.js
// META: script=/common/get-host-info.sub.js
// META: script=/common/utils.js
// META: script=/html/browsers/browsing-the-web/back-forward-cache/resources/rc-helper.js
// META: script=/html/browsers/browsing-the-web/remote-context-helper/resources/remote-context-helper.js
// META: timeout=long

'use strict';

promise_test(async t => {
  const rcHelper = new RemoteContextHelper();
  // Open a window with noopener so that BFCache will work.
  const rc1 = await rcHelper.addWindow(
      /*config=*/ null, /*options=*/ {features: 'noopener'});
  const rc1_url = await rc1.executeScript(() => {
    return location.href;
  });
  const wavURL = new URL(get_host_info().HTTP_REMOTE_ORIGIN + '/fetch/range/resources/long-wav.py');
  await rc1.executeScript((wavURL) => {
    // Register pagehide handler to create a fetch request.
    addEventListener('pagehide', (wavURL) => {
      fetch(wavURL, {
        keepalive: true
      });
    })
  });

  // Check the BFCache result and the reported reasons.
  await assertBFCacheEligibility(rc1, /*shouldRestoreFromBFCache=*/ false);
  await assertNotRestoredReasonsEquals(
      rc1,
      /*url=*/ rc1_url,
      /*src=*/ null,
      /*id=*/ null,
      /*name=*/ null,
      /*reasons=*/[{'reason': 'fetch'}],
      /*children=*/[]);
});