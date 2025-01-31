// Flags: --no-warnings
'use strict';
require('../common');
const assert = require('assert').strict;

process.env.NODE_DISABLE_COLORS = true;

assert.throws(
  () => {
    assert.deepStrictEqual({}, { foo: 'bar' });
  },
  {
    message: 'Expected values to be strictly deep-equal:\n' +
      '+ actual - expected\n' +
      '\n' +
      '+ {}\n' +
      '- {\n' +
      '-   foo: \'bar\'\n' +
      '- }\n',
  });

{
  assert.throws(
    () => {
      assert.partialDeepStrictEqual({}, { foo: 'bar' });
    },
    {
      message: 'Expected values to be partially and strictly deep-equal:\n' +
        '+ actual - expected\n' +
        '\n' +
        '+ {}\n' +
        '- {\n' +
        "-   foo: 'bar'\n" +
        '- }\n',
    });
}
