import '../common/index.mjs';
import assert from 'node:assert';
import { test } from 'node:test';
import { parseArgs } from 'node:util';

test('when short option used as flag then stored as flag', () => {
  const args = ['-f'];
  const expected = { values: { __proto__: null, f: true }, positionals: [] };
  const result = parseArgs({ strict: false, args });
  assert.deepStrictEqual(result, expected);
});

test('when short option used as flag before positional then stored as flag and positional (and not value)', () => {
  const args = ['-f', 'bar'];
  const expected = { values: { __proto__: null, f: true }, positionals: [ 'bar' ] };
  const result = parseArgs({ strict: false, args });
  assert.deepStrictEqual(result, expected);
});

test('when short option `type: "string"` used with value then stored as value', () => {
  const args = ['-f', 'bar'];
  const options = { f: { type: 'string' } };
  const expected = { values: { __proto__: null, f: 'bar' }, positionals: [] };
  const result = parseArgs({ args, options });
  assert.deepStrictEqual(result, expected);
});

test('when short option listed in short used as flag then long option stored as flag', () => {
  const args = ['-f'];
  const options = { foo: { short: 'f', type: 'boolean' } };
  const expected = { values: { __proto__: null, foo: true }, positionals: [] };
  const result = parseArgs({ args, options });
  assert.deepStrictEqual(result, expected);
});

test('when short option listed in short and long listed in `type: "string"` and ' +
     'used with value then long option stored as value', () => {
  const args = ['-f', 'bar'];
  const options = { foo: { short: 'f', type: 'string' } };
  const expected = { values: { __proto__: null, foo: 'bar' }, positionals: [] };
  const result = parseArgs({ args, options });
  assert.deepStrictEqual(result, expected);
});

test('when short option `type: "string"` used without value then stored as flag', () => {
  const args = ['-f'];
  const options = { f: { type: 'string' } };
  const expected = { values: { __proto__: null, f: true }, positionals: [] };
  const result = parseArgs({ strict: false, args, options });
  assert.deepStrictEqual(result, expected);
});

test('short option group behaves like multiple short options', () => {
  const args = ['-rf'];
  const options = { };
  const expected = { values: { __proto__: null, r: true, f: true }, positionals: [] };
  const result = parseArgs({ strict: false, args, options });
  assert.deepStrictEqual(result, expected);
});

test('short option group does not consume subsequent positional', () => {
  const args = ['-rf', 'foo'];
  const options = { };
  const expected = { values: { __proto__: null, r: true, f: true }, positionals: ['foo'] };
  const result = parseArgs({ strict: false, args, options });
  assert.deepStrictEqual(result, expected);
});

// See: Guideline 5 https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap12.html
test('if terminal of short-option group configured `type: "string"`, subsequent positional is stored', () => {
  const args = ['-rvf', 'foo'];
  const options = { f: { type: 'string' } };
  const expected = { values: { __proto__: null, r: true, v: true, f: 'foo' }, positionals: [] };
  const result = parseArgs({ strict: false, args, options });
  assert.deepStrictEqual(result, expected);
});

test('handles short-option groups in conjunction with long-options', () => {
  const args = ['-rf', '--foo', 'foo'];
  const options = { foo: { type: 'string' } };
  const expected = { values: { __proto__: null, r: true, f: true, foo: 'foo' }, positionals: [] };
  const result = parseArgs({ strict: false, args, options });
  assert.deepStrictEqual(result, expected);
});

test('handles short-option groups with "short" alias configured', () => {
  const args = ['-rf'];
  const options = { remove: { short: 'r', type: 'boolean' } };
  const expected = { values: { __proto__: null, remove: true, f: true }, positionals: [] };
  const result = parseArgs({ strict: false, args, options });
  assert.deepStrictEqual(result, expected);
});

test('handles short-option followed by its value', () => {
  const args = ['-fFILE'];
  const options = { foo: { short: 'f', type: 'string' } };
  const expected = { values: { __proto__: null, foo: 'FILE' }, positionals: [] };
  const result = parseArgs({ strict: false, args, options });
  assert.deepStrictEqual(result, expected);
});

test('Everything after a bare `--` is considered a positional argument', () => {
  const args = ['--', 'barepositionals', 'mopositionals'];
  const expected = { values: { __proto__: null }, positionals: ['barepositionals', 'mopositionals'] };
  const result = parseArgs({ allowPositionals: true, args });
  assert.deepStrictEqual(result, expected, Error('testing bare positionals'));
});

test('args are true', () => {
  const args = ['--foo', '--bar'];
  const expected = { values: { __proto__: null, foo: true, bar: true }, positionals: [] };
  const result = parseArgs({ strict: false, args });
  assert.deepStrictEqual(result, expected, Error('args are true'));
});

test('arg is true and positional is identified', () => {
  const args = ['--foo=a', '--foo', 'b'];
  const expected = { values: { __proto__: null, foo: true }, positionals: ['b'] };
  const result = parseArgs({ strict: false, args });
  assert.deepStrictEqual(result, expected, Error('arg is true and positional is identified'));
});

test('args equals are passed `type: "string"`', () => {
  const args = ['--so=wat'];
  const options = { so: { type: 'string' } };
  const expected = { values: { __proto__: null, so: 'wat' }, positionals: [] };
  const result = parseArgs({ args, options });
  assert.deepStrictEqual(result, expected, Error('arg value is passed'));
});

test('when args include single dash then result stores dash as positional', () => {
  const args = ['-'];
  const expected = { values: { __proto__: null }, positionals: ['-'] };
  const result = parseArgs({ allowPositionals: true, args });
  assert.deepStrictEqual(result, expected);
});

test('zero config args equals are parsed as if `type: "string"`', () => {
  const args = ['--so=wat'];
  const options = { };
  const expected = { values: { __proto__: null, so: 'wat' }, positionals: [] };
  const result = parseArgs({ strict: false, args, options });
  assert.deepStrictEqual(result, expected, Error('arg value is passed'));
});

test('same arg is passed twice `type: "string"` and last value is recorded', () => {
  const args = ['--foo=a', '--foo', 'b'];
  const options = { foo: { type: 'string' } };
  const expected = { values: { __proto__: null, foo: 'b' }, positionals: [] };
  const result = parseArgs({ args, options });
  assert.deepStrictEqual(result, expected, Error('last arg value is passed'));
});

test('args equals pass string including more equals', () => {
  const args = ['--so=wat=bing'];
  const options = { so: { type: 'string' } };
  const expected = { values: { __proto__: null, so: 'wat=bing' }, positionals: [] };
  const result = parseArgs({ args, options });
  assert.deepStrictEqual(result, expected, Error('arg value is passed'));
});

test('first arg passed for `type: "string"` and "multiple" is in array', () => {
  const args = ['--foo=a'];
  const options = { foo: { type: 'string', multiple: true } };
  const expected = { values: { __proto__: null, foo: ['a'] }, positionals: [] };
  const result = parseArgs({ args, options });
  assert.deepStrictEqual(result, expected, Error('first multiple in array'));
});

test('args are passed `type: "string"` and "multiple"', () => {
  const args = ['--foo=a', '--foo', 'b'];
  const options = {
    foo: {
      type: 'string',
      multiple: true,
    },
  };
  const expected = { values: { __proto__: null, foo: ['a', 'b'] }, positionals: [] };
  const result = parseArgs({ args, options });
  assert.deepStrictEqual(result, expected, Error('both arg values are passed'));
});

test('when expecting `multiple:true` boolean option and option used multiple times then result includes array of ' +
     'booleans matching usage', () => {
  const args = ['--foo', '--foo'];
  const options = {
    foo: {
      type: 'boolean',
      multiple: true,
    },
  };
  const expected = { values: { __proto__: null, foo: [true, true] }, positionals: [] };
  const result = parseArgs({ args, options });
  assert.deepStrictEqual(result, expected);
});

test('order of option and positional does not matter (per README)', () => {
  const args1 = ['--foo=bar', 'baz'];
  const args2 = ['baz', '--foo=bar'];
  const options = { foo: { type: 'string' } };
  const expected = { values: { __proto__: null, foo: 'bar' }, positionals: ['baz'] };
  assert.deepStrictEqual(
    parseArgs({ allowPositionals: true, args: args1, options }),
    expected,
    Error('option then positional')
  );
  assert.deepStrictEqual(
    parseArgs({ allowPositionals: true, args: args2, options }),
    expected,
    Error('positional then option')
  );
});

test('correct default args when use node -p', () => {
  const holdArgv = process.argv;
  process.argv = [process.argv0, '--foo'];
  const holdExecArgv = process.execArgv;
  process.execArgv = ['-p', '0'];
  const result = parseArgs({ strict: false });

  const expected = { values: { __proto__: null, foo: true },
                     positionals: [] };
  assert.deepStrictEqual(result, expected);
  process.argv = holdArgv;
  process.execArgv = holdExecArgv;
});

test('correct default args when use node --print', () => {
  const holdArgv = process.argv;
  process.argv = [process.argv0, '--foo'];
  const holdExecArgv = process.execArgv;
  process.execArgv = ['--print', '0'];
  const result = parseArgs({ strict: false });

  const expected = { values: { __proto__: null, foo: true },
                     positionals: [] };
  assert.deepStrictEqual(result, expected);
  process.argv = holdArgv;
  process.execArgv = holdExecArgv;
});

test('correct default args when use node -e', () => {
  const holdArgv = process.argv;
  process.argv = [process.argv0, '--foo'];
  const holdExecArgv = process.execArgv;
  process.execArgv = ['-e', '0'];
  const result = parseArgs({ strict: false });

  const expected = { values: { __proto__: null, foo: true },
                     positionals: [] };
  assert.deepStrictEqual(result, expected);
  process.argv = holdArgv;
  process.execArgv = holdExecArgv;
});

test('correct default args when use node --eval', () => {
  const holdArgv = process.argv;
  process.argv = [process.argv0, '--foo'];
  const holdExecArgv = process.execArgv;
  process.execArgv = ['--eval', '0'];
  const result = parseArgs({ strict: false });
  const expected = { values: { __proto__: null, foo: true },
                     positionals: [] };
  assert.deepStrictEqual(result, expected);
  process.argv = holdArgv;
  process.execArgv = holdExecArgv;
});

test('correct default args when normal arguments', () => {
  const holdArgv = process.argv;
  process.argv = [process.argv0, 'script.js', '--foo'];
  const holdExecArgv = process.execArgv;
  process.execArgv = [];
  const result = parseArgs({ strict: false });

  const expected = { values: { __proto__: null, foo: true },
                     positionals: [] };
  assert.deepStrictEqual(result, expected);
  process.argv = holdArgv;
  process.execArgv = holdExecArgv;
});

test('excess leading dashes on options are retained', () => {
  // Enforce a design decision for an edge case.
  const args = ['---triple'];
  const options = { };
  const expected = {
    values: { '__proto__': null, '-triple': true },
    positionals: []
  };
  const result = parseArgs({ strict: false, args, options });
  assert.deepStrictEqual(result, expected, Error('excess option dashes are retained'));
});

test('positional arguments are allowed by default in strict:false', () => {
  const args = ['foo'];
  const options = { };
  const expected = {
    values: { __proto__: null },
    positionals: ['foo']
  };
  const result = parseArgs({ strict: false, args, options });
  assert.deepStrictEqual(result, expected);
});

test('positional arguments may be explicitly disallowed in strict:false', () => {
  const args = ['foo'];
  const options = { };
  assert.throws(() => { parseArgs({ strict: false, allowPositionals: false, args, options }); }, {
    code: 'ERR_PARSE_ARGS_UNEXPECTED_POSITIONAL'
  });
});

// Test bad inputs

test('invalid argument passed for options', () => {
  const args = ['--so=wat'];
  const options = 'bad value';
  assert.throws(() => { parseArgs({ args, options }); }, {
    code: 'ERR_INVALID_ARG_TYPE'
  });
});

test('type property missing for option then throw', () => {
  const knownOptions = { foo: { } };
  assert.throws(() => { parseArgs({ options: knownOptions }); }, {
    code: 'ERR_INVALID_ARG_TYPE'
  });
});

test('boolean passed to "type" option', () => {
  const args = ['--so=wat'];
  const options = { foo: { type: true } };
  assert.throws(() => { parseArgs({ args, options }); }, {
    code: 'ERR_INVALID_ARG_TYPE'
  });
});

test('invalid union value passed to "type" option', () => {
  const args = ['--so=wat'];
  const options = { foo: { type: 'str' } };
  assert.throws(() => { parseArgs({ args, options }); }, {
    code: 'ERR_INVALID_ARG_TYPE'
  });
});

// Test strict mode

test('unknown long option --bar', () => {
  const args = ['--foo', '--bar'];
  const options = { foo: { type: 'boolean' } };
  assert.throws(() => { parseArgs({ args, options }); }, {
    code: 'ERR_PARSE_ARGS_UNKNOWN_OPTION'
  });
});

test('unknown short option -b', () => {
  const args = ['--foo', '-b'];
  const options = { foo: { type: 'boolean' } };
  assert.throws(() => { parseArgs({ args, options }); }, {
    code: 'ERR_PARSE_ARGS_UNKNOWN_OPTION'
  });
});

test('unknown option -r in short option group -bar', () => {
  const args = ['-bar'];
  const options = { b: { type: 'boolean' }, a: { type: 'boolean' } };
  assert.throws(() => { parseArgs({ args, options }); }, {
    code: 'ERR_PARSE_ARGS_UNKNOWN_OPTION'
  });
});

test('unknown option with explicit value', () => {
  const args = ['--foo', '--bar=baz'];
  const options = { foo: { type: 'boolean' } };
  assert.throws(() => { parseArgs({ args, options }); }, {
    code: 'ERR_PARSE_ARGS_UNKNOWN_OPTION'
  });
});

test('unexpected positional', () => {
  const args = ['foo'];
  const options = { foo: { type: 'boolean' } };
  assert.throws(() => { parseArgs({ args, options }); }, {
    code: 'ERR_PARSE_ARGS_UNEXPECTED_POSITIONAL'
  });
});

test('unexpected positional after --', () => {
  const args = ['--', 'foo'];
  const options = { foo: { type: 'boolean' } };
  assert.throws(() => { parseArgs({ args, options }); }, {
    code: 'ERR_PARSE_ARGS_UNEXPECTED_POSITIONAL'
  });
});

test('-- by itself is not a positional', () => {
  const args = ['--foo', '--'];
  const options = { foo: { type: 'boolean' } };
  const result = parseArgs({ args, options });
  const expected = { values: { __proto__: null, foo: true },
                     positionals: [] };
  assert.deepStrictEqual(result, expected);
});

test('string option used as boolean', () => {
  const args = ['--foo'];
  const options = { foo: { type: 'string' } };
  assert.throws(() => { parseArgs({ args, options }); }, {
    code: 'ERR_PARSE_ARGS_INVALID_OPTION_VALUE'
  });
});

test('boolean option used with value', () => {
  const args = ['--foo=bar'];
  const options = { foo: { type: 'boolean' } };
  assert.throws(() => { parseArgs({ args, options }); }, {
    code: 'ERR_PARSE_ARGS_INVALID_OPTION_VALUE'
  });
});

test('invalid short option length', () => {
  const args = [];
  const options = { foo: { short: 'fo', type: 'boolean' } };
  assert.throws(() => { parseArgs({ args, options }); }, {
    code: 'ERR_INVALID_ARG_VALUE'
  });
});

test('null prototype: when no options then values.toString is undefined', () => {
  const result = parseArgs({ args: [] });
  assert.strictEqual(result.values.toString, undefined);
});

test('null prototype: when --toString then values.toString is true', () => {
  const args = ['--toString'];
  const options = { toString: { type: 'boolean' } };
  const expectedResult = { values: { __proto__: null, toString: true }, positionals: [] };

  const result = parseArgs({ args, options });
  assert.deepStrictEqual(result, expectedResult);
});

const candidateGreedyOptions = [
  '',
  '-',
  '--',
  'abc',
  '123',
  '-s',
  '--foo',
];

for (const value of candidateGreedyOptions) {
  test(`greedy: when short option with value '${value}' then eaten`, () => {
    const args = ['-w', value];
    const options = { with: { type: 'string', short: 'w' } };
    const expectedResult = { values: { __proto__: null, with: value }, positionals: [] };

    const result = parseArgs({ args, options, strict: false });
    assert.deepStrictEqual(result, expectedResult);
  });

  test(`greedy: when long option with value '${value}' then eaten`, () => {
    const args = ['--with', value];
    const options = { with: { type: 'string', short: 'w' } };
    const expectedResult = { values: { __proto__: null, with: value }, positionals: [] };

    const result = parseArgs({ args, options, strict: false });
    assert.deepStrictEqual(result, expectedResult);
  });
}

test('strict: when candidate option value is plain text then does not throw', () => {
  const args = ['--with', 'abc'];
  const options = { with: { type: 'string' } };
  const expectedResult = { values: { __proto__: null, with: 'abc' }, positionals: [] };

  const result = parseArgs({ args, options, strict: true });
  assert.deepStrictEqual(result, expectedResult);
});

test("strict: when candidate option value is '-' then does not throw", () => {
  const args = ['--with', '-'];
  const options = { with: { type: 'string' } };
  const expectedResult = { values: { __proto__: null, with: '-' }, positionals: [] };

  const result = parseArgs({ args, options, strict: true });
  assert.deepStrictEqual(result, expectedResult);
});

test("strict: when candidate option value is '--' then throws", () => {
  const args = ['--with', '--'];
  const options = { with: { type: 'string' } };

  assert.throws(() => {
    parseArgs({ args, options });
  }, {
    code: 'ERR_PARSE_ARGS_INVALID_OPTION_VALUE'
  });
});

test('strict: when candidate option value is short option then throws', () => {
  const args = ['--with', '-a'];
  const options = { with: { type: 'string' } };

  assert.throws(() => {
    parseArgs({ args, options });
  }, {
    code: 'ERR_PARSE_ARGS_INVALID_OPTION_VALUE'
  });
});

test('strict: when candidate option value is short option digit then throws', () => {
  const args = ['--with', '-1'];
  const options = { with: { type: 'string' } };

  assert.throws(() => {
    parseArgs({ args, options });
  }, {
    code: 'ERR_PARSE_ARGS_INVALID_OPTION_VALUE'
  });
});

test('strict: when candidate option value is long option then throws', () => {
  const args = ['--with', '--foo'];
  const options = { with: { type: 'string' } };

  assert.throws(() => {
    parseArgs({ args, options });
  }, {
    code: 'ERR_PARSE_ARGS_INVALID_OPTION_VALUE'
  });
});

test('strict: when short option and suspect value then throws with short option in error message', () => {
  const args = ['-w', '--foo'];
  const options = { with: { type: 'string', short: 'w' } };

  assert.throws(() => {
    parseArgs({ args, options });
  }, /for '-w'/
  );
});

test('strict: when long option and suspect value then throws with long option in error message', () => {
  const args = ['--with', '--foo'];
  const options = { with: { type: 'string' } };

  assert.throws(() => {
    parseArgs({ args, options });
  }, /for '--with'/
  );
});

test('strict: when short option and suspect value then throws with whole expected message', () => {
  const args = ['-w', '--foo'];
  const options = { with: { type: 'string', short: 'w' } };

  try {
    parseArgs({ args, options });
  } catch (err) {
    console.info(err.message);
  }

  assert.throws(() => {
    parseArgs({ args, options });
  }, /To specify an option argument starting with a dash use '--with=-XYZ' or '-w-XYZ'/
  );
});

test('strict: when long option and suspect value then throws with whole expected message', () => {
  const args = ['--with', '--foo'];
  const options = { with: { type: 'string', short: 'w' } };

  assert.throws(() => {
    parseArgs({ args, options });
  }, /To specify an option argument starting with a dash use '--with=-XYZ'/
  );
});

test('tokens: positional', () => {
  const args = ['one'];
  const expectedTokens = [
    { kind: 'positional', index: 0, value: 'one' },
  ];
  const { tokens } = parseArgs({ strict: false, args, tokens: true });
  assert.deepStrictEqual(tokens, expectedTokens);
});

test('tokens: -- followed by option-like', () => {
  const args = ['--', '--foo'];
  const expectedTokens = [
    { kind: 'option-terminator', index: 0 },
    { kind: 'positional', index: 1, value: '--foo' },
  ];
  const { tokens } = parseArgs({ strict: false, args, tokens: true });
  assert.deepStrictEqual(tokens, expectedTokens);
});

test('tokens: strict:true boolean short', () => {
  const args = ['-f'];
  const options = {
    file: { short: 'f', type: 'boolean' }
  };
  const expectedTokens = [
    { kind: 'option', name: 'file', rawName: '-f',
      index: 0, value: undefined, inlineValue: undefined },
  ];
  const { tokens } = parseArgs({ strict: true, args, options, tokens: true });
  assert.deepStrictEqual(tokens, expectedTokens);
});

test('tokens: strict:true boolean long', () => {
  const args = ['--file'];
  const options = {
    file: { short: 'f', type: 'boolean' }
  };
  const expectedTokens = [
    { kind: 'option', name: 'file', rawName: '--file',
      index: 0, value: undefined, inlineValue: undefined },
  ];
  const { tokens } = parseArgs({ strict: true, args, options, tokens: true });
  assert.deepStrictEqual(tokens, expectedTokens);
});

test('tokens: strict:false boolean short', () => {
  const args = ['-f'];
  const expectedTokens = [
    { kind: 'option', name: 'f', rawName: '-f',
      index: 0, value: undefined, inlineValue: undefined },
  ];
  const { tokens } = parseArgs({ strict: false, args, tokens: true });
  assert.deepStrictEqual(tokens, expectedTokens);
});

test('tokens: strict:false boolean long', () => {
  const args = ['--file'];
  const expectedTokens = [
    { kind: 'option', name: 'file', rawName: '--file',
      index: 0, value: undefined, inlineValue: undefined },
  ];
  const { tokens } = parseArgs({ strict: false, args, tokens: true });
  assert.deepStrictEqual(tokens, expectedTokens);
});

test('tokens: strict:false boolean option group', () => {
  const args = ['-ab'];
  const expectedTokens = [
    { kind: 'option', name: 'a', rawName: '-a',
      index: 0, value: undefined, inlineValue: undefined },
    { kind: 'option', name: 'b', rawName: '-b',
      index: 0, value: undefined, inlineValue: undefined },
  ];
  const { tokens } = parseArgs({ strict: false, args, tokens: true });
  assert.deepStrictEqual(tokens, expectedTokens);
});

test('tokens: strict:false boolean option group with repeated option', () => {
  // Also positional to check index correct after grouop
  const args = ['-aa', 'pos'];
  const expectedTokens = [
    { kind: 'option', name: 'a', rawName: '-a',
      index: 0, value: undefined, inlineValue: undefined },
    { kind: 'option', name: 'a', rawName: '-a',
      index: 0, value: undefined, inlineValue: undefined },
    { kind: 'positional', index: 1, value: 'pos' },
  ];
  const { tokens } = parseArgs({ strict: false, allowPositionals: true, args, tokens: true });
  assert.deepStrictEqual(tokens, expectedTokens);
});

test('tokens: strict:true string short with value after space', () => {
  // Also positional to check index correct after out-of-line.
  const args = ['-f', 'bar', 'ppp'];
  const options = {
    file: { short: 'f', type: 'string' }
  };
  const expectedTokens = [
    { kind: 'option', name: 'file', rawName: '-f',
      index: 0, value: 'bar', inlineValue: false },
    { kind: 'positional', index: 2, value: 'ppp' },
  ];
  const { tokens } = parseArgs({ strict: true, allowPositionals: true, args, options, tokens: true });
  assert.deepStrictEqual(tokens, expectedTokens);
});

test('tokens: strict:true string short with value inline', () => {
  const args = ['-fBAR'];
  const options = {
    file: { short: 'f', type: 'string' }
  };
  const expectedTokens = [
    { kind: 'option', name: 'file', rawName: '-f',
      index: 0, value: 'BAR', inlineValue: true },
  ];
  const { tokens } = parseArgs({ strict: true, args, options, tokens: true });
  assert.deepStrictEqual(tokens, expectedTokens);
});

test('tokens: strict:false string short missing value', () => {
  const args = ['-f'];
  const options = {
    file: { short: 'f', type: 'string' }
  };
  const expectedTokens = [
    { kind: 'option', name: 'file', rawName: '-f',
      index: 0, value: undefined, inlineValue: undefined },
  ];
  const { tokens } = parseArgs({ strict: false, args, options, tokens: true });
  assert.deepStrictEqual(tokens, expectedTokens);
});

test('tokens: strict:true string long with value after space', () => {
  // Also positional to check index correct after out-of-line.
  const args = ['--file', 'bar', 'ppp'];
  const options = {
    file: { short: 'f', type: 'string' }
  };
  const expectedTokens = [
    { kind: 'option', name: 'file', rawName: '--file',
      index: 0, value: 'bar', inlineValue: false },
    { kind: 'positional', index: 2, value: 'ppp' },
  ];
  const { tokens } = parseArgs({ strict: true, allowPositionals: true, args, options, tokens: true });
  assert.deepStrictEqual(tokens, expectedTokens);
});

test('tokens: strict:true string long with value inline', () => {
  // Also positional to check index correct after out-of-line.
  const args = ['--file=bar', 'pos'];
  const options = {
    file: { short: 'f', type: 'string' }
  };
  const expectedTokens = [
    { kind: 'option', name: 'file', rawName: '--file',
      index: 0, value: 'bar', inlineValue: true },
    { kind: 'positional', index: 1, value: 'pos' },
  ];
  const { tokens } = parseArgs({ strict: true, allowPositionals: true, args, options, tokens: true });
  assert.deepStrictEqual(tokens, expectedTokens);
});

test('tokens: strict:false string long with value inline', () => {
  const args = ['--file=bar'];
  const expectedTokens = [
    { kind: 'option', name: 'file', rawName: '--file',
      index: 0, value: 'bar', inlineValue: true },
  ];
  const { tokens } = parseArgs({ strict: false, args, tokens: true });
  assert.deepStrictEqual(tokens, expectedTokens);
});

test('tokens: strict:false string long missing value', () => {
  const args = ['--file'];
  const options = {
    file: { short: 'f', type: 'string' }
  };
  const expectedTokens = [
    { kind: 'option', name: 'file', rawName: '--file',
      index: 0, value: undefined, inlineValue: undefined },
  ];
  const { tokens } = parseArgs({ strict: false, args, options, tokens: true });
  assert.deepStrictEqual(tokens, expectedTokens);
});

test('tokens: strict:true complex option group with value after space', () => {
  // Also positional to check index correct afterwards.
  const args = ['-ab', 'c', 'pos'];
  const options = {
    alpha: { short: 'a', type: 'boolean' },
    beta: { short: 'b', type: 'string' },
  };
  const expectedTokens = [
    { kind: 'option', name: 'alpha', rawName: '-a',
      index: 0, value: undefined, inlineValue: undefined },
    { kind: 'option', name: 'beta', rawName: '-b',
      index: 0, value: 'c', inlineValue: false },
    { kind: 'positional', index: 2, value: 'pos' },
  ];
  const { tokens } = parseArgs({ strict: true, allowPositionals: true, args, options, tokens: true });
  assert.deepStrictEqual(tokens, expectedTokens);
});

test('tokens: strict:true complex option group with inline value', () => {
  // Also positional to check index correct afterwards.
  const args = ['-abc', 'pos'];
  const options = {
    alpha: { short: 'a', type: 'boolean' },
    beta: { short: 'b', type: 'string' },
  };
  const expectedTokens = [
    { kind: 'option', name: 'alpha', rawName: '-a',
      index: 0, value: undefined, inlineValue: undefined },
    { kind: 'option', name: 'beta', rawName: '-b',
      index: 0, value: 'c', inlineValue: true },
    { kind: 'positional', index: 1, value: 'pos' },
  ];
  const { tokens } = parseArgs({ strict: true, allowPositionals: true, args, options, tokens: true });
  assert.deepStrictEqual(tokens, expectedTokens);
});

test('tokens: strict:false with single dashes', () => {
  const args = ['--file', '-', '-'];
  const options = {
    file: { short: 'f', type: 'string' },
  };
  const expectedTokens = [
    { kind: 'option', name: 'file', rawName: '--file',
      index: 0, value: '-', inlineValue: false },
    { kind: 'positional', index: 2, value: '-' },
  ];
  const { tokens } = parseArgs({ strict: false, args, options, tokens: true });
  assert.deepStrictEqual(tokens, expectedTokens);
});

test('tokens: strict:false with -- --', () => {
  const args = ['--', '--'];
  const expectedTokens = [
    { kind: 'option-terminator', index: 0 },
    { kind: 'positional', index: 1, value: '--' },
  ];
  const { tokens } = parseArgs({ strict: false, args, tokens: true });
  assert.deepStrictEqual(tokens, expectedTokens);
});

test('default must be a boolean when option type is boolean', () => {
  const args = [];
  const options = { alpha: { type: 'boolean', default: 'not a boolean' } };
  assert.throws(() => {
    parseArgs({ args, options });
  }, /"options\.alpha\.default" property must be of type boolean/
  );
});

test('default must accept undefined value', () => {
  const args = [];
  const options = { alpha: { type: 'boolean', default: undefined } };
  const result = parseArgs({ args, options });
  const expected = {
    values: {
      __proto__: null,
    },
    positionals: []
  };
  assert.deepStrictEqual(result, expected);
});

test('default must be a boolean array when option type is boolean and multiple', () => {
  const args = [];
  const options = { alpha: { type: 'boolean', multiple: true, default: 'not an array' } };
  assert.throws(() => {
    parseArgs({ args, options });
  }, /"options\.alpha\.default" property must be an instance of Array/
  );
});

test('default must be a boolean array when option type is string and multiple is true', () => {
  const args = [];
  const options = { alpha: { type: 'boolean', multiple: true, default: [true, true, 42] } };
  assert.throws(() => {
    parseArgs({ args, options });
  }, /"options\.alpha\.default\[2\]" property must be of type boolean/
  );
});

test('default must be a string when option type is string', () => {
  const args = [];
  const options = { alpha: { type: 'string', default: true } };
  assert.throws(() => {
    parseArgs({ args, options });
  }, /"options\.alpha\.default" property must be of type string/
  );
});

test('default must be an array when option type is string and multiple is true', () => {
  const args = [];
  const options = { alpha: { type: 'string', multiple: true, default: 'not an array' } };
  assert.throws(() => {
    parseArgs({ args, options });
  }, /"options\.alpha\.default" property must be an instance of Array/
  );
});

test('default must be a string array when option type is string and multiple is true', () => {
  const args = [];
  const options = { alpha: { type: 'string', multiple: true, default: ['str', 42] } };
  assert.throws(() => {
    parseArgs({ args, options });
  }, /"options\.alpha\.default\[1\]" property must be of type string/
  );
});

test('default accepted input when multiple is true', () => {
  const args = ['--inputStringArr', 'c', '--inputStringArr', 'd', '--inputBoolArr', '--inputBoolArr'];
  const options = {
    inputStringArr: { type: 'string', multiple: true, default: ['a', 'b'] },
    emptyStringArr: { type: 'string', multiple: true, default: [] },
    fullStringArr: { type: 'string', multiple: true, default: ['a', 'b'] },
    inputBoolArr: { type: 'boolean', multiple: true, default: [false, true, false] },
    emptyBoolArr: { type: 'boolean', multiple: true, default: [] },
    fullBoolArr: { type: 'boolean', multiple: true, default: [false, true, false] },
  };
  const expected = { values: { __proto__: null,
                               inputStringArr: ['c', 'd'],
                               inputBoolArr: [true, true],
                               emptyStringArr: [],
                               fullStringArr: ['a', 'b'],
                               emptyBoolArr: [],
                               fullBoolArr: [false, true, false] },
                     positionals: [] };
  const result = parseArgs({ args, options });
  assert.deepStrictEqual(result, expected);
});

test('when default is set, the option must be added as result', () => {
  const args = [];
  const options = {
    a: { type: 'string', default: 'HELLO' },
    b: { type: 'boolean', default: false },
    c: { type: 'boolean', default: true }
  };
  const expected = { values: { __proto__: null, a: 'HELLO', b: false, c: true }, positionals: [] };

  const result = parseArgs({ args, options });
  assert.deepStrictEqual(result, expected);
});

test('when default is set, the args value takes precedence', () => {
  const args = ['--a', 'WORLD', '--b', '-c'];
  const options = {
    a: { type: 'string', default: 'HELLO' },
    b: { type: 'boolean', default: false },
    c: { type: 'boolean', default: true }
  };
  const expected = { values: { __proto__: null, a: 'WORLD', b: true, c: true }, positionals: [] };

  const result = parseArgs({ args, options });
  assert.deepStrictEqual(result, expected);
});

test('tokens should not include the default options', () => {
  const args = [];
  const options = {
    a: { type: 'string', default: 'HELLO' },
    b: { type: 'boolean', default: false },
    c: { type: 'boolean', default: true }
  };

  const expectedTokens = [];

  const { tokens } = parseArgs({ args, options, tokens: true });
  assert.deepStrictEqual(tokens, expectedTokens);
});

test('tokens:true should not include the default options after the args input', () => {
  const args = ['--z', 'zero', 'positional-item'];
  const options = {
    z: { type: 'string' },
    a: { type: 'string', default: 'HELLO' },
    b: { type: 'boolean', default: false },
    c: { type: 'boolean', default: true }
  };

  const expectedTokens = [
    { kind: 'option', name: 'z', rawName: '--z', index: 0, value: 'zero', inlineValue: false },
    { kind: 'positional', index: 2, value: 'positional-item' },
  ];

  const { tokens } = parseArgs({ args, options, tokens: true, allowPositionals: true });
  assert.deepStrictEqual(tokens, expectedTokens);
});

test('proto as default value must be ignored', () => {
  const args = [];
  const options = { __proto__: null };

  // eslint-disable-next-line no-proto
  options.__proto__ = { type: 'string', default: 'HELLO' };

  const result = parseArgs({ args, options, allowPositionals: true });
  const expected = { values: { __proto__: null }, positionals: [] };
  assert.deepStrictEqual(result, expected);
});


test('multiple as false should expect a String', () => {
  const args = [];
  const options = { alpha: { type: 'string', multiple: false, default: ['array'] } };
  assert.throws(() => {
    parseArgs({ args, options });
  }, /"options\.alpha\.default" property must be of type string/
  );
});

// Test negative options
test('disable negative options and args are started with "--no-" prefix', () => {
  const args = ['--no-alpha'];
  const options = { alpha: { type: 'boolean' } };
  assert.throws(() => {
    parseArgs({ args, options });
  }, {
    code: 'ERR_PARSE_ARGS_UNKNOWN_OPTION'
  });
});

test('args are passed `type: "string"` and allow negative options', () => {
  const args = ['--no-alpha', 'value'];
  const options = { alpha: { type: 'string' } };
  assert.throws(() => {
    parseArgs({ args, options, allowNegative: true });
  }, {
    code: 'ERR_PARSE_ARGS_UNKNOWN_OPTION'
  });
});

test('args are passed `type: "boolean"` and allow negative options', () => {
  const args = ['--no-alpha'];
  const options = { alpha: { type: 'boolean' } };
  const expected = { values: { __proto__: null, alpha: false }, positionals: [] };
  assert.deepStrictEqual(parseArgs({ args, options, allowNegative: true }), expected);
});

test('args are passed `default: "true"` and allow negative options', () => {
  const args = ['--no-alpha'];
  const options = { alpha: { type: 'boolean', default: true } };
  const expected = { values: { __proto__: null, alpha: false }, positionals: [] };
  assert.deepStrictEqual(parseArgs({ args, options, allowNegative: true }), expected);
});

test('args are passed `default: "false" and allow negative options', () => {
  const args = ['--no-alpha'];
  const options = { alpha: { type: 'boolean', default: false } };
  const expected = { values: { __proto__: null, alpha: false }, positionals: [] };
  assert.deepStrictEqual(parseArgs({ args, options, allowNegative: true }), expected);
});

test('allow negative options and multiple as true', () => {
  const args = ['--no-alpha', '--alpha', '--no-alpha'];
  const options = { alpha: { type: 'boolean', multiple: true } };
  const expected = { values: { __proto__: null, alpha: [false, true, false] }, positionals: [] };
  assert.deepStrictEqual(parseArgs({ args, options, allowNegative: true }), expected);
});

test('allow negative options and passed multiple arguments', () => {
  const args = ['--no-alpha', '--alpha'];
  const options = { alpha: { type: 'boolean' } };
  const expected = { values: { __proto__: null, alpha: true }, positionals: [] };
  assert.deepStrictEqual(parseArgs({ args, options, allowNegative: true }), expected);
});

test('auto-detect --no-foo as negated when strict:false and allowNegative', () => {
  const holdArgv = process.argv;
  process.argv = [process.argv0, 'script.js', '--no-foo'];
  const holdExecArgv = process.execArgv;
  process.execArgv = [];
  const result = parseArgs({ strict: false, allowNegative: true });

  const expected = { values: { __proto__: null, foo: false },
                     positionals: [] };
  assert.deepStrictEqual(result, expected);
  process.argv = holdArgv;
  process.execArgv = holdExecArgv;
});
