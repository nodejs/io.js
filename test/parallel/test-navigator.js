// Flags: --expose-internals

'use strict';

/* eslint node-core/require-common-first: 0 */

const assert = require('assert');

{

  // Ensures `navigator` has not been evaluated yet
  assert.strictEqual(require.resolve('../common') in require.cache, false);

  const { version, platform, arch } = process;
  try {
    let called = false;
    Object.defineProperty(process, 'arch', { get() { called += 'arch|'; return arch; } });
    Object.defineProperty(process, 'platform', { get() { called = 'platform|'; return platform; } });
    Object.defineProperty(process, 'version', { get() { called = 'version|'; return version; } });

    navigator; // eslint-disable-line no-unused-expressions

    assert.strictEqual(
      called,
      false
    );
  } finally {
    Object.defineProperty(process, 'arch', { value: arch });
    Object.defineProperty(process, 'platform', { value: platform });
    Object.defineProperty(process, 'version', { value: version });
  }
}

const common = require('../common');
const { getNavigatorPlatform } = require('internal/navigator');
const { execFile } = require('child_process');

const is = {
  number: (value, key) => {
    assert(!Number.isNaN(value), `${key} should not be NaN`);
    assert.strictEqual(typeof value, 'number');
  },
};

is.number(+navigator.hardwareConcurrency, 'hardwareConcurrency');
is.number(navigator.hardwareConcurrency, 'hardwareConcurrency');
assert.ok(navigator.hardwareConcurrency > 0);
assert.strictEqual(typeof navigator.userAgent, 'string');
assert.match(navigator.userAgent, /^Node\.js\/\d+$/);

assert.strictEqual(typeof navigator.platform, 'string');
if (process.platform === 'darwin') {
  assert.strictEqual(navigator.platform, 'MacIntel');
} else if (process.platform === 'win32') {
  assert.strictEqual(navigator.platform, 'Win32');
} else if (process.platform === 'linux' && process.arch === 'ia32') {
  assert.strictEqual(navigator.platform, 'Linux i686');
} else if (process.platform === 'linux' && process.arch === 'x64') {
  assert.strictEqual(navigator.platform, 'Linux x86_64');
} else if (process.platform === 'freebsd') {
  if (process.arch === 'ia32') {
    assert.strictEqual(navigator.platform, 'FreeBSD i386');
  } else if (process.arch === 'x64') {
    assert.strictEqual(navigator.platform, 'FreeBSD amd64');
  } else {
    assert.strictEqual(navigator.platform, `FreeBSD ${process.arch}`);
  }
} else if (process.platform === 'openbsd') {
  if (process.arch === 'ia32') {
    assert.strictEqual(navigator.platform, 'OpenBSD i386');
  } else if (process.arch === 'x64') {
    assert.strictEqual(navigator.platform, 'OpenBSD amd64');
  } else {
    assert.strictEqual(navigator.platform, `OpenBSD ${process.arch}`);
  }
} else if (process.platform === 'sunos') {
  if (process.arch === 'ia32') {
    assert.strictEqual(navigator.platform, 'SunOS i86pc');
  } else {
    assert.strictEqual(navigator.platform, `SunOS ${process.arch}`);
  }
} else if (process.platform === 'aix') {
  assert.strictEqual(navigator.platform, 'AIX');
} else {
  assert.strictEqual(navigator.platform, `${process.platform[0].toUpperCase()}${process.platform.slice(1)} ${process.arch}`);
}

assert.strictEqual(getNavigatorPlatform('x64', 'darwin'), 'MacIntel');
assert.strictEqual(getNavigatorPlatform('arm64', 'darwin'), 'MacIntel');
assert.strictEqual(getNavigatorPlatform('ia32', 'linux'), 'Linux i686');
assert.strictEqual(getNavigatorPlatform('x64', 'linux'), 'Linux x86_64');
assert.strictEqual(getNavigatorPlatform('arm64', 'linux'), 'Linux arm64');
assert.strictEqual(getNavigatorPlatform('x64', 'win32'), 'Win32');
assert.strictEqual(getNavigatorPlatform('arm64', 'win32'), 'Win32');
assert.strictEqual(getNavigatorPlatform('ia32', 'freebsd'), 'FreeBSD i386');
assert.strictEqual(getNavigatorPlatform('x64', 'freebsd'), 'FreeBSD amd64');
assert.strictEqual(getNavigatorPlatform('arm64', 'freebsd'), 'FreeBSD arm64');
assert.strictEqual(getNavigatorPlatform('ia32', 'openbsd'), 'OpenBSD i386');
assert.strictEqual(getNavigatorPlatform('x64', 'openbsd'), 'OpenBSD amd64');
assert.strictEqual(getNavigatorPlatform('arm64', 'openbsd'), 'OpenBSD arm64');
assert.strictEqual(getNavigatorPlatform('ia32', 'sunos'), 'SunOS i86pc');
assert.strictEqual(getNavigatorPlatform('x64', 'sunos'), 'SunOS x64');
assert.strictEqual(getNavigatorPlatform('ppc64', 'aix'), 'AIX');
assert.strictEqual(getNavigatorPlatform('x64', 'reactos'), 'Reactos x64');

assert.strictEqual(typeof navigator.language, 'string');
assert.strictEqual(navigator.language.length !== 0, true);

assert.ok(Array.isArray(navigator.languages));
assert.strictEqual(navigator.languages.length, 1);
assert.strictEqual(typeof navigator.languages[0], 'string');
assert.strictEqual(navigator.languages[0].length !== 0, true);

assert.throws(() => {
  navigator.languages[0] = 'foo';
}, new TypeError("Cannot assign to read only property '0' of object '[object Array]'"));
assert.notStrictEqual(navigator.languages[0], 'foo');
assert.strictEqual(typeof navigator.languages[0] === 'string', true);
assert.strictEqual(navigator.languages[0].length !== 0, true);

if (common.hasIntl && common.isWindows === false) {
  const testLocale = navigator.language === 'de-DE' ?
    'en-US' :
    'de-DE';
  {
    const env = { ...process.env, LC_ALL: testLocale };
    execFile(
      process.execPath,
      ['--print', `"process.exit(navigator.language === '${testLocale}' ? 0 : 1)"`],
      { env },
      common.mustSucceed()
    );
  }

  {
    const env = { ...process.env, LC_ALL: testLocale };
    execFile(
      process.execPath,
      ['--print', `"process.exit(navigator.languages[0] === '${testLocale}' ? 0 : 1)"`],
      { env },
      common.mustSucceed()
    );
  }
}

Object.defineProperty(navigator, 'language', { value: 'for-testing' });
assert.strictEqual(navigator.language, 'for-testing');
assert.strictEqual(navigator.languages.length, 1);
assert.strictEqual(navigator.languages[0] !== 'for-testing', true);
