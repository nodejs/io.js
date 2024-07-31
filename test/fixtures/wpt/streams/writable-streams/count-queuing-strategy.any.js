// META: global=window,worker,shadowrealm
'use strict';

test(() => {
  new WritableStream({}, new CountQueuingStrategy({ highWaterMark: 4 }));
}, 'Can construct a writable stream with a valid CountQueuingStrategy');

promise_test(() => {
  const dones = Object.create(null);

  const ws = new WritableStream(
    {
      write(chunk) {
        return new Promise(resolve => {
          dones[chunk] = resolve;
        });
      }
    },
    new CountQueuingStrategy({ highWaterMark: 0 })
  );

  const writer = ws.getWriter();
  let writePromiseB;
  let writePromiseC;

  return Promise.resolve().then(() => {
    assert_equals(writer.desiredSize, 0, 'desiredSize should be initially 0');

    const writePromiseA = writer.write('a');
    assert_equals(writer.desiredSize, -1, 'desiredSize should be -1 after 1st write()');

    writePromiseB = writer.write('b');
    assert_equals(writer.desiredSize, -2, 'desiredSize should be -2 after 2nd write()');

    dones.a();
    return writePromiseA;
  }).then(() => {
    assert_equals(writer.desiredSize, -1, 'desiredSize should be -1 after completing 1st write()');

    dones.b();
    return writePromiseB;
  }).then(() => {
    assert_equals(writer.desiredSize, 0, 'desiredSize should be 0 after completing 2nd write()');

    writePromiseC = writer.write('c');
    assert_equals(writer.desiredSize, -1, 'desiredSize should be -1 after 3rd write()');

    dones.c();
    return writePromiseC;
  }).then(() => {
    assert_equals(writer.desiredSize, 0, 'desiredSize should be 0 after completing 3rd write()');
  });
}, 'Correctly governs the value of a WritableStream\'s state property (HWM = 0)');

promise_test(() => {
  const dones = Object.create(null);

  const ws = new WritableStream(
    {
      write(chunk) {
        return new Promise(resolve => {
          dones[chunk] = resolve;
        });
      }
    },
    new CountQueuingStrategy({ highWaterMark: 4 })
  );

  const writer = ws.getWriter();
  let writePromiseB;
  let writePromiseC;
  let writePromiseD;

  return Promise.resolve().then(() => {
    assert_equals(writer.desiredSize, 4, 'desiredSize should be initially 4');

    const writePromiseA = writer.write('a');
    assert_equals(writer.desiredSize, 3, 'desiredSize should be 3 after 1st write()');

    writePromiseB = writer.write('b');
    assert_equals(writer.desiredSize, 2, 'desiredSize should be 2 after 2nd write()');

    writePromiseC = writer.write('c');
    assert_equals(writer.desiredSize, 1, 'desiredSize should be 1 after 3rd write()');

    writePromiseD = writer.write('d');
    assert_equals(writer.desiredSize, 0, 'desiredSize should be 0 after 4th write()');

    writer.write('e');
    assert_equals(writer.desiredSize, -1, 'desiredSize should be -1 after 5th write()');

    writer.write('f');
    assert_equals(writer.desiredSize, -2, 'desiredSize should be -2 after 6th write()');

    writer.write('g');
    assert_equals(writer.desiredSize, -3, 'desiredSize should be -3 after 7th write()');

    dones.a();
    return writePromiseA;
  }).then(() => {
    assert_equals(writer.desiredSize, -2, 'desiredSize should be -2 after completing 1st write()');

    dones.b();
    return writePromiseB;
  }).then(() => {
    assert_equals(writer.desiredSize, -1, 'desiredSize should be -1 after completing 2nd write()');

    dones.c();
    return writePromiseC;
  }).then(() => {
    assert_equals(writer.desiredSize, 0, 'desiredSize should be 0 after completing 3rd write()');

    writer.write('h');
    assert_equals(writer.desiredSize, -1, 'desiredSize should be -1 after 8th write()');

    dones.d();
    return writePromiseD;
  }).then(() => {
    assert_equals(writer.desiredSize, 0, 'desiredSize should be 0 after completing 4th write()');

    writer.write('i');
    assert_equals(writer.desiredSize, -1, 'desiredSize should be -1 after 9th write()');
  });
}, 'Correctly governs the value of a WritableStream\'s state property (HWM = 4)');
