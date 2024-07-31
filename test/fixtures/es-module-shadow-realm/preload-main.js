// This fixture is used to test that --require preload modules are not enabled in the ShadowRealm.

'use strict';
const assert = require('assert');

assert.strictEqual(globalThis.preload, 42);
const realm = new ShadowRealm();
const value = realm.evaluate(`globalThis.preload`);
assert.strictEqual(value, undefined);
