// META: script=/common/get-host-info.sub.js
// META: script=/common/utils.js
// META: script=/common/dispatcher/dispatcher.js
// META: script=/html/cross-origin-embedder-policy/credentialless/resources/common.js
// META: script=/html/anonymous-iframe/resources/common.js

promise_test(async test => {
    const same_origin= get_host_info().HTTPS_ORIGIN;
    const cross_origin = get_host_info().HTTPS_REMOTE_ORIGIN;
    const reply_token = token();

    for(iframe of [
      newIframe(same_origin),
      newIframe(cross_origin),
    ]) {
      send(iframe, `
        try {
          let c = window.localStorage;
          send("${reply_token}","OK");
        } catch (exception) {
          send("${reply_token}","ERROR");
        }
      `);
    }
    assert_equals(await receive(reply_token), "OK");
    assert_equals(await receive(reply_token), "OK");
  }, "LocalStorage should be accessible on both same_origin and cross_origin iframes");