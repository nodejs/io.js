// Flags: --expose-internals
'use strict';

require('../common');
const assert = require('assert');
const { E, SystemError, codes, kIsNodeError } = require('internal/errors');
const { inspect } = require('internal/util/inspect');

assert.throws(
  () => { new SystemError(); },
  {
    name: 'TypeError',
    message: "Cannot read properties of undefined (reading 'syscall')",
  }
);

E('ERR_TEST', 'custom message', SystemError);
const { ERR_TEST } = codes;

{
  const ctx = {
    code: 'ETEST',
    message: 'code message',
    syscall: 'syscall_test',
    path: '/str',
    dest: '/str2'
  };

  assert.throws(
    () => { throw new ERR_TEST(ctx); },
    {
      code: 'ERR_TEST',
      name: 'SystemError',
      message: 'custom message: syscall_test returned ETEST (code message)' +
        ' /str => /str2',
      info: ctx
    }
  );
}

{
  const ctx = {
    code: 'ETEST',
    message: 'code message',
    syscall: 'syscall_test',
    path: Buffer.from('/buf'),
    dest: '/str2'
  };
  assert.throws(
    () => { throw new ERR_TEST(ctx); },
    {
      code: 'ERR_TEST',
      name: 'SystemError',
      message: 'custom message: syscall_test returned ETEST (code message)' +
        ' /buf => /str2',
      info: ctx
    }
  );
}

{
  const ctx = {
    code: 'ETEST',
    message: 'code message',
    syscall: 'syscall_test',
    path: Buffer.from('/buf'),
    dest: Buffer.from('/buf2')
  };
  assert.throws(
    () => { throw new ERR_TEST(ctx); },
    {
      code: 'ERR_TEST',
      name: 'SystemError',
      message: 'custom message: syscall_test returned ETEST (code message)' +
        ' /buf => /buf2',
      info: ctx
    }
  );
}

{
  const ctx = {
    code: 'ERR',
    errno: 123,
    message: 'something happened',
    syscall: 'syscall_test',
    path: Buffer.from('a'),
    dest: Buffer.from('b')
  };
  const err = new ERR_TEST(ctx);
  assert.strictEqual(err.info, ctx);
  assert.strictEqual(err.code, 'ERR_TEST');
  err.code = 'test';
  assert.strictEqual(err.code, 'test');

  // Test legacy properties. These shouldn't be used anymore
  // but let us make sure they still work
  assert.strictEqual(err.errno, 123);
  assert.strictEqual(err.syscall, 'syscall_test');
  assert.strictEqual(err.path, 'a');
  assert.strictEqual(err.dest, 'b');

  // Make sure it's mutable
  err.code = 'test';
  err.errno = 321;
  err.syscall = 'test';
  err.path = 'path';
  err.dest = 'path';

  assert.strictEqual(err.errno, 321);
  assert.strictEqual(err.syscall, 'test');
  assert.strictEqual(err.path, 'path');
  assert.strictEqual(err.dest, 'path');

  assert.strictEqual(err.info.errno, 321);
  assert.strictEqual(err.info.dest.toString(), 'path');
  assert.strictEqual(err.info.path.toString(), 'path');
  assert.strictEqual(err.info.syscall, 'test');
}

{
  const ctx = {
    code: 'ERR_TEST',
    message: 'Error occurred',
    syscall: 'syscall_test'
  };
  assert.throws(
    () => {
      const err = new ERR_TEST(ctx);
      err.name = 'Foobar';
      throw err;
    },
    {
      code: 'ERR_TEST',
      name: 'Foobar',
      message: 'custom message: syscall_test returned ERR_TEST ' +
        '(Error occurred)',
      info: ctx
    }
  );
}

{
  const ctx = {
    code: 'ERR',
    errno: 123,
    message: 'something happened',
    syscall: 'syscall_test',
  };
  const err = new ERR_TEST(ctx);

  // is set to true
  assert.strictEqual(err[kIsNodeError], true);

  // is not writable
  assert.throws(
    () => { err[kIsNodeError] = false; },
    {
      name: 'TypeError',
      message: /Symbol\(kIsNodeError\)/,
    }
  );

  // is not enumerable
  assert.strictEqual(Object.prototype.propertyIsEnumerable.call(err, kIsNodeError), false);

  // is configurable
  delete err[kIsNodeError];
  assert.strictEqual(kIsNodeError in err, false);
}

{
  const ctx = {
    code: 'ERR',
    errno: 123,
    message: 'something happened',
    syscall: 'syscall_test',
  };
  const err = new ERR_TEST(ctx);

  // is set to true
  assert.strictEqual(err.name, 'SystemError');

  // is writable
  err.name = 'CustomError';
  assert.strictEqual(err.name, 'CustomError');

  // is not enumerable
  assert.strictEqual(Object.prototype.propertyIsEnumerable.call(err, 'name'), false);

  // is configurable
  delete err.name;
  assert.strictEqual(err.name, 'Error');
}

{
  const ctx = {
    code: 'ERR',
    errno: 123,
    message: 'something happened',
    syscall: 'syscall_test',
  };
  const err = new ERR_TEST(ctx);

  // Is set with the correct message
  assert.strictEqual(err.message, 'custom message: syscall_test returned ERR (something happened)');

  // is writable
  err.message = 'custom message';
  assert.strictEqual(err.message, 'custom message');

  // is not enumerable
  assert.strictEqual(Object.prototype.propertyIsEnumerable.call(err, 'message'), false);

  // is configurable
  delete err.message;
  assert.strictEqual(err.message, '');
}

{
  const ctx = {
    code: 'ERR',
    errno: 123,
    message: 'something happened',
    syscall: 'syscall_test',
  };
  const err = new ERR_TEST(ctx);

  // Is set to the correct syscall
  assert.strictEqual(err.syscall, 'syscall_test');

  // is writable
  err.syscall = 'custom syscall';
  assert.strictEqual(err.syscall, 'custom syscall');

  // is enumerable
  assert(Object.prototype.propertyIsEnumerable.call(err, 'syscall'));

  // is configurable
  delete err.syscall;
  assert.strictEqual('syscall' in err, false);
}

{
  const ctx = {
    code: 'ERR',
    errno: 123,
    message: 'something happened',
    syscall: 'syscall_test',
  };
  const err = new ERR_TEST(ctx);

  // Is set to the correct errno
  assert.strictEqual(err.errno, 123);

  // is writable
  err.errno = 'custom errno';
  assert.strictEqual(err.errno, 'custom errno');

  // is enumerable
  assert(Object.prototype.propertyIsEnumerable.call(err, 'errno'));

  // is configurable
  delete err.errno;
  assert.strictEqual('errno' in err, false);
}

{
  const ctx = {
    code: 'ERR',
    errno: 123,
    message: 'something happened',
    syscall: 'syscall_test',
  };
  const err = new ERR_TEST(ctx);

  // Is set to the correct info
  assert.strictEqual(Object.keys(err.info).length, 4);
  assert.strictEqual(err.info.errno, 123);
  assert.strictEqual(err.info.code, 'ERR');
  assert.strictEqual(err.info.message, 'something happened');
  assert.strictEqual(err.info.syscall, 'syscall_test');

  // is not writable
  assert.throws(
    () => {
      err.info = {
        ...ctx,
        errno: 124
      };
    },
    {
      name: 'TypeError',
      message: /'info'/,
    }
  );

  assert.strictEqual(Object.keys(err.info).length, 4);
  assert.strictEqual(err.info.errno, 123);
  assert.strictEqual(err.info.code, 'ERR');
  assert.strictEqual(err.info.message, 'something happened');
  assert.strictEqual(err.info.syscall, 'syscall_test');

  // is enumerable
  assert(Object.prototype.propertyIsEnumerable.call(err, 'info'));

  // is configurable
  delete err.info;

  assert.strictEqual('info' in err, false);
}

{
  // Make sure the stack trace does not contain SystemError
  try {
    throw new ERR_TEST({
      code: 'ERR',
      errno: 123,
      message: 'something happened',
      syscall: 'syscall_test',
    });
  } catch (e) {
    assert.doesNotMatch(e.stack, /at new SystemError/);
    assert.match(e.stack.split('\n')[1], /test-errors-systemerror\.js/);
  }
}

{
  // Make sure the stack trace has the correct number of frames
  const limit = Error.stackTraceLimit;
  Error.stackTraceLimit = 3;
  function a() {
    b();
  }

  function b() {
    c();
  }

  function c() {
    throw new ERR_TEST({
      code: 'ERR',
      errno: 123,
      message: 'something happened',
      syscall: 'syscall_test',
    });
  }
  try {
    a();
  } catch (e) {
    assert.doesNotMatch(e.stack, /at new SystemError/);
    assert.match(e.stack.split('\n')[1], /test-errors-systemerror\.js/);
    assert.match(e.stack.split('\n')[1], /at c \(/);
    assert.match(e.stack.split('\n')[2], /test-errors-systemerror\.js/);
    assert.match(e.stack.split('\n')[2], /at b \(/);
    assert.match(e.stack.split('\n')[3], /test-errors-systemerror\.js/);
    assert.match(e.stack.split('\n')[3], /at a \(/);
    assert.strictEqual(e.stack.split('\n').length, 4);
  } finally {
    Error.stackTraceLimit = limit;
  }
}

{
  // Inspect should return the correct string
  const err = new ERR_TEST({
    code: 'ERR',
    errno: 123,
    message: 'something happened',
    syscall: 'syscall_test',
    custom: 'custom'
  });
  let inspectedErr = inspect(err);

  assert.ok(inspectedErr.includes(`info: {
    code: 'ERR',
    errno: 123,
    message: 'something happened',
    syscall: 'syscall_test',
    custom: 'custom'
  },`));

  err.syscall = 'custom_syscall';

  inspectedErr = inspect(err);

  assert.ok(inspectedErr.includes(`info: {
    code: 'ERR',
    errno: 123,
    message: 'something happened',
    syscall: 'custom_syscall',
    custom: 'custom'
  },`));
}

{
  // toString should return the correct string

  const err = new ERR_TEST({
    code: 'ERR',
    errno: 123,
    message: 'something happened',
    syscall: 'syscall_test',
  });

  assert.strictEqual(
    err.toString(),
    'SystemError [ERR_TEST]: custom message: syscall_test returned ERR (something happened)'
  );
}
