'use strict';

const common = require('../common');
const assert = require('assert');

const {
  readFileSync,
} = require('fs');

const {
  open,
} = require('fs/promises');

const check = readFileSync(__filename, { encoding: 'utf8' });

// Make sure the ReadableStream works...
(async () => {
  const dec = new TextDecoder();
  const file = await open(__filename);
  let data = '';
  for await (const chunk of file.readableWebStream())
    data += dec.decode(chunk);

  assert.strictEqual(check, data);

  assert.throws(() => file.readableWebStream(), {
    code: 'ERR_INVALID_STATE',
  });

  await file.close();
})().then(common.mustCall());

// Make sure that acquiring a ReadableStream fails if the
// FileHandle is already closed.
(async () => {
  const file = await open(__filename);
  await file.close();

  assert.throws(() => file.readableWebStream(), {
    code: 'ERR_INVALID_STATE',
  });
})().then(common.mustCall());

// Make sure that acquiring a ReadableStream fails if the
// FileHandle is already closing.
(async () => {
  const file = await open(__filename);
  file.close();

  assert.throws(() => file.readableWebStream(), {
    code: 'ERR_INVALID_STATE',
  });
})().then(common.mustCall());

// Make sure the ReadableStream is closed when the underlying
// FileHandle is closed.
(async () => {
  const file = await open(__filename);
  const readable = file.readableWebStream();
  const reader = readable.getReader();
  file.close();
  await reader.closed;
})().then(common.mustCall());

// Make sure the ReadableStream is closed when the underlying
// FileHandle is closed.
(async () => {
  const file = await open(__filename);
  const readable = file.readableWebStream();
  file.close();
  const reader = readable.getReader();
  await reader.closed;
})().then(common.mustCall());

// Make sure that the FileHandle is properly marked "in use"
// when a ReadableStream has been acquired for it.
(async () => {
  const file = await open(__filename);
  file.readableWebStream();
  const mc = new MessageChannel();
  mc.port1.onmessage = common.mustNotCall();
  assert.throws(() => mc.port2.postMessage(file, [file]), {
    code: 25,
    name: 'DataCloneError',
  });
  mc.port1.close();
  await file.close();
})().then(common.mustCall());

// Make sure 'byob' reader works
(async () => {
  const file = await open(__filename);
  const dec = new TextDecoder();
  const readable = file.readableWebStream();
  const reader = readable.getReader({ mode: 'byob' });

  let data = '';
  let result;
  do {
    const buff = new ArrayBuffer(100);
    result = await reader.read(new DataView(buff));
    if (result.value !== undefined) {
      data += dec.decode(result.value);
      assert.ok(result.value.byteLength <= 100);
    }
  } while (!result.done);

  assert.strictEqual(check, data);

  assert.throws(() => file.readableWebStream(), {
    code: 'ERR_INVALID_STATE',
  });

  await file.close();
})().then(common.mustCall());

// Make sure a warning is logged if a non-'bytes' type is passed.
(async () => {
  const file = await open(__filename);
  common.expectWarning({
    ExperimentalWarning: [
      'A non-"bytes" options.type has no effect. A byte-oriented steam is ' +
      'always created.',
    ],
  });
  const stream = file.readableWebStream({ type: 'foobar' });
  await stream.cancel();
  await file.close();
})().then(common.mustCall());
