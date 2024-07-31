// META: global=window,worker,shadowrealm
// META: script=../resources/test-utils.js
// META: script=../resources/rs-utils.js
'use strict';

promise_test(async () => {
  const channel = new MessageChannel;
  const port1 = channel.port1;
  const port2 = channel.port2;

  const source = {
    start(controller) {
      controller.enqueue(port1, { transfer : [ port1 ] });
    },
    type: 'owning'
  };

  const stream = new ReadableStream(source);

  const chunk = await stream.getReader().read();

  assert_not_equals(chunk.value, port1);

  let promise = new Promise(resolve => port2.onmessage = e => resolve(e.data));
  chunk.value.postMessage("toPort2");
  assert_equals(await promise, "toPort2");

  promise = new Promise(resolve => chunk.value.onmessage = e => resolve(e.data));
  port2.postMessage("toPort1");
  assert_equals(await promise, "toPort1");
}, 'Transferred MessageChannel works as expected');

promise_test(async t => {
  const channel = new MessageChannel;
  const port1 = channel.port1;
  const port2 = channel.port2;

  const source = {
    start(controller) {
      controller.enqueue({ port1 }, { transfer : [ port1 ] });
    },
    type: 'owning'
  };

  const stream = new ReadableStream(source);
  const [clone1, clone2] = stream.tee();

  await promise_rejects_dom(t, "DataCloneError", clone2.getReader().read());
}, 'Second branch of owning ReadableStream tee should end up into errors with transfer only values');
