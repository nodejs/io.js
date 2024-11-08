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

// Ensure that empty attributes are reported as empty strings and missing
// attributes are reported as null.
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
      /*attributes=*/ {id: '', name: ''},
  );
  const rc2_child = await rc1.addIframe(
    /*extraConfig=*/ {
      origin: 'HTTP_REMOTE_ORIGIN',
      scripts: [],
      headers: [],
    },
    /*attributes=*/ {},
  );
  const rc3_child = await rc1.addIframe(
    /*extraConfig=*/ {},
    /*attributes=*/ {},
  );
  const rc4_child = await rc1.addIframe(
    /*extraConfig=*/ {},
    /*attributes=*/ {id: '', name: ''},
  );
  // Use WebSocket to block BFCache.
  await useWebSocket(rc1);
  const rc1_child_url = await rc1_child.executeScript(() => {
    return location.href;
  });
  const rc2_child_url = await rc2_child.executeScript(() => {
    return location.href;
  });
  const rc3_child_url = await rc3_child.executeScript(() => {
    return location.href;
  });
  const rc4_child_url = await rc4_child.executeScript(() => {
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
      /*children=*/[{
        'url': null,
        'src': rc1_child_url,
        // Id and name should be empty.
        'id': '',
        'name': '',
        'reasons': null,
        'children': null
      }, {
        'url': null,
        'src': rc2_child_url,
        // Id and name should be null.
        'id': null,
        'name': null,
        'reasons': null,
        'children': null
      },{
        'url': rc3_child_url,
        'src': rc3_child_url,
        // Id and name should be null.
        'id': null,
        'name': null,
        'reasons': [],
        'children': []
      }, {
        'url': rc4_child_url,
        'src': rc4_child_url,
        'id': '',
        'name': '',
        'reasons': [],
        'children': []
      }]);
});