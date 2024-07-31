// META: global=window,worker,shadowrealm
'use strict';

const error1 = new Error('error1');
error1.name = 'error1';

const error2 = new Error('error2');
error2.name = 'error2';

promise_test(t => {
  const ws = new WritableStream({
    start(controller) {
      controller.error(error1);
    }
  });
  return promise_rejects_exactly(t, error1, ws.getWriter().closed, 'stream should be errored');
}, 'controller.error() should error the stream');

test(() => {
  let controller;
  const ws = new WritableStream({
    start(c) {
      controller = c;
    }
  });
  ws.abort();
  controller.error(error1);
}, 'controller.error() on erroring stream should not throw');

promise_test(t => {
  let controller;
  const ws = new WritableStream({
    start(c) {
      controller = c;
    }
  });
  controller.error(error1);
  controller.error(error2);
  return promise_rejects_exactly(t, error1, ws.getWriter().closed, 'first controller.error() should win');
}, 'surplus calls to controller.error() should be a no-op');

promise_test(() => {
  let controller;
  const ws = new WritableStream({
    start(c) {
      controller = c;
    }
  });
  return ws.abort().then(() => {
    controller.error(error1);
  });
}, 'controller.error() on errored stream should not throw');

promise_test(() => {
  let controller;
  const ws = new WritableStream({
    start(c) {
      controller = c;
    }
  });
  return ws.getWriter().close().then(() => {
    controller.error(error1);
  });
}, 'controller.error() on closed stream should not throw');
