'use strict';

const {
  ObjectAssign,
  ObjectDefineProperty,
} = primordials;

const { test, suite, before, after, beforeEach, afterEach } = require('internal/test_runner/harness');
const { run } = require('internal/test_runner/runner');

module.exports = test;
ObjectAssign(module.exports, {
  after,
  afterEach,
  before,
  beforeEach,
  describe: suite,
  it: test,
  run,
  suite,
  test,
});

let lazyMock;

ObjectDefineProperty(module.exports, 'mock', {
  __proto__: null,
  configurable: true,
  enumerable: true,
  get() {
    if (lazyMock === undefined) {
      const { MockTracker } = require('internal/test_runner/mock/mock');

      lazyMock = new MockTracker();
    }

    return lazyMock;
  },
});

let lazySnapshot;

ObjectDefineProperty(module.exports, 'snapshot', {
  __proto__: null,
  configurable: true,
  enumerable: true,
  get() {
    if (lazySnapshot === undefined) {
      const {
        setDefaultSnapshotSerializers,
        setResolveSnapshotPath,
      } = require('internal/test_runner/snapshot');

      lazySnapshot = {
        __proto__: null,
        setDefaultSnapshotSerializers,
        setResolveSnapshotPath,
      };
    }

    return lazySnapshot;
  },
});

ObjectDefineProperty(module.exports, 'assert', {
  __proto__: null,
  configurable: true,
  enumerable: true,
  get() {
    const { register } = require('internal/test_runner/assert');
    const assert = { __proto__: null, register };
    ObjectDefineProperty(module.exports, 'assert', assert);
    return assert;
  },
});
