// Flags: --expose-internals
'use strict';

require('../common');
const { hideStackFrames, codes } = require('internal/errors');
const { validateInteger } = require('internal/validators');
const assert = require('assert');

{
  // Test that the a built-in error has the correct name and message.
  function a() {
    b();
  }

  function b() {
    c();
  }

  const c = hideStackFrames(function() {
    throw new Error('test');
  });

  try {
    a();
  } catch (e) {
    assert.strictEqual(e.name, 'Error');
    assert.strictEqual(e.message, 'test');
  }
}

{
  // Test that validator errors have the correct name and message.
  try {
    validateInteger('2', 'test');
  } catch (e) {
    assert.strictEqual(e.name, 'TypeError');
    assert.strictEqual(e.message, 'The "test" argument must be of type number. Received type string (\'2\')');
  }
}

{
  // Test that validator fn is not in the stack trace.

  function a(value) {
    validateInteger(value, 'test');
  }
  try {
    a('2');
  } catch (e) {
    assert.strictEqual(Error.stackTraceLimit, 10);
    const stack = e.stack.split('\n');
    assert.doesNotMatch(stack[1], /validateInteger/);
    assert.match(stack[1], /at a/);
  }
}

{
  // Test that the stack trace is hidden for normal unnamed functions.
  function a() {
    b();
  }

  function b() {
    c();
  }

  const c = hideStackFrames(function() {
    throw new Error('test');
  });

  try {
    a();
  } catch (e) {
    assert.strictEqual(Error.stackTraceLimit, 10);
    const stack = e.stack.split('\n');
    assert.doesNotMatch(stack[1], /at c/);
    assert.match(stack[1], /at b/);
    assert.strictEqual(Error.stackTraceLimit, 10);
  }
}

{
  // Test that the stack trace is hidden for normal functions.
  function a() {
    b();
  }

  function b() {
    c();
  }

  const c = hideStackFrames(function c() {
    throw new Error('test');
  });

  try {
    a();
  } catch (e) {
    assert.strictEqual(Error.stackTraceLimit, 10);
    const stack = e.stack.split('\n');
    assert.doesNotMatch(stack[1], /at c/);
    assert.match(stack[1], /at b/);
    assert.strictEqual(Error.stackTraceLimit, 10);
  }
}

{
  // Test that the stack trace is hidden for arrow functions.
  function a() {
    b();
  }

  function b() {
    c();
  }

  const c = hideStackFrames(() => {
    throw new Error('test');
  });

  try {
    a();
  } catch (e) {
    assert.strictEqual(Error.stackTraceLimit, 10);
    const stack = e.stack.split('\n');
    assert.doesNotMatch(stack[1], /at c/);
    assert.match(stack[1], /at b/);
    assert.strictEqual(Error.stackTraceLimit, 10);
  }
}

{
  // Creating a new Error object without stack trace, then throwing it
  // should get a stack trace by hideStackFrames.
  function a() {
    b();
  }

  function b() {
    c();
  }

  const c = hideStackFrames(function() {
    throw new Error('test');
  });

  try {
    a();
  } catch (e) {
    assert.strictEqual(Error.stackTraceLimit, 10);
    const stack = e.stack.split('\n');
    assert.doesNotMatch(stack[1], /at c/);
    assert.match(stack[1], /at b/);
    assert.strictEqual(Error.stackTraceLimit, 10);
  }
}

{
  const ERR_ACCESS_DENIED = codes.ERR_ACCESS_DENIED;
  // Creating a new Error object without stack trace, then throwing it
  // should get a stack trace by hideStackFrames.
  function a() {
    b();
  }

  function b() {
    c();
  }

  const c = hideStackFrames(function() {
    throw new ERR_ACCESS_DENIED.NoStackError('test');
  });

  try {
    a();
  } catch (e) {
    assert.strictEqual(Error.stackTraceLimit, 10);
    const stack = e.stack.split('\n');
    assert.doesNotMatch(stack[1], /at c/);
    assert.match(stack[1], /at b/);
    assert.strictEqual(Error.stackTraceLimit, 10);
  }
}

{
  // Creating a new Error object with stack trace, then throwing it
  // should get a stack trace by hideStackFrames.
  function a() {
    b();
  }

  const b = hideStackFrames(function b() {
    c();
  });

  const c = hideStackFrames(function() {
    throw new Error('test');
  });

  try {
    a();
  } catch (e) {
    assert.strictEqual(Error.stackTraceLimit, 10);
    const stack = e.stack.split('\n');
    assert.match(stack[1], /at a/);
    assert.strictEqual(Error.stackTraceLimit, 10);
  }
}

{
  // Binding passes the value of this to the wrapped function.
  let called = false;
  function a() {
    b.bind({ key: 'value' })();
  }

  const b = hideStackFrames(function b() {
    assert.strictEqual(this.key, 'value');
    called = true;
  });

  a();

  assert.strictEqual(called, true);
}

{
  // Binding passes the value of this to the withoutStackTrace function.
  let called = false;
  function a() {
    b.withoutStackTrace.bind({ key: 'value' })();
  }

  const b = hideStackFrames(function b() {
    assert.strictEqual(this.key, 'value');
    called = true;
  });

  a();

  assert.strictEqual(called, true);
}
