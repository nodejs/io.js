"use strict";

function receiveEventOnce(target, name) {
  return new Promise(resolve => {
    target.addEventListener(
      name,
      ev => {
        resolve(ev);
      },
      { once: true }
    );
  });
}

async function postAndTestMessageEvent(data, transfer, title) {
  postMessage(data, "*", transfer);
  const messagePortCount = transfer.filter(i => i instanceof MessagePort)
    .length;
  const ev = await receiveEventOnce(window, "message");
  assert_equals(
    ev.ports.length,
    messagePortCount,
    `Correct number of ports ${title}`
  );
  for (const [i, port] of ev.ports.entries()) {
    assert_true(
      port instanceof MessagePort,
      `ports[${i}] include MessagePort ${title}`
    );
  }
  for (const [key, value] of Object.entries(data)) {
    assert_true(
      ev.data[key] instanceof value.constructor,
      `data.${key} has correct interface ${value.constructor.name} ${title}`
    );
  }
}

async function transferMessagePortWithOrder1(stream) {
  const channel = new MessageChannel();
  await postAndTestMessageEvent(
    { stream, port2: channel.port2 },
    [stream, channel.port2],
    `when transferring [${stream.constructor.name}, MessagePort]`
  );
}

async function transferMessagePortWithOrder2(stream) {
  const channel = new MessageChannel();
  await postAndTestMessageEvent(
    { stream, port2: channel.port2 },
    [channel.port2, stream],
    `when transferring [MessagePort, ${stream.constructor.name}]`
  );
}

async function transferMessagePortWithOrder3(stream) {
  const channel = new MessageChannel();
  await postAndTestMessageEvent(
    { port1: channel.port1, stream, port2: channel.port2 },
    [channel.port1, stream, channel.port2],
    `when transferring [MessagePort, ${stream.constructor.name}, MessagePort]`
  );
}

async function transferMessagePortWithOrder4(stream) {
  const channel = new MessageChannel();
  await postAndTestMessageEvent(
    {},
    [channel.port1, stream, channel.port2],
    `when transferring [MessagePort, ${stream.constructor.name}, MessagePort] but with empty data`
  );
}

async function transferMessagePortWithOrder5(stream) {
  const channel = new MessageChannel();
  await postAndTestMessageEvent(
    { port2: channel.port2, port1: channel.port1, stream },
    [channel.port1, stream, channel.port2],
    `when transferring [MessagePort, ${stream.constructor.name}, MessagePort] but with data having different order`
  );
}

async function transferMessagePortWithOrder6(stream) {
  const channel = new MessageChannel();
  await postAndTestMessageEvent(
    { port2: channel.port2, port1: channel.port1 },
    [channel.port1, stream, channel.port2],
    `when transferring [MessagePort, ${stream.constructor.name}, MessagePort] but with stream not being in the data`
  );
}

async function transferMessagePortWithOrder7(stream) {
  const channel = new MessageChannel();
  await postAndTestMessageEvent(
    { stream },
    [channel.port1, stream, channel.port2],
    `when transferring [MessagePort, ${stream.constructor.name}, MessagePort] but with ports not being in the data`
  );
}

async function transferMessagePortWith(constructor) {
  await transferMessagePortWithOrder1(new constructor());
  await transferMessagePortWithOrder2(new constructor());
  await transferMessagePortWithOrder3(new constructor());
}

async function advancedTransferMessagePortWith(constructor) {
  await transferMessagePortWithOrder4(new constructor());
  await transferMessagePortWithOrder5(new constructor());
  await transferMessagePortWithOrder6(new constructor());
  await transferMessagePortWithOrder7(new constructor());
}

async function mixedTransferMessagePortWithOrder1() {
  const channel = new MessageChannel();
  const readable = new ReadableStream();
  const writable = new WritableStream();
  const transform = new TransformStream();
  await postAndTestMessageEvent(
    {
      readable,
      writable,
      transform,
      port1: channel.port1,
      port2: channel.port2,
    },
    [readable, writable, transform, channel.port1, channel.port2],
    `when transferring [ReadableStream, WritableStream, TransformStream, MessagePort, MessagePort]`
  );
}

async function mixedTransferMessagePortWithOrder2() {
  const channel = new MessageChannel();
  const readable = new ReadableStream();
  const writable = new WritableStream();
  const transform = new TransformStream();
  await postAndTestMessageEvent(
    { readable, writable, transform },
    [transform, channel.port1, readable, channel.port2, writable],
    `when transferring [TransformStream, MessagePort, ReadableStream, MessagePort, WritableStream]`
  );
}

async function mixedTransferMessagePortWithOrder3() {
  const channel = new MessageChannel();
  const readable1 = new ReadableStream();
  const readable2 = new ReadableStream();
  const writable1 = new WritableStream();
  const writable2 = new WritableStream();
  const transform1 = new TransformStream();
  const transform2 = new TransformStream();
  await postAndTestMessageEvent(
    { readable1, writable1, transform1, readable2, writable2, transform2 },
    [
      transform2,
      channel.port1,
      readable1,
      channel.port2,
      writable2,
      readable2,
      writable1,
      transform1,
    ],
    `when transferring [TransformStream, MessagePort, ReadableStream, MessagePort, WritableStream, ReadableStream, WritableStream, TransformStream] but with the data having different order`
  );
}

async function mixedTransferMessagePortWith() {
  await mixedTransferMessagePortWithOrder1();
  await mixedTransferMessagePortWithOrder2();
  await mixedTransferMessagePortWithOrder3();
}

promise_test(async t => {
  await transferMessagePortWith(ReadableStream);
}, "Transferring a MessagePort with a ReadableStream should set `.ports`");

promise_test(async t => {
  await transferMessagePortWith(WritableStream);
}, "Transferring a MessagePort with a WritableStream should set `.ports`");

promise_test(async t => {
  await transferMessagePortWith(TransformStream);
}, "Transferring a MessagePort with a TransformStream should set `.ports`");

promise_test(async t => {
  await advancedTransferMessagePortWith(ReadableStream);
}, "Transferring a MessagePort with a ReadableStream should set `.ports`, advanced");

promise_test(async t => {
  await advancedTransferMessagePortWith(WritableStream);
}, "Transferring a MessagePort with a WritableStream should set `.ports`, advanced");

promise_test(async t => {
  await advancedTransferMessagePortWith(TransformStream);
}, "Transferring a MessagePort with a TransformStream should set `.ports`, advanced");

promise_test(async t => {
  await mixedTransferMessagePortWith();
}, "Transferring a MessagePort with multiple streams should set `.ports`");

test(() => {
  assert_throws_dom("DataCloneError", () =>
    postMessage({ stream: new ReadableStream() }, "*")
  );
}, "ReadableStream must not be serializable");

test(() => {
  assert_throws_dom("DataCloneError", () =>
    postMessage({ stream: new WritableStream() }, "*")
  );
}, "WritableStream must not be serializable");

test(() => {
  assert_throws_dom("DataCloneError", () =>
    postMessage({ stream: new TransformStream() }, "*")
  );
}, "TransformStream must not be serializable");
