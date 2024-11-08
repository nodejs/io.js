'use strict';
require('../common');
const assert = require('assert');
const { test } = require('node:test');

const defaultStartMessage = 'Expected values to be strictly deep-equal:\n' +
  '+ actual - expected\n' +
  '\n';

test('Handle error causes', () => {
  assert.deepStrictEqual(new Error('a', { cause: new Error('x') }), new Error('a', { cause: new Error('x') }));
  assert.deepStrictEqual(
    new Error('a', { cause: new RangeError('x') }),
    new Error('a', { cause: new RangeError('x') }),
  );

  assert.throws(() => {
    assert.deepStrictEqual(new Error('a', { cause: new Error('x') }), new Error('a', { cause: new Error('y') }));
  }, { message: defaultStartMessage + '  [Error: a] {\n' +
    '+   [cause]: [Error: x]\n' +
    '-   [cause]: [Error: y]\n' +
    '  }\n' });

  assert.throws(() => {
    assert.deepStrictEqual(new Error('a', { cause: new Error('x') }), new Error('a', { cause: new TypeError('x') }));
  }, { message: defaultStartMessage + '  [Error: a] {\n' +
    '+   [cause]: [Error: x]\n' +
    '-   [cause]: [TypeError: x]\n' +
    '  }\n' });

  assert.throws(() => {
    assert.deepStrictEqual(new Error('a'), new Error('a', { cause: new Error('y') }));
  }, { message: defaultStartMessage + '+ [Error: a]\n' +
    '- [Error: a] {\n' +
    '-   [cause]: [Error: y]\n' +
    '- }\n' });

  assert.throws(() => {
    assert.deepStrictEqual(new Error('a'), new Error('a', { cause: { prop: 'value' } }));
  }, { message: defaultStartMessage + '+ [Error: a]\n' +
    '- [Error: a] {\n' +
    '-   [cause]: {\n' +
    '-     prop: \'value\'\n' +
    '-   }\n' +
    '- }\n' });

  assert.notDeepStrictEqual(new Error('a', { cause: new Error('x') }), new Error('a', { cause: new Error('y') }));
  assert.notDeepStrictEqual(
    new Error('a', { cause: { prop: 'value' } }),
    new Error('a', { cause: { prop: 'a different value' } })
  );
});

test('Handle undefined causes', () => {
  assert.deepStrictEqual(new Error('a', { cause: undefined }), new Error('a', { cause: undefined }));

  assert.notDeepStrictEqual(new Error('a', { cause: 'undefined' }), new Error('a', { cause: undefined }));
  assert.notDeepStrictEqual(new Error('a', { cause: undefined }), new Error('a'));
  assert.notDeepStrictEqual(new Error('a'), new Error('a', { cause: undefined }));

  assert.throws(() => {
    assert.deepStrictEqual(new Error('a'), new Error('a', { cause: undefined }));
  }, { message: defaultStartMessage +
    '+ [Error: a]\n' +
    '- [Error: a] {\n' +
    '-   [cause]: undefined\n' +
    '- }\n' });

  assert.throws(() => {
    assert.deepStrictEqual(new Error('a', { cause: undefined }), new Error('a'));
  }, { message: defaultStartMessage +
    '+ [Error: a] {\n' +
    '+   [cause]: undefined\n' +
    '+ }\n' +
    '- [Error: a]\n' });
  assert.throws(() => {
    assert.deepStrictEqual(new Error('a', { cause: undefined }), new Error('a', { cause: 'undefined' }));
  }, { message: defaultStartMessage + '  [Error: a] {\n' +
    '+   [cause]: undefined\n' +
    '-   [cause]: \'undefined\'\n' +
    '  }\n' });
});
