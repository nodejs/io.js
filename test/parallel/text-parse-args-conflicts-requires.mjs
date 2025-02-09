import '../common/index.mjs';
import assert from 'node:assert';
import { test } from 'node:test';
import { parseArgs } from 'node:util';

test('required option cannot have a default', () => {
  const options = {
    f: {
      requires: ['g'],
      type: 'string',
    },
    g: {
      default: 'b',
      type: 'string',
    },
  };
  assert.throws(() => { parseArgs({ options }); }, {
    code: 'ERR_PARSE_ARGS_INVALID_OPTION_CONFIG'
  });
});

test('required option cannot also be a conflict', () => {
  const options = {
    f: {
      requires: ['g'],
      conflicts: ['g'],
      type: 'string',
    },
  };
  assert.throws(() => { parseArgs({ options }); }, {
    code: 'ERR_PARSE_ARGS_INVALID_OPTION_CONFIG'
  });
});

test('required option cannot also be a conflict 2', () => {
  const options = {
    f: {
      requires: ['g'],
      type: 'string',
    },
    g: {
      type: 'string',
      conflicts: ['f'],
    }
  };
  assert.throws(() => { parseArgs({ options }); }, {
    code: 'ERR_PARSE_ARGS_INVALID_OPTION_CONFIG'
  });
});

test('requirements fulfilled', () => {
  const options = {
    f: {
      requiresOne: ['g', 'h'],
      type: 'string',
    },
    g: {
      type: 'string',
    }
  };
  const args = ['--f', '1', '--g', '2', '--h=3'];
  const expected = { values: { __proto__: null, f: '1', g: '2', h: '3' }, positionals: [] };
  assert.deepStrictEqual(parseArgs({ args, options, strict: false }), expected);
});

test('requireOne fulfilled', () => {
  const options = {
    f: {
      requiresOne: ['g', 'h'],
      type: 'string',
    },
    g: {
      type: 'string',
    }
  };
  const args = ['--f', '1', '--h=2'];
  const expected = { values: { __proto__: null, f: '1', h: '2' }, positionals: [] };
  assert.deepStrictEqual(parseArgs({ args, options, strict: false }), expected);
});

test('missing required option', () => {
  const options = {
    f: {
      requires: ['g', 'h'],
      type: 'string',
    },
    g: {
      type: 'string',
    },
    h: {
      type: 'string',
    },
  };
  const args = ['--f', '1', '--g', '2'];
  assert.throws(() => { parseArgs({ options, args }); }, {
    code: 'ERR_PARSE_ARGS_INVALID_OPTION_VALUE'
  });
});

test('missing requiredOne option', () => {
  const options = {
    f: {
      requiresOne: ['g', 'h'],
      type: 'string',
    },
    g: {
      type: 'string',
    },
    h: {
      type: 'string',
    },
  };
  const args = ['--f', '1'];
  assert.throws(() => { parseArgs({ options, args }); }, {
    code: 'ERR_PARSE_ARGS_INVALID_OPTION_VALUE'
  });
});

test('conflicting options', () => {
  const options = {
    f: {
      conflicts: ['g'],
      type: 'string',
    },
    g: {
      type: 'string',
    },
  };
  const args = ['--f', '1', '--g', '2'];
  assert.throws(() => { parseArgs({ options, args }); }, {
    code: 'ERR_PARSE_ARGS_INVALID_OPTION_VALUE'
  });
});
