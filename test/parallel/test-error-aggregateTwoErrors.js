// Flags: --expose-internals
'use strict';

require('../common');
const assert = require('assert');
const { aggregateTwoErrors } = require('internal/errors');

assert.strictEqual(aggregateTwoErrors(null, null), null);

{
  const err = new Error();
  assert.strictEqual(aggregateTwoErrors(null, err), err);
}

{
  const err = new Error();
  assert.strictEqual(aggregateTwoErrors(err, null), err);
}

{
  const err0 = new Error('original');
  const err1 = new Error('second error');

  err0.code = 'ERR0';
  err1.code = 'ERR1';

  const chainedError = aggregateTwoErrors(err1, err0);
  assert.strictEqual(chainedError.message, err0.message);
  assert.strictEqual(chainedError.code, err0.code);
  assert.deepStrictEqual(chainedError.errors, [err0, err1]);
}

{
  const err0 = new Error('original');
  const err1 = new Error('second error');
  const err2 = new Error('third error');

  err0.code = 'ERR0';
  err1.code = 'ERR1';
  err2.code = 'ERR2';

  const chainedError = aggregateTwoErrors(err2, aggregateTwoErrors(err1, err0));
  assert.strictEqual(chainedError.message, err0.message);
  assert.strictEqual(chainedError.code, err0.code);
  assert.deepStrictEqual(chainedError.errors, [err0, err1, err2]);
}

{
  const err0 = new Error('original');
  const err1 = new Error('second error');

  err0.code = 'ERR0';
  err1.code = 'ERR1';

  const chainedError = aggregateTwoErrors(null, aggregateTwoErrors(err1, err0));
  assert.strictEqual(chainedError.message, err0.message);
  assert.strictEqual(chainedError.code, err0.code);
  assert.deepStrictEqual(chainedError.errors, [err0, err1]);
}

{
  const err0 = new Error('original');
  const err1 = new Error('second error');

  err0.code = 'ERR0';
  err1.code = 'ERR1';

  const chainedError = aggregateTwoErrors(null, aggregateTwoErrors(err1, err0));
  const stack = chainedError.stack.split('\n');
  assert.match(stack[1], /^ {4}at Object/);
}
