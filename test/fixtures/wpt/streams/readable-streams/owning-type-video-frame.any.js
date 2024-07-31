// META: global=window,worker,shadowrealm
// META: script=../resources/test-utils.js
// META: script=../resources/rs-utils.js
'use strict';

function createVideoFrame()
{
  let init = {
    format: 'I420',
    timestamp: 1234,
    codedWidth: 4,
    codedHeight: 2
  };
  let data = new Uint8Array([
    1, 2, 3, 4, 5, 6, 7, 8,  // y
    1, 2,                    // u
    1, 2,                    // v
  ]);

  return new VideoFrame(data, init);
}

promise_test(async () => {
  const videoFrame = createVideoFrame();
  videoFrame.test = 1;
  const source = {
    start(controller) {
      assert_equals(videoFrame.format, 'I420');
      controller.enqueue(videoFrame, { transfer : [ videoFrame ] });
      assert_equals(videoFrame.format, null);
      assert_equals(videoFrame.test, 1);
    },
    type: 'owning'
  };

  const stream = new ReadableStream(source);
  // Cancelling the stream should close all video frames, thus no console messages of GCing VideoFrames should happen.
  stream.cancel();
}, 'ReadableStream of type owning should close serialized chunks');

promise_test(async () => {
  const videoFrame = createVideoFrame();
  videoFrame.test = 1;
  const source = {
    start(controller) {
      assert_equals(videoFrame.format, 'I420');
      controller.enqueue({ videoFrame }, { transfer : [ videoFrame ] });
      assert_equals(videoFrame.format, null);
      assert_equals(videoFrame.test, 1);
    },
    type: 'owning'
  };

  const stream = new ReadableStream(source);
  const reader = stream.getReader();

  const chunk = await reader.read();
  assert_equals(chunk.value.videoFrame.format, 'I420');
  assert_equals(chunk.value.videoFrame.test, undefined);

  chunk.value.videoFrame.close();
}, 'ReadableStream of type owning should transfer JS chunks with transferred values');

promise_test(async t => {
  const videoFrame = createVideoFrame();
  videoFrame.close();
  const source = {
    start(controller) {
      assert_throws_dom("DataCloneError", () => controller.enqueue(videoFrame, { transfer : [ videoFrame ] }));
    },
    type: 'owning'
  };

  const stream = new ReadableStream(source);
  const reader = stream.getReader();

  await promise_rejects_dom(t, "DataCloneError", reader.read());
}, 'ReadableStream of type owning should error when trying to enqueue not serializable values');

promise_test(async () => {
  const videoFrame = createVideoFrame();
  const source = {
    start(controller) {
      controller.enqueue(videoFrame, { transfer : [ videoFrame ] });
    },
    type: 'owning'
  };

  const stream = new ReadableStream(source);
  const [clone1, clone2] = stream.tee();

  const chunk1 = await clone1.getReader().read();
  const chunk2 = await clone2.getReader().read();

  assert_equals(videoFrame.format, null);
  assert_equals(chunk1.value.format, 'I420');
  assert_equals(chunk2.value.format, 'I420');

  chunk1.value.close();
  chunk2.value.close();
}, 'ReadableStream of type owning should clone serializable objects when teeing');

promise_test(async () => {
  const videoFrame = createVideoFrame();
  videoFrame.test = 1;
  const source = {
    start(controller) {
      assert_equals(videoFrame.format, 'I420');
      controller.enqueue({ videoFrame }, { transfer : [ videoFrame ] });
      assert_equals(videoFrame.format, null);
      assert_equals(videoFrame.test, 1);
    },
    type: 'owning'
  };

  const stream = new ReadableStream(source);
  const [clone1, clone2] = stream.tee();

  const chunk1 = await clone1.getReader().read();
  const chunk2 = await clone2.getReader().read();

  assert_equals(videoFrame.format, null);
  assert_equals(chunk1.value.videoFrame.format, 'I420');
  assert_equals(chunk2.value.videoFrame.format, 'I420');

  chunk1.value.videoFrame.close();
  chunk2.value.videoFrame.close();
}, 'ReadableStream of type owning should clone JS Objects with serializables when teeing');
