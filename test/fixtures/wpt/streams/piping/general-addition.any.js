// META: global=window,worker,shadowrealm
'use strict';

promise_test(async t => {
  /** @type {ReadableStreamDefaultController} */
  var con;
  let synchronous = false;
  new ReadableStream({ start(c) { con = c }}, { highWaterMark: 0 }).pipeTo(
    new WritableStream({ write() { synchronous = true; } })
  )
  // wait until start algorithm finishes
  await Promise.resolve();
  con.enqueue();
  assert_false(synchronous, 'write algorithm must not run synchronously');
}, "enqueue() must not synchronously call write algorithm");
