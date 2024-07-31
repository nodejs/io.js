const { Module: { createRequire } } = require('module');
const createdRequire = createRequire(__filename);

// Although, require('../common') works locally, that couldn't be used here
// because we set NODE_TEST_DIR=/Users/iojs/node-tmp on Jenkins CI.
const { expectWarning, mustNotCall } = createdRequire(process.env.COMMON_DIRECTORY);

const builtinWarning =
`Currently the require() provided to the main script embedded into single-executable applications only supports loading built-in modules.
To load a module from disk after the single executable application is launched, use require("module").createRequire().
Support for bundled module loading or virtual file systems are under discussions in https://github.com/nodejs/single-executable`;

expectWarning('Warning', builtinWarning);  // Triggered by require() calls below.
// This additionally makes sure that no unexpected warnings are emitted.
if (!createdRequire('./sea-config.json').disableExperimentalSEAWarning) {
  expectWarning('ExperimentalWarning',
                'Single executable application is an experimental feature and ' +
                'might change at any time');
  // Any unexpected warning would throw this error:
  // https://github.com/nodejs/node/blob/c301404105a7256b79a0b8c4522ce47af96dfa17/test/common/index.js#L697-L700.
}

// Should be possible to require core modules that optionally require the
// "node:" scheme.
const { deepStrictEqual, strictEqual, throws } = require('assert');
const { dirname } = require('node:path');

// Checks that the source filename is used in the error stack trace.
strictEqual(new Error('lol').stack.split('\n')[1], '    at sea.js:29:13');

// Should be possible to require a core module that requires using the "node:"
// scheme.
{
  const { test } = require('node:test');
  strictEqual(typeof test, 'function');
}

// Should not be possible to require a core module without the "node:" scheme if
// it requires using the "node:" scheme.
throws(() => require('test'), {
  code: 'ERR_UNKNOWN_BUILTIN_MODULE',
});

deepStrictEqual(process.argv, [process.execPath, process.execPath, '-a', '--b=c', 'd']);

strictEqual(require.cache, undefined);
strictEqual(require.extensions, undefined);
strictEqual(require.main, module);
strictEqual(require.resolve, undefined);

strictEqual(__filename, process.execPath);
strictEqual(__dirname, dirname(process.execPath));
strictEqual(module.exports, exports);

throws(() => require('./requirable.js'), {
  code: 'ERR_UNKNOWN_BUILTIN_MODULE',
});

const requirable = createdRequire('./requirable.js');
deepStrictEqual(requirable, {
  hello: 'world',
});

console.log('Hello, world! 😊');
