// META: global=window,worker,shadowrealm
// META: script=../resources/test-utils.js
// META: script=../resources/recording-streams.js
'use strict';

function interceptThen() {
  const intercepted = [];
  let callCount = 0;
  Object.prototype.then = function(resolver) {
    if (!this.done) {
      intercepted.push(this.value);
    }
    const retval = Object.create(null);
    retval.done = ++callCount === 3;
    retval.value = callCount;
    resolver(retval);
    if (retval.done) {
      delete Object.prototype.then;
    }
  }
  return intercepted;
}

promise_test(async t => {
  const rs = new ReadableStream({
    start(controller) {
      controller.enqueue('a');
      controller.close();
    }
  });
  const ws = recordingWritableStream();

  const intercepted = interceptThen();
  t.add_cleanup(() => {
    delete Object.prototype.then;
  });

  await rs.pipeTo(ws);
  delete Object.prototype.then;


  assert_array_equals(intercepted, [], 'nothing should have been intercepted');
  assert_array_equals(ws.events, ['write', 'a', 'close'], 'written chunk should be "a"');
}, 'piping should not be observable');

promise_test(async t => {
  const rs = new ReadableStream({
    start(controller) {
      controller.enqueue('a');
      controller.close();
    }
  });
  const ws = recordingWritableStream();

  const [ branch1, branch2 ] = rs.tee();

  const intercepted = interceptThen();
  t.add_cleanup(() => {
    delete Object.prototype.then;
  });

  await branch1.pipeTo(ws);
  delete Object.prototype.then;
  branch2.cancel();

  assert_array_equals(intercepted, [], 'nothing should have been intercepted');
  assert_array_equals(ws.events, ['write', 'a', 'close'], 'written chunk should be "a"');
}, 'tee should not be observable');
