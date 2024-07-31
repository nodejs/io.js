'use strict';
require('../common');
const assert = require('assert');
const vm = require('vm');

const sandbox = {
  onSelf: 'onSelf',
};

function onSelfGetter() {
  return 'onSelfGetter';
}

Object.defineProperty(sandbox, 'onSelfGetter', {
  get: onSelfGetter,
});

Object.defineProperty(sandbox, 1, {
  value: 'onSelfIndexed',
  writable: false,
  enumerable: false,
  configurable: true,
});

const ctx = vm.createContext(sandbox);

const result = vm.runInContext(`
Object.prototype.onProto = 'onProto';
Object.defineProperty(Object.prototype, 'onProtoGetter', {
  get() {
    return 'onProtoGetter';
  },
});
Object.defineProperty(Object.prototype, 2, {
  value: 'onProtoIndexed',
  writable: false,
  enumerable: false,
  configurable: true,
});

const resultHasOwn = {
  onSelf: Object.hasOwn(this, 'onSelf'),
  onSelfGetter: Object.hasOwn(this, 'onSelfGetter'),
  onSelfIndexed: Object.hasOwn(this, 1),
  onProto: Object.hasOwn(this, 'onProto'),
  onProtoGetter: Object.hasOwn(this, 'onProtoGetter'),
  onProtoIndexed: Object.hasOwn(this, 2),
};

const getDesc = (prop) => Object.getOwnPropertyDescriptor(this, prop);
const resultDesc = {
  onSelf: getDesc('onSelf'),
  onSelfGetter: getDesc('onSelfGetter'),
  onSelfIndexed: getDesc(1),
  onProto: getDesc('onProto'),
  onProtoGetter: getDesc('onProtoGetter'),
  onProtoIndexed: getDesc(2),
};
({
  resultHasOwn,
  resultDesc,
});
`, ctx);

// eslint-disable-next-line no-restricted-properties
assert.deepEqual(result, {
  resultHasOwn: {
    onSelf: true,
    onSelfGetter: true,
    onSelfIndexed: true,
    onProto: false,
    onProtoGetter: false,
    onProtoIndexed: false,
  },
  resultDesc: {
    onSelf: { value: 'onSelf', writable: true, enumerable: true, configurable: true },
    onSelfGetter: { get: onSelfGetter, set: undefined, enumerable: false, configurable: false },
    onSelfIndexed: { value: 'onSelfIndexed', writable: false, enumerable: false, configurable: true },
    onProto: undefined,
    onProtoGetter: undefined,
    onProtoIndexed: undefined,
  },
});
