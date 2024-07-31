'use strict';
const common = require('../common');

// This test checks that the semantics of `util.callbackify` are as described in
// the API docs

const assert = require('assert');
const { callbackify } = require('util');
const { execFile } = require('child_process');
const fixtures = require('../common/fixtures');

const values = [
  'hello world',
  null,
  undefined,
  false,
  0,
  {},
  { key: 'value' },
  Symbol('I am a symbol'),
  function ok() {},
  ['array', 'with', 4, 'values'],
  new Error('boo'),
];

{
  // Test that the resolution value is passed as second argument to callback
  for (const value of values) {
    // Test and `async function`
    async function asyncFn() {
      return value;
    }

    const cbAsyncFn = callbackify(asyncFn);
    cbAsyncFn(common.mustSucceed((ret) => {
      assert.strictEqual(ret, value);
    }));

    // Test Promise factory
    function promiseFn() {
      return Promise.resolve(value);
    }

    const cbPromiseFn = callbackify(promiseFn);
    cbPromiseFn(common.mustSucceed((ret) => {
      assert.strictEqual(ret, value);
    }));

    // Test Thenable
    function thenableFn() {
      return {
        then(onRes, onRej) {
          onRes(value);
        }
      };
    }

    const cbThenableFn = callbackify(thenableFn);
    cbThenableFn(common.mustSucceed((ret) => {
      assert.strictEqual(ret, value);
    }));
  }
}

{
  // Test that rejection reason is passed as first argument to callback
  for (const value of values) {
    // Test an `async function`
    async function asyncFn() {
      return Promise.reject(value);
    }

    const cbAsyncFn = callbackify(asyncFn);
    assert.strictEqual(cbAsyncFn.length, 1);
    assert.strictEqual(cbAsyncFn.name, 'asyncFnCallbackified');
    cbAsyncFn(common.mustCall((err, ret) => {
      assert.strictEqual(ret, undefined);
      if (err instanceof Error) {
        if ('reason' in err) {
          assert(!value);
          assert.strictEqual(err.code, 'ERR_FALSY_VALUE_REJECTION');
          assert.strictEqual(err.reason, value);
        } else {
          assert.strictEqual(String(value).endsWith(err.message), true);
        }
      } else {
        assert.strictEqual(err, value);
      }
    }));

    // Test a Promise factory
    function promiseFn() {
      return Promise.reject(value);
    }
    const obj = {};
    Object.defineProperty(promiseFn, 'name', {
      value: obj,
      writable: false,
      enumerable: false,
      configurable: true
    });

    const cbPromiseFn = callbackify(promiseFn);
    assert.strictEqual(promiseFn.name, obj);
    cbPromiseFn(common.mustCall((err, ret) => {
      assert.strictEqual(ret, undefined);
      if (err instanceof Error) {
        if ('reason' in err) {
          assert(!value);
          assert.strictEqual(err.code, 'ERR_FALSY_VALUE_REJECTION');
          assert.strictEqual(err.reason, value);
        } else {
          assert.strictEqual(String(value).endsWith(err.message), true);
        }
      } else {
        assert.strictEqual(err, value);
      }
    }));

    // Test Thenable
    function thenableFn() {
      return {
        then(onRes, onRej) {
          onRej(value);
        }
      };
    }

    const cbThenableFn = callbackify(thenableFn);
    cbThenableFn(common.mustCall((err, ret) => {
      assert.strictEqual(ret, undefined);
      if (err instanceof Error) {
        if ('reason' in err) {
          assert(!value);
          assert.strictEqual(err.code, 'ERR_FALSY_VALUE_REJECTION');
          assert.strictEqual(err.reason, value);
        } else {
          assert.strictEqual(String(value).endsWith(err.message), true);
        }
      } else {
        assert.strictEqual(err, value);
      }
    }));
  }
}

{
  // Test that arguments passed to callbackified function are passed to original
  for (const value of values) {
    async function asyncFn(arg) {
      assert.strictEqual(arg, value);
      return arg;
    }

    const cbAsyncFn = callbackify(asyncFn);
    assert.strictEqual(cbAsyncFn.length, 2);
    assert.notStrictEqual(
      Object.getPrototypeOf(cbAsyncFn),
      Object.getPrototypeOf(asyncFn)
    );
    assert.strictEqual(Object.getPrototypeOf(cbAsyncFn), Function.prototype);
    cbAsyncFn(value, common.mustSucceed((ret) => {
      assert.strictEqual(ret, value);
    }));

    function promiseFn(arg) {
      assert.strictEqual(arg, value);
      return Promise.resolve(arg);
    }
    const obj = {};
    Object.defineProperty(promiseFn, 'length', {
      value: obj,
      writable: false,
      enumerable: false,
      configurable: true
    });

    const cbPromiseFn = callbackify(promiseFn);
    assert.strictEqual(promiseFn.length, obj);
    cbPromiseFn(value, common.mustSucceed((ret) => {
      assert.strictEqual(ret, value);
    }));
  }
}

{
  // Test that `this` binding is the same for callbackified and original
  for (const value of values) {
    const iAmThis = {
      fn(arg) {
        assert.strictEqual(this, iAmThis);
        return Promise.resolve(arg);
      },
    };
    iAmThis.cbFn = callbackify(iAmThis.fn);
    iAmThis.cbFn(value, common.mustSucceed(function(ret) {
      assert.strictEqual(ret, value);
      assert.strictEqual(this, iAmThis);
    }));

    const iAmThat = {
      async fn(arg) {
        assert.strictEqual(this, iAmThat);
        return arg;
      },
    };
    iAmThat.cbFn = callbackify(iAmThat.fn);
    iAmThat.cbFn(value, common.mustSucceed(function(ret) {
      assert.strictEqual(ret, value);
      assert.strictEqual(this, iAmThat);
    }));
  }
}

{
  // Test that callback that throws emits an `uncaughtException` event
  const fixture = fixtures.path('uncaught-exceptions', 'callbackify1.js');
  execFile(
    process.execPath,
    [fixture],
    common.mustCall((err, stdout, stderr) => {
      assert.strictEqual(err.code, 1);
      assert.strictEqual(Object.getPrototypeOf(err).name, 'Error');
      assert.strictEqual(stdout, '');
      const errLines = stderr.trim().split(/[\r\n]+/);
      const errLine = errLines.find((l) => /^Error/.exec(l));
      assert.strictEqual(errLine, `Error: ${fixture}`);
      assert.strictEqual(errLines.length, 7);
    })
  );
}

{
  // Test that handled `uncaughtException` works and passes rejection reason
  const fixture = fixtures.path('uncaught-exceptions', 'callbackify2.js');
  execFile(
    process.execPath,
    [fixture],
    common.mustSucceed((stdout, stderr) => {
      assert.strictEqual(
        stdout.trim(),
        `ifError got unwanted exception: ${fixture}`);
      assert.strictEqual(stderr, '');
    })
  );
}

{
  // Verify that non-function inputs throw.
  ['foo', null, undefined, false, 0, {}, Symbol(), []].forEach((value) => {
    assert.throws(() => {
      callbackify(value);
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError',
      message: 'The "original" argument must be of type function.' +
               common.invalidArgTypeHelper(value)
    });
  });
}

{
  async function asyncFn() {
    return 42;
  }

  const cb = callbackify(asyncFn);
  const args = [];

  // Verify that the last argument to the callbackified function is a function.
  ['foo', null, undefined, false, 0, {}, Symbol(), []].forEach((value) => {
    args.push(value);
    assert.throws(() => {
      cb(...args);
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError',
      message: 'The last argument must be of type function.' +
               common.invalidArgTypeHelper(value)
    });
  });
}

{
  // Test Promise factory
  function promiseFn(value) {
    return Promise.reject(value);
  }

  const cbPromiseFn = callbackify(promiseFn);

  cbPromiseFn(null, (err) => {
    assert.strictEqual(err.message, 'Promise was rejected with falsy value');
    assert.strictEqual(err.code, 'ERR_FALSY_VALUE_REJECTION');
    assert.strictEqual(err.reason, null);
    const stack = err.stack.split(/[\r\n]+/);
    assert.match(stack[1], /at process\.processTicksAndRejections/);
  });
}
