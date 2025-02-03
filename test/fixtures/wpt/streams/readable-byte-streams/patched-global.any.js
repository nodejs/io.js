// META: global=window,worker,shadowrealm
// META: script=../resources/test-utils.js
'use strict';

// Tests which patch the global environment are kept separate to avoid
// interfering with other tests.

promise_test(async (t) => {
  let controller;
  const rs = new ReadableStream({
    type: 'bytes',
    start(c) {
      controller = c;
    }
  });
  const reader = rs.getReader({mode: 'byob'});

  const length = 0x4000;
  const buffer = new ArrayBuffer(length);
  const bigArray = new BigUint64Array(buffer, length - 8, 1);

  const read1 = reader.read(new Uint8Array(new ArrayBuffer(0x100)));
  const read2 = reader.read(bigArray);

  let flag = false;
  Object.defineProperty(Object.prototype, 'then', {
    get: t.step_func(() => {
      if (!flag) {
        flag = true;
        assert_equals(controller.byobRequest, null, 'byobRequest should be null after filling both views');
      }
    }),
    configurable: true
  });
  t.add_cleanup(() => {
    delete Object.prototype.then;
  });

  controller.enqueue(new Uint8Array(0x110).fill(0x42));
  assert_true(flag, 'patched then() should be called');

  // The first read() is filled entirely with 0x100 bytes
  const result1 = await read1;
  assert_false(result1.done, 'result1.done');
  assert_typed_array_equals(result1.value, new Uint8Array(0x100).fill(0x42), 'result1.value');

  // The second read() is filled with the remaining 0x10 bytes
  const result2 = await read2;
  assert_false(result2.done, 'result2.done');
  assert_equals(result2.value.constructor, BigUint64Array, 'result2.value constructor');
  assert_equals(result2.value.byteOffset, length - 8, 'result2.value byteOffset');
  assert_equals(result2.value.length, 1, 'result2.value length');
  assert_array_equals([...result2.value], [0x42424242_42424242n], 'result2.value contents');
}, 'Patched then() sees byobRequest after filling all pending pull-into descriptors');
