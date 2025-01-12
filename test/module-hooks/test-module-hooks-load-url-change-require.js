'use strict';

const common = require('../common');
const assert = require('assert');
const { registerHooks } = require('module');
const fixtures = require('../common/fixtures');

// This tests shows the url parameter in `load` can be changed in the `nextLoad` call
// It changes `foo` package name into `redirected-fs` and then loads `redirected-fs`

const hook = registerHooks({
  resolve(specifier, context, nextResolve) {
    assert.strictEqual(specifier, 'foo');
    return {
      url: 'foo://bar',
      shortCircuit: true,
    };
  },
  load: common.mustCall(function load(url, context, nextLoad) {
    assert.strictEqual(url, 'foo://bar');
    return nextLoad(
      fixtures.fileURL('module-hooks', 'redirected-fs.js').href,
      context,
    );
  }),
});

assert.strictEqual(require('foo').exports_for_test, 'redirected fs');

hook.deregister();
