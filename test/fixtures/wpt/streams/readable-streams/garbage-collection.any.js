// META: global=window,worker,shadowrealm
// META: script=../resources/test-utils.js
// META: script=/common/gc.js
'use strict';

promise_test(async () => {

  let controller;
  new ReadableStream({
    start(c) {
      controller = c;
    }
  });

  await garbageCollect();

  return delay(50).then(() => {
    controller.close();
    assert_throws_js(TypeError, () => controller.close(), 'close should throw a TypeError the second time');
    controller.error();
  });

}, 'ReadableStreamController methods should continue working properly when scripts lose their reference to the ' +
   'readable stream');

promise_test(async () => {

  let controller;

  const closedPromise = new ReadableStream({
    start(c) {
      controller = c;
    }
  }).getReader().closed;

  await garbageCollect();

  return delay(50).then(() => controller.close()).then(() => closedPromise);

}, 'ReadableStream closed promise should fulfill even if the stream and reader JS references are lost');

promise_test(async t => {

  const theError = new Error('boo');
  let controller;

  const closedPromise = new ReadableStream({
    start(c) {
      controller = c;
    }
  }).getReader().closed;

  await garbageCollect();

  return delay(50).then(() => controller.error(theError))
                  .then(() => promise_rejects_exactly(t, theError, closedPromise));

}, 'ReadableStream closed promise should reject even if stream and reader JS references are lost');

promise_test(async () => {

  const rs = new ReadableStream({});

  rs.getReader();

  await garbageCollect();

  return delay(50).then(() => assert_throws_js(TypeError, () => rs.getReader(),
    'old reader should still be locking the stream even after garbage collection'));

}, 'Garbage-collecting a ReadableStreamDefaultReader should not unlock its stream');
