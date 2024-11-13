'use strict';
require('../common');
const assert = require('assert');
const vm = require('vm');

const outerProto = {
  onOuterProto: 'onOuterProto',
  bothProto: 'onOuterProto',
};
function onOuterProtoGetter() {
  return 'onOuterProtoGetter';
}
Object.defineProperties(outerProto, {
  onOuterProtoGetter: {
    get: onOuterProtoGetter,
  },
  bothProtoGetter: {
    get: onOuterProtoGetter,
  },
  // outer proto indexed
  0: {
    value: 'onOuterProtoIndexed',
    writable: false,
    enumerable: false,
    configurable: true,
  },
  // both proto indexed
  3: {
    value: 'onOuterProtoIndexed',
    writable: false,
    enumerable: false,
    configurable: true,
  },
});

// Creating a new intermediate proto to mimic the
// window -> Window.prototype -> EventTarget.prototype chain in JSDom.
const sandboxProto = {
  __proto__: outerProto,
};

const sandbox = {
  __proto__: sandboxProto,
  onSelf: 'onSelf',
};

function onSelfGetter() {
  return 'onSelfGetter';
}
Object.defineProperties(sandbox, {
  onSelfGetter: {
    get: onSelfGetter,
  },
  1: {
    value: 'onSelfIndexed',
    writable: false,
    enumerable: false,
    configurable: true,
  }
});

const ctx = vm.createContext(sandbox);

const result = vm.runInContext(`
Object.prototype.onInnerProto = 'onInnerProto';
Object.defineProperties(Object.prototype, {
  onInnerProtoGetter: {
    get() {
      return 'onInnerProtoGetter';
    },
  },
  2: {
    value: 'onInnerProtoIndexed',
    writable: false,
    enumerable: false,
    configurable: true,
  },
});

// Override outer proto properties
Object.prototype.bothProto = 'onInnerProto';
Object.defineProperties(Object.prototype, {
  bothProtoGetter: {
    get() {
      return 'onInnerProtoGetter';
    },
  },
  // outer proto indexed
  3: {
    value: 'onInnerProtoIndexed',
    writable: false,
    enumerable: false,
    configurable: true,
  },
});

const resultHasOwn = {
  onSelf: Object.hasOwn(this, 'onSelf'),
  onSelfGetter: Object.hasOwn(this, 'onSelfGetter'),
  onSelfIndexed: Object.hasOwn(this, 1),
  onOuterProto: Object.hasOwn(this, 'onOuterProto'),
  onOuterProtoGetter: Object.hasOwn(this, 'onOuterProtoGetter'),
  onOuterProtoIndexed: Object.hasOwn(this, 0),
  onInnerProto: Object.hasOwn(this, 'onInnerProto'),
  onInnerProtoGetter: Object.hasOwn(this, 'onInnerProtoGetter'),
  onInnerProtoIndexed: Object.hasOwn(this, 2),
  bothProto: Object.hasOwn(this, 'bothProto'),
  bothProtoGetter: Object.hasOwn(this, 'bothProtoGetter'),
  bothProtoIndexed: Object.hasOwn(this, 3),
};

const getDesc = (prop) => Object.getOwnPropertyDescriptor(this, prop);
const resultDesc = {
  onSelf: getDesc('onSelf'),
  onSelfGetter: getDesc('onSelfGetter'),
  onSelfIndexed: getDesc(1),
  onOuterProto: getDesc('onOuterProto'),
  onOuterProtoGetter: getDesc('onOuterProtoGetter'),
  onOuterProtoIndexed: getDesc(0),
  onInnerProto: getDesc('onInnerProto'),
  onInnerProtoGetter: getDesc('onInnerProtoGetter'),
  onInnerProtoIndexed: getDesc(2),
  bothProto: getDesc('bothProto'),
  bothProtoGetter: getDesc('bothProtoGetter'),
  bothProtoIndexed: getDesc(3),
};
const resultIn = {
  onSelf: 'onSelf' in this,
  onSelfGetter: 'onSelfGetter' in this,
  onSelfIndexed: 1 in this,
  onOuterProto: 'onOuterProto' in this,
  onOuterProtoGetter: 'onOuterProtoGetter' in this,
  onOuterProtoIndexed: 0 in this,
  onInnerProto: 'onInnerProto' in this,
  onInnerProtoGetter: 'onInnerProtoGetter' in this,
  onInnerProtoIndexed: 2 in this,
  bothProto: 'bothProto' in this,
  bothProtoGetter: 'bothProtoGetter' in this,
  bothProtoIndexed: 3 in this,
};
const resultValue = {
  onSelf: this.onSelf,
  onSelfGetter: this.onSelfGetter,
  onSelfIndexed: this[1],
  onOuterProto: this.onOuterProto,
  onOuterProtoGetter: this.onOuterProtoGetter,
  onOuterProtoIndexed: this[0],
  onInnerProto: this.onInnerProto,
  onInnerProtoGetter: this.onInnerProtoGetter,
  onInnerProtoIndexed: this[2],
  bothProto: this.bothProto,
  bothProtoGetter: this.bothProtoGetter,
  bothProtoIndexed: this[3],
};
({
  resultHasOwn,
  resultDesc,
  resultIn,
  resultValue,
});
`, ctx);

// eslint-disable-next-line no-restricted-properties
assert.deepEqual(result, {
  resultHasOwn: {
    onSelf: true,
    onSelfGetter: true,
    onSelfIndexed: true,

    // All prototype properties are not own properties.
    onOuterProto: false,
    onOuterProtoGetter: false,
    onOuterProtoIndexed: false,
    onInnerProto: false,
    onInnerProtoGetter: false,
    onInnerProtoIndexed: false,
    bothProto: false,
    bothProtoGetter: false,
    bothProtoIndexed: false,
  },
  resultDesc: {
    onSelf: { value: 'onSelf', writable: true, enumerable: true, configurable: true },
    onSelfGetter: { get: onSelfGetter, set: undefined, enumerable: false, configurable: false },
    onSelfIndexed: { value: 'onSelfIndexed', writable: false, enumerable: false, configurable: true },

    // All prototype properties are not own properties.
    onOuterProto: undefined,
    onOuterProtoGetter: undefined,
    onOuterProtoIndexed: undefined,
    onInnerProto: undefined,
    onInnerProtoGetter: undefined,
    onInnerProtoIndexed: undefined,
    bothProto: undefined,
    bothProtoGetter: undefined,
    bothProtoIndexed: undefined,
  },
  resultIn: {
    onSelf: true,
    onSelfGetter: true,
    onSelfIndexed: true,

    // Only properties exist on inner prototype chain will be looked up
    // on `in` operator. In the VM Context, the prototype chain will be like:
    // ```
    // Object
    // ^
    // | prototype
    // InnerPrototype
    // ^
    // | prototype
    // globalThis
    // ```
    // Outer prototype is not in the inner global object prototype chain and it
    // will not be looked up on `in` operator.
    onOuterProto: false,
    onOuterProtoGetter: false,
    onOuterProtoIndexed: false,
    onInnerProto: true,
    onInnerProtoGetter: true,
    onInnerProtoIndexed: true,
    bothProto: true,
    bothProtoGetter: true,
    bothProtoIndexed: true,
  },
  resultValue: {
    onSelf: 'onSelf',
    onSelfGetter: 'onSelfGetter',
    onSelfIndexed: 'onSelfIndexed',

    // FIXME(legendecas): The outer prototype is not observable from the inner
    // vm. Allowing property getter on the outer prototype can be confusing
    // comparing to the normal JavaScript objects.
    // Additionally, this may expose unexpected properties on the outer
    // prototype chain, like polyfills, to the vm context.
    onOuterProto: 'onOuterProto',
    onOuterProtoGetter: 'onOuterProtoGetter',
    onOuterProtoIndexed: 'onOuterProtoIndexed',
    onInnerProto: 'onInnerProto',
    onInnerProtoGetter: 'onInnerProtoGetter',
    onInnerProtoIndexed: 'onInnerProtoIndexed',
    bothProto: 'onOuterProto',
    bothProtoGetter: 'onOuterProtoGetter',
    bothProtoIndexed: 'onOuterProtoIndexed',
  },
});
