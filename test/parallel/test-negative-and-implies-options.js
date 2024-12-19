'use strict';

const common = require('../common');
const assert = require('assert');
const { describe, it } = require('node:test');

describe('negative and implies options', () => {
  it('should remove --inspect option when --no-inspect-brk is specified', async () => {
    const { stdout, stderr } = await common.spawnPromisified(
      process.execPath,
      ['--inspect-brk=0', '--no-inspect-brk', '-e', "console.log('inspect-brk')"]);
    assert.strictEqual(stdout, 'inspect-brk\n');
    assert.strictEqual(stderr, '');
  });

  it('should remove --inspect option when --no-inspect-wait is specified', async () => {
    const { stdout, stderr } = await common.spawnPromisified(
      process.execPath,
      ['--inspect-wait=0', '--no-inspect-wait', '-e', "console.log('inspect-wait')"]);
    assert.strictEqual(stdout, 'inspect-wait\n');
    assert.strictEqual(stderr, '');
  });

  it('should remove --trace-env option when --no-trace-env-js-stack is specified', async () => {
    const { stdout, stderr } = await common.spawnPromisified(
      process.execPath,
      ['--trace-env-js-stack', '--no-trace-env-js-stack', '-e', "console.log('trace-env-js-stack')"]);
    assert.strictEqual(stdout, 'trace-env-js-stack\n');
    assert.strictEqual(stderr, '');
  });

  it('should remove --trace-env option when --no-trace-env-native-stack is specified', async () => {
    const { stdout, stderr } = await common.spawnPromisified(
      process.execPath,
      ['--trace-env-native-stack', '--no-trace-env-native-stack', '-e', "console.log('trace-env-native-stack')"]);
    assert.strictEqual(stdout, 'trace-env-native-stack\n');
    assert.strictEqual(stderr, '');
  });

  it('should remove implies options when --no-experimental-transform-types is specified',
     async () => {
       {
         const { stdout, stderr } = await common.spawnPromisified(
           process.execPath,
           ['--experimental-transform-types',
            '--no-experimental-transform-types',
            '-e',
            "console.log('experimental-transform-types')"]);
         assert.strictEqual(stdout, 'experimental-transform-types\n');
         assert.strictEqual(stderr, '');
       }
       {
         const { stderr } = await common.spawnPromisified(
           process.execPath,
           ['--experimental-transform-types',
            '--no-experimental-transform-types',
            '../fixtures/source-map/throw-async.mjs']);
         assert.doesNotMatch(
           stderr,
           /at Throw \([^)]+throw-async\.ts:4:9\)/
         );
       }
     });
  it('should remove shadow-realm option when negate shadow-realm options are specified', async () => {
    {
      const { stdout, stderr } = await common.spawnPromisified(
        process.execPath,
        ['--experimental-shadow-realm',
         '--no-experimental-shadow-realm',
         '-e',
         "new ShadowRealm().eval('console.log(1)')",
        ]);
      assert.strictEqual(stdout, '');
      assert.match(stderr, /Error: Not supported/);
    }
    {
      const { stdout, stderr } = await common.spawnPromisified(
        process.execPath,
        ['--harmony-shadow-realm', '--no-harmony-shadow-realm', '-e', "new ShadowRealm().eval('console.log(1)')"]);
      assert.strictEqual(stdout, '');
      assert.match(stderr, /ReferenceError: ShadowRealm is not defined/);
    }
    {
      const { stdout, stderr } = await common.spawnPromisified(
        process.execPath,
        ['--harmony-shadow-realm', '--no-experimental-shadow-realm', '-e', "new ShadowRealm().eval('console.log(1)')"]);
      assert.strictEqual(stdout, '');
      assert.match(stderr, /Error: Not supported/);
    }
    {
      const { stdout, stderr } = await common.spawnPromisified(
        process.execPath,
        ['--experimental-shadow-realm', '--no-harmony-shadow-realm', '-e', "new ShadowRealm().eval('console.log(1)')"]);
      assert.strictEqual(stdout, '');
      assert.match(stderr, /ReferenceError: ShadowRealm is not defined/);
    }
  });


  it('should not affect with only negation option', async () => {
    const { stdout, stderr } = await common.spawnPromisified(
      process.execPath,
      ['--no-inspect-brk', '-e', "console.log('inspect-brk')"]);
    assert.strictEqual(stdout, 'inspect-brk\n');
    assert.strictEqual(stderr, '');
  });

  it('should throw no boolean option error', async () => {
    const { stdout, stderr } = await common.spawnPromisified(
      process.execPath,
      [`--env-file=../fixtures/dotenv/.env`, '--no-env-file', '-e', 'const foo = 1'],
      { cwd: __dirname });

    assert.strictEqual(stdout, '');
    assert.match(stderr, /--no-env-file is an invalid negation because it is not a boolean option/);
  });
});
