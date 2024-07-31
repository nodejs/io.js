// Flags: --experimental-test-module-mocks --experimental-require-module
'use strict';
const common = require('../common');

if (!common.isMainThread) {
  common.skip('registering customization hooks in Workers does not work');
}

const fixtures = require('../common/fixtures');
const assert = require('node:assert');
const { relative } = require('node:path');
const { test } = require('node:test');
const { pathToFileURL } = require('node:url');

test('input validation', async (t) => {
  await t.test('throws if specifier is not a string', (t) => {
    assert.throws(() => {
      t.mock.module(5);
    }, { code: 'ERR_INVALID_ARG_TYPE' });
  });

  await t.test('throws if options is not an object', (t) => {
    assert.throws(() => {
      t.mock.module(__filename, null);
    }, { code: 'ERR_INVALID_ARG_TYPE' });
  });

  await t.test('throws if cache is not a boolean', (t) => {
    assert.throws(() => {
      t.mock.module(__filename, { cache: 5 });
    }, { code: 'ERR_INVALID_ARG_TYPE' });
  });

  await t.test('throws if namedExports is not an object', async (t) => {
    assert.throws(() => {
      t.mock.module(__filename, {
        namedExports: null,
      });
    }, { code: 'ERR_INVALID_ARG_TYPE' });
  });
});

test('core module mocking with namedExports option', async (t) => {
  await t.test('does not cache by default', async (t) => {
    const original = require('readline');

    assert.strictEqual(typeof original.cursorTo, 'function');
    assert.strictEqual(original.fn, undefined);

    t.mock.module('readline', {
      namedExports: { fn() { return 42; } },
    });
    const mocked = require('readline');

    assert.notStrictEqual(original, mocked);
    assert.notStrictEqual(mocked, require('readline'));
    assert.notStrictEqual(mocked, require('node:readline'));
    assert.strictEqual(mocked.cursorTo, undefined);
    assert.strictEqual(mocked.fn(), 42);
    t.mock.reset();
    assert.strictEqual(original, require('readline'));
  });

  await t.test('explicitly enables caching', async (t) => {
    const original = require('readline');

    assert.strictEqual(typeof original.cursorTo, 'function');
    assert.strictEqual(original.fn, undefined);

    t.mock.module('readline', {
      namedExports: { fn() { return 42; } },
      cache: true,
    });
    const mocked = require('readline');

    assert.notStrictEqual(original, mocked);
    assert.strictEqual(mocked, require('readline'));
    assert.strictEqual(mocked, require('node:readline'));
    assert.strictEqual(mocked.string, undefined);
    assert.strictEqual(mocked.fn(), 42);
    t.mock.reset();
    assert.strictEqual(original, require('readline'));
  });

  await t.test('explicitly disables caching', async (t) => {
    const original = require('readline');

    assert.strictEqual(typeof original.cursorTo, 'function');
    assert.strictEqual(original.fn, undefined);

    t.mock.module('readline', {
      namedExports: { fn() { return 42; } },
      cache: false,
    });
    const mocked = require('readline');

    assert.notStrictEqual(original, mocked);
    assert.notStrictEqual(mocked, require('readline'));
    assert.strictEqual(mocked.string, undefined);
    assert.strictEqual(mocked.fn(), 42);
    t.mock.reset();
    assert.strictEqual(original, require('readline'));
  });

  await t.test('named exports are applied to defaultExport', async (t) => {
    const original = require('readline');

    assert.strictEqual(typeof original.cursorTo, 'function');
    assert.strictEqual(original.val1, undefined);
    assert.strictEqual(original.val2, undefined);

    const defaultExport = { val1: 5, val2: 3 };

    t.mock.module('readline', {
      defaultExport,
      namedExports: { val1: 'mock value' },
    });
    const mocked = require('readline');

    assert.notStrictEqual(original, mocked);
    assert.strictEqual(mocked.cursorTo, undefined);
    assert.strictEqual(mocked.val1, 'mock value');
    assert.strictEqual(mocked.val2, 3);
    t.mock.reset();
    assert.strictEqual(original, require('readline'));
  });

  await t.test('throws if named exports cannot be applied to defaultExport', async (t) => {
    const fixture = 'readline';
    const original = require(fixture);

    assert.strictEqual(typeof original.cursorTo, 'function');
    assert.strictEqual(original.val1, undefined);

    const defaultExport = null;

    t.mock.module(fixture, {
      defaultExport,
      namedExports: { val1: 'mock value' },
    });
    assert.throws(() => {
      require(fixture);
    }, /Cannot create mock/);
    t.mock.reset();
    assert.strictEqual(original, require(fixture));
  });
});

test('CJS mocking with namedExports option', async (t) => {
  await t.test('does not cache by default', async (t) => {
    const fixture = fixtures.path('module-mocking', 'basic-cjs.js');
    const original = require(fixture);

    assert.strictEqual(original.string, 'original cjs string');
    assert.strictEqual(original.fn, undefined);

    t.mock.module(fixture, {
      namedExports: { fn() { return 42; } },
    });
    const mocked = require(fixture);

    assert.notStrictEqual(original, mocked);
    assert.notStrictEqual(mocked, require(fixture));
    assert.strictEqual(mocked.string, undefined);
    assert.strictEqual(mocked.fn(), 42);
    t.mock.reset();
    assert.strictEqual(original, require(fixture));
  });

  await t.test('explicitly enables caching', async (t) => {
    const fixture = fixtures.path('module-mocking', 'basic-cjs.js');
    const original = require(fixture);

    assert.strictEqual(original.string, 'original cjs string');
    assert.strictEqual(original.fn, undefined);

    t.mock.module(fixture, {
      namedExports: { fn() { return 42; } },
      cache: true,
    });
    const mocked = require(fixture);

    assert.notStrictEqual(original, mocked);
    assert.strictEqual(mocked, require(fixture));
    assert.strictEqual(mocked.string, undefined);
    assert.strictEqual(mocked.fn(), 42);
    t.mock.reset();
    assert.strictEqual(original, require(fixture));
  });

  await t.test('explicitly disables caching', async (t) => {
    const fixture = fixtures.path('module-mocking', 'basic-cjs.js');
    const original = require(fixture);

    assert.strictEqual(original.string, 'original cjs string');
    assert.strictEqual(original.fn, undefined);

    t.mock.module(fixture, {
      namedExports: { fn() { return 42; } },
      cache: false,
    });
    const mocked = require(fixture);

    assert.notStrictEqual(original, mocked);
    assert.notStrictEqual(mocked, require(fixture));
    assert.strictEqual(mocked.string, undefined);
    assert.strictEqual(mocked.fn(), 42);
    t.mock.reset();
    assert.strictEqual(original, require(fixture));
  });

  await t.test('named exports are applied to defaultExport', async (t) => {
    const fixture = fixtures.path('module-mocking', 'basic-cjs.js');
    const original = require(fixture);

    assert.strictEqual(original.string, 'original cjs string');
    assert.strictEqual(original.val1, undefined);
    assert.strictEqual(original.val2, undefined);

    const defaultExport = { val1: 5, val2: 3 };

    t.mock.module(fixture, {
      defaultExport,
      namedExports: { val1: 'mock value' },
    });
    const mocked = require(fixture);

    assert.notStrictEqual(original, mocked);
    assert.strictEqual(mocked.string, undefined);
    assert.strictEqual(mocked.val1, 'mock value');
    assert.strictEqual(mocked.val2, 3);
    t.mock.reset();
    assert.strictEqual(original, require(fixture));
  });

  await t.test('throws if named exports cannot be applied to defaultExport', async (t) => {
    const fixture = fixtures.path('module-mocking', 'basic-cjs.js');
    const original = require(fixture);

    assert.strictEqual(original.string, 'original cjs string');
    assert.strictEqual(original.val1, undefined);

    const defaultExport = null;

    t.mock.module(fixture, {
      defaultExport,
      namedExports: { val1: 'mock value' },
    });
    assert.throws(() => {
      require(fixture);
    }, /Cannot create mock/);
    t.mock.reset();
    assert.strictEqual(original, require(fixture));
  });
});

test('ESM mocking with namedExports option', async (t) => {
  await t.test('does not cache by default', async (t) => {
    const fixture = fixtures.path('module-mocking', 'basic-esm.mjs');
    const original = await import(fixture);

    assert.strictEqual(original.string, 'original esm string');
    assert.strictEqual(original.fn, undefined);

    t.mock.module(fixture, {
      namedExports: { fn() { return 42; } },
    });
    const mocked = await import(fixture);

    assert.notStrictEqual(original, mocked);
    assert.notStrictEqual(mocked, await import(fixture));
    assert.strictEqual(mocked.string, undefined);
    assert.strictEqual(mocked.fn(), 42);
    t.mock.reset();
    assert.strictEqual(original, await import(fixture));
  });

  await t.test('explicitly enables caching', async (t) => {
    const fixture = fixtures.path('module-mocking', 'basic-esm.mjs');
    const original = await import(fixture);

    assert.strictEqual(original.string, 'original esm string');
    assert.strictEqual(original.fn, undefined);

    t.mock.module(fixture, {
      namedExports: { fn() { return 42; } },
      cache: true,
    });
    const mocked = await import(fixture);

    assert.notStrictEqual(original, mocked);
    assert.strictEqual(mocked, await import(fixture));
    assert.strictEqual(mocked.string, undefined);
    assert.strictEqual(mocked.fn(), 42);
    t.mock.reset();
    assert.strictEqual(original, await import(fixture));
  });

  await t.test('explicitly disables caching', async (t) => {
    const fixture = fixtures.path('module-mocking', 'basic-esm.mjs');
    const original = await import(fixture);

    assert.strictEqual(original.string, 'original esm string');
    assert.strictEqual(original.fn, undefined);

    t.mock.module(fixture, {
      namedExports: { fn() { return 42; } },
      cache: false,
    });
    const mocked = await import(fixture);

    assert.notStrictEqual(original, mocked);
    assert.notStrictEqual(mocked, await import(fixture));
    assert.strictEqual(mocked.string, undefined);
    assert.strictEqual(mocked.fn(), 42);
    t.mock.reset();
    assert.strictEqual(original, await import(fixture));
  });

  await t.test('named exports are not applied to defaultExport', async (t) => {
    const fixture = fixtures.path('module-mocking', 'basic-esm.mjs');
    const original = await import(fixture);

    assert.strictEqual(original.string, 'original esm string');
    assert.strictEqual(original.val1, undefined);
    assert.strictEqual(original.val2, undefined);

    const defaultExport = 'mock default';

    t.mock.module(fixture, {
      defaultExport,
      namedExports: { val1: 'mock value' },
    });
    const mocked = await import(fixture);

    assert.notStrictEqual(original, mocked);
    assert.strictEqual(mocked.string, undefined);
    assert.strictEqual(mocked.default, 'mock default');
    assert.strictEqual(mocked.val1, 'mock value');
    t.mock.reset();
    common.expectRequiredModule(require(fixture), original);
  });

  await t.test('throws if named exports cannot be applied to defaultExport as CJS', async (t) => {
    const fixture = fixtures.path('module-mocking', 'basic-cjs.js');
    const original = await import(fixture);

    assert.strictEqual(original.default.string, 'original cjs string');
    assert.strictEqual(original.default.val1, undefined);

    const defaultExport = null;

    t.mock.module(fixture, {
      defaultExport,
      namedExports: { val1: 'mock value' },
    });
    await assert.rejects(async () => {
      await import(fixture);
    }, /Cannot create mock/);

    t.mock.reset();
    assert.strictEqual(original, await import(fixture));
  });
});

test('modules cannot be mocked multiple times at once', async (t) => {
  await t.test('CJS', async (t) => {
    const fixture = fixtures.path('module-mocking', 'basic-cjs.js');

    t.mock.module(fixture, {
      namedExports: { fn() { return 42; } },
    });

    assert.throws(() => {
      t.mock.module(fixture, {
        namedExports: { fn() { return 55; } },
      });
    }, {
      code: 'ERR_INVALID_STATE',
      message: /The module is already mocked/,
    });

    const mocked = require(fixture);

    assert.strictEqual(mocked.fn(), 42);
  });

  await t.test('ESM', async (t) => {
    const fixture = fixtures.path('module-mocking', 'basic-esm.mjs');

    t.mock.module(fixture, {
      namedExports: { fn() { return 42; } },
    });

    assert.throws(() => {
      t.mock.module(fixture, {
        namedExports: { fn() { return 55; } },
      });
    }, {
      code: 'ERR_INVALID_STATE',
      message: /The module is already mocked/,
    });

    const mocked = await import(fixture);

    assert.strictEqual(mocked.fn(), 42);
  });
});

test('mocks are automatically restored', async (t) => {
  const cjsFixture = fixtures.path('module-mocking', 'basic-cjs.js');
  const esmFixture = fixtures.path('module-mocking', 'basic-esm.mjs');

  await t.test('CJS', async (t) => {
    t.mock.module(cjsFixture, {
      namedExports: { fn() { return 42; } },
    });

    const mocked = require(cjsFixture);

    assert.strictEqual(mocked.fn(), 42);
  });

  await t.test('ESM', async (t) => {
    t.mock.module(esmFixture, {
      namedExports: { fn() { return 43; } },
    });

    const mocked = await import(esmFixture);

    assert.strictEqual(mocked.fn(), 43);
  });

  const cjsMock = require(cjsFixture);
  const esmMock = await import(esmFixture);

  assert.strictEqual(cjsMock.string, 'original cjs string');
  assert.strictEqual(cjsMock.fn, undefined);
  assert.strictEqual(esmMock.string, 'original esm string');
  assert.strictEqual(esmMock.fn, undefined);
});

test('mocks can be restored independently', async (t) => {
  const cjsFixture = fixtures.path('module-mocking', 'basic-cjs.js');
  const esmFixture = fixtures.path('module-mocking', 'basic-esm.mjs');

  const cjsMock = t.mock.module(cjsFixture, {
    namedExports: { fn() { return 42; } },
  });

  const esmMock = t.mock.module(esmFixture, {
    namedExports: { fn() { return 43; } },
  });

  let cjsImpl = require(cjsFixture);
  let esmImpl = await import(esmFixture);

  assert.strictEqual(cjsImpl.fn(), 42);
  assert.strictEqual(esmImpl.fn(), 43);

  cjsMock.restore();
  cjsImpl = require(cjsFixture);

  assert.strictEqual(cjsImpl.fn, undefined);
  assert.strictEqual(esmImpl.fn(), 43);

  esmMock.restore();
  esmImpl = await import(esmFixture);

  assert.strictEqual(cjsImpl.fn, undefined);
  assert.strictEqual(esmImpl.fn, undefined);
});

test('core module mocks can be used by both module systems', async (t) => {
  const coreMock = t.mock.module('readline', {
    namedExports: { fn() { return 42; } },
  });

  let esmImpl = await import('readline');
  let cjsImpl = require('readline');

  assert.strictEqual(esmImpl.fn(), 42);
  assert.strictEqual(cjsImpl.fn(), 42);

  coreMock.restore();
  esmImpl = await import('readline');
  cjsImpl = require('readline');

  assert.strictEqual(typeof esmImpl.cursorTo, 'function');
  assert.strictEqual(typeof cjsImpl.cursorTo, 'function');
});

test('node:- core module mocks can be used by both module systems', async (t) => {
  const coreMock = t.mock.module('node:readline', {
    namedExports: { fn() { return 42; } },
  });

  let esmImpl = await import('node:readline');
  let cjsImpl = require('node:readline');

  assert.strictEqual(esmImpl.fn(), 42);
  assert.strictEqual(cjsImpl.fn(), 42);

  coreMock.restore();
  esmImpl = await import('node:readline');
  cjsImpl = require('node:readline');

  assert.strictEqual(typeof esmImpl.cursorTo, 'function');
  assert.strictEqual(typeof cjsImpl.cursorTo, 'function');
});

test('CJS mocks can be used by both module systems', async (t) => {
  const cjsFixture = fixtures.path('module-mocking', 'basic-cjs.js');
  const cjsMock = t.mock.module(cjsFixture, {
    namedExports: { fn() { return 42; } },
  });
  let esmImpl = await import(cjsFixture);
  let cjsImpl = require(cjsFixture);

  assert.strictEqual(esmImpl.fn(), 42);
  assert.strictEqual(cjsImpl.fn(), 42);

  cjsMock.restore();

  esmImpl = await import(cjsFixture);
  cjsImpl = require(cjsFixture);

  assert.strictEqual(esmImpl.default.string, 'original cjs string');
  assert.strictEqual(cjsImpl.string, 'original cjs string');
});

test('ESM mocks can be used by both module systems', async (t) => {
  const esmFixture = fixtures.path('module-mocking', 'basic-esm.mjs');
  const esmMock = t.mock.module(esmFixture, {
    namedExports: { fn() { return 42; } },
  });

  let cjsImpl = require(esmFixture);
  let esmImpl = await import(esmFixture);

  assert.strictEqual(cjsImpl.fn(), 42);
  assert.strictEqual(esmImpl.fn(), 42);

  esmMock.restore();
  cjsImpl = require(esmFixture);
  esmImpl = await import(esmFixture);

  assert.strictEqual(esmImpl.string, 'original esm string');
  assert.strictEqual(cjsImpl.string, 'original esm string');
});

test('relative paths can be used by both module systems', async (t) => {
  const fixture = relative(
    __dirname, fixtures.path('module-mocking', 'basic-esm.mjs')
  );
  const mock = t.mock.module(fixture, {
    namedExports: { fn() { return 42; } },
  });
  let cjsImpl = require(fixture);
  let esmImpl = await import(fixture);

  assert.strictEqual(cjsImpl.fn(), 42);
  assert.strictEqual(esmImpl.fn(), 42);

  mock.restore();
  cjsImpl = require(fixture);
  esmImpl = await import(fixture);

  assert.strictEqual(esmImpl.string, 'original esm string');
  assert.strictEqual(cjsImpl.string, 'original esm string');
});

test('node_modules can be used by both module systems', async (t) => {
  const cwd = fixtures.path('test-runner');
  const fixture = fixtures.path('test-runner', 'mock-nm.js');
  const args = ['--experimental-test-module-mocks', fixture];
  const {
    code,
    stdout,
    signal,
  } = await common.spawnPromisified(process.execPath, args, { cwd });

  assert.strictEqual(code, 0);
  assert.strictEqual(signal, null);
  assert.match(stdout, /# pass 1/);
});

test('file:// imports are supported in ESM only', async (t) => {
  const fixture = pathToFileURL(
    fixtures.path('module-mocking', 'basic-esm.mjs')
  ).href;
  const mock = t.mock.module(fixture, {
    namedExports: { fn() { return 42; } },
  });
  let impl = await import(fixture);

  assert.strictEqual(impl.fn(), 42);
  assert.throws(() => {
    require(fixture);
  }, { code: 'MODULE_NOT_FOUND' });
  mock.restore();
  impl = await import(fixture);
  assert.strictEqual(impl.string, 'original esm string');
});

test('mocked modules do not impact unmocked modules', async (t) => {
  const mockedFixture = fixtures.path('module-mocking', 'basic-cjs.js');
  const unmockedFixture = fixtures.path('module-mocking', 'basic-esm.mjs');
  t.mock.module(mockedFixture, {
    namedExports: { fn() { return 42; } },
  });
  const mockedImpl = await import(mockedFixture);
  const unmockedImpl = await import(unmockedFixture);

  assert.strictEqual(mockedImpl.fn(), 42);
  assert.strictEqual(unmockedImpl.fn, undefined);
  assert.strictEqual(unmockedImpl.string, 'original esm string');
});

test('defaultExports work with CJS mocks in both module systems', async (t) => {
  const fixture = fixtures.path('module-mocking', 'basic-cjs.js');
  const original = require(fixture);
  const defaultExport = Symbol('default');

  assert.strictEqual(original.string, 'original cjs string');
  t.mock.module(fixture, { defaultExport });
  assert.strictEqual(require(fixture), defaultExport);
  assert.strictEqual((await import(fixture)).default, defaultExport);
});

test('defaultExports work with ESM mocks in both module systems', async (t) => {
  const fixture = fixtures.path('module-mocking', 'basic-esm.mjs');
  const original = await import(fixture);
  const defaultExport = Symbol('default');

  assert.strictEqual(original.string, 'original esm string');
  t.mock.module(fixture, { defaultExport });
  assert.strictEqual((await import(fixture)).default, defaultExport);
  assert.strictEqual(require(fixture), defaultExport);
});
