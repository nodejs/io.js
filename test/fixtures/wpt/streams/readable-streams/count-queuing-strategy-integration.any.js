// META: global=window,worker,shadowrealm
'use strict';

test(() => {

  new ReadableStream({}, new CountQueuingStrategy({ highWaterMark: 4 }));

}, 'Can construct a readable stream with a valid CountQueuingStrategy');

promise_test(() => {

  let controller;
  const rs = new ReadableStream(
    {
      start(c) {
        controller = c;
      }
    },
    new CountQueuingStrategy({ highWaterMark: 0 })
  );
  const reader = rs.getReader();

  assert_equals(controller.desiredSize, 0, '0 reads, 0 enqueues: desiredSize should be 0');
  controller.enqueue('a');
  assert_equals(controller.desiredSize, -1, '0 reads, 1 enqueue: desiredSize should be -1');
  controller.enqueue('b');
  assert_equals(controller.desiredSize, -2, '0 reads, 2 enqueues: desiredSize should be -2');
  controller.enqueue('c');
  assert_equals(controller.desiredSize, -3, '0 reads, 3 enqueues: desiredSize should be -3');
  controller.enqueue('d');
  assert_equals(controller.desiredSize, -4, '0 reads, 4 enqueues: desiredSize should be -4');

  return reader.read()
    .then(result => {
      assert_object_equals(result, { value: 'a', done: false },
                           '1st read gives back the 1st chunk enqueued (queue now contains 3 chunks)');
      return reader.read();
    })
    .then(result => {
      assert_object_equals(result, { value: 'b', done: false },
                           '2nd read gives back the 2nd chunk enqueued (queue now contains 2 chunks)');
      return reader.read();
    })
    .then(result => {
      assert_object_equals(result, { value: 'c', done: false },
                           '3rd read gives back the 3rd chunk enqueued (queue now contains 1 chunk)');

      assert_equals(controller.desiredSize, -1, '3 reads, 4 enqueues: desiredSize should be -1');
      controller.enqueue('e');
      assert_equals(controller.desiredSize, -2, '3 reads, 5 enqueues: desiredSize should be -2');

      return reader.read();
    })
    .then(result => {
      assert_object_equals(result, { value: 'd', done: false },
                           '4th read gives back the 4th chunk enqueued (queue now contains 1 chunks)');
      return reader.read();

    }).then(result => {
      assert_object_equals(result, { value: 'e', done: false },
                           '5th read gives back the 5th chunk enqueued (queue now contains 0 chunks)');

      assert_equals(controller.desiredSize, 0, '5 reads, 5 enqueues: desiredSize should be 0');
      controller.enqueue('f');
      assert_equals(controller.desiredSize, -1, '5 reads, 6 enqueues: desiredSize should be -1');
      controller.enqueue('g');
      assert_equals(controller.desiredSize, -2, '5 reads, 7 enqueues: desiredSize should be -2');
    });

}, 'Correctly governs a ReadableStreamController\'s desiredSize property (HWM = 0)');

promise_test(() => {

  let controller;
  const rs = new ReadableStream(
    {
      start(c) {
        controller = c;
      }
    },
    new CountQueuingStrategy({ highWaterMark: 1 })
  );
  const reader = rs.getReader();

  assert_equals(controller.desiredSize, 1, '0 reads, 0 enqueues: desiredSize should be 1');
  controller.enqueue('a');
  assert_equals(controller.desiredSize, 0, '0 reads, 1 enqueue: desiredSize should be 0');
  controller.enqueue('b');
  assert_equals(controller.desiredSize, -1, '0 reads, 2 enqueues: desiredSize should be -1');
  controller.enqueue('c');
  assert_equals(controller.desiredSize, -2, '0 reads, 3 enqueues: desiredSize should be -2');
  controller.enqueue('d');
  assert_equals(controller.desiredSize, -3, '0 reads, 4 enqueues: desiredSize should be -3');

  return reader.read()
    .then(result => {
      assert_object_equals(result, { value: 'a', done: false },
                           '1st read gives back the 1st chunk enqueued (queue now contains 3 chunks)');
      return reader.read();
    })
    .then(result => {
      assert_object_equals(result, { value: 'b', done: false },
                           '2nd read gives back the 2nd chunk enqueued (queue now contains 2 chunks)');
      return reader.read();
    })
    .then(result => {
      assert_object_equals(result, { value: 'c', done: false },
                           '3rd read gives back the 3rd chunk enqueued (queue now contains 1 chunk)');

      assert_equals(controller.desiredSize, 0, '3 reads, 4 enqueues: desiredSize should be 0');
      controller.enqueue('e');
      assert_equals(controller.desiredSize, -1, '3 reads, 5 enqueues: desiredSize should be -1');

      return reader.read();
    })
    .then(result => {
      assert_object_equals(result, { value: 'd', done: false },
                           '4th read gives back the 4th chunk enqueued (queue now contains 1 chunks)');
      return reader.read();
    })
    .then(result => {
      assert_object_equals(result, { value: 'e', done: false },
                           '5th read gives back the 5th chunk enqueued (queue now contains 0 chunks)');

      assert_equals(controller.desiredSize, 1, '5 reads, 5 enqueues: desiredSize should be 1');
      controller.enqueue('f');
      assert_equals(controller.desiredSize, 0, '5 reads, 6 enqueues: desiredSize should be 0');
      controller.enqueue('g');
      assert_equals(controller.desiredSize, -1, '5 reads, 7 enqueues: desiredSize should be -1');
    });

}, 'Correctly governs a ReadableStreamController\'s desiredSize property (HWM = 1)');

promise_test(() => {

  let controller;
  const rs = new ReadableStream(
    {
      start(c) {
        controller = c;
      }
    },
    new CountQueuingStrategy({ highWaterMark: 4 })
  );
  const reader = rs.getReader();

  assert_equals(controller.desiredSize, 4, '0 reads, 0 enqueues: desiredSize should be 4');
  controller.enqueue('a');
  assert_equals(controller.desiredSize, 3, '0 reads, 1 enqueue: desiredSize should be 3');
  controller.enqueue('b');
  assert_equals(controller.desiredSize, 2, '0 reads, 2 enqueues: desiredSize should be 2');
  controller.enqueue('c');
  assert_equals(controller.desiredSize, 1, '0 reads, 3 enqueues: desiredSize should be 1');
  controller.enqueue('d');
  assert_equals(controller.desiredSize, 0, '0 reads, 4 enqueues: desiredSize should be 0');
  controller.enqueue('e');
  assert_equals(controller.desiredSize, -1, '0 reads, 5 enqueues: desiredSize should be -1');
  controller.enqueue('f');
  assert_equals(controller.desiredSize, -2, '0 reads, 6 enqueues: desiredSize should be -2');


  return reader.read()
    .then(result => {
      assert_object_equals(result, { value: 'a', done: false },
                           '1st read gives back the 1st chunk enqueued (queue now contains 5 chunks)');
      return reader.read();
    })
    .then(result => {
      assert_object_equals(result, { value: 'b', done: false },
                           '2nd read gives back the 2nd chunk enqueued (queue now contains 4 chunks)');

      assert_equals(controller.desiredSize, 0, '2 reads, 6 enqueues: desiredSize should be 0');
      controller.enqueue('g');
      assert_equals(controller.desiredSize, -1, '2 reads, 7 enqueues: desiredSize should be -1');

      return reader.read();
    })
    .then(result => {
      assert_object_equals(result, { value: 'c', done: false },
                           '3rd read gives back the 3rd chunk enqueued (queue now contains 4 chunks)');
      return reader.read();
    })
    .then(result => {
      assert_object_equals(result, { value: 'd', done: false },
                           '4th read gives back the 4th chunk enqueued (queue now contains 3 chunks)');
      return reader.read();
    })
    .then(result => {
      assert_object_equals(result, { value: 'e', done: false },
                           '5th read gives back the 5th chunk enqueued (queue now contains 2 chunks)');
      return reader.read();
    })
    .then(result => {
      assert_object_equals(result, { value: 'f', done: false },
                           '6th read gives back the 6th chunk enqueued (queue now contains 0 chunks)');

      assert_equals(controller.desiredSize, 3, '6 reads, 7 enqueues: desiredSize should be 3');
      controller.enqueue('h');
      assert_equals(controller.desiredSize, 2, '6 reads, 8 enqueues: desiredSize should be 2');
      controller.enqueue('i');
      assert_equals(controller.desiredSize, 1, '6 reads, 9 enqueues: desiredSize should be 1');
      controller.enqueue('j');
      assert_equals(controller.desiredSize, 0, '6 reads, 10 enqueues: desiredSize should be 0');
      controller.enqueue('k');
      assert_equals(controller.desiredSize, -1, '6 reads, 11 enqueues: desiredSize should be -1');
    });

}, 'Correctly governs a ReadableStreamController\'s desiredSize property (HWM = 4)');
