// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is intended for permanent JS behavior changes for mocking out
// non-deterministic behavior. For temporary suppressions, please refer to
// v8_suppressions.js.
// This file is loaded before each correctness test cases and won't get
// minimized.


// This will be overridden in the test cases. The override can be minimized.
var prettyPrinted = function prettyPrinted(msg) { return msg; };

// Mock Math.random.
(function() {
  let index = 1;
  Math.random = function() {
      const x = Math.sin(index++) * 10000;
      return x - Math.floor(x);
  }
})();

// Mock Math.pow. Work around an optimization for -0.5.
(function() {
  const origMathPow = Math.pow;
  Math.pow = function(a, b) {
    if (b === -0.5) {
      return 0;
    } else {
      return origMathPow(a, b);
    }
  }
})();


// Mock Date.
(function() {
  let index = 0;
  let mockDate = 1477662728696;
  const mockDateNow = function() {
    index = (index + 1) % 10;
    mockDate = mockDate + index + 1;
    return mockDate;
  }

  const origDate = Date;
  const construct = Reflect.construct;
  const constructDate = function(args) {
    let result;
    if (args.length) {
      result = construct(origDate, args);
    } else {
      result = new origDate(mockDateNow());
    }
    result.constructor = function(...args) { return constructDate(args); }
    Object.defineProperty(
        result, "constructor", { configurable: false, writable: false });
    return result;
  }

  origDate.prototype.constructor = function(...args) {
    return constructDate(args);
  };

  var handler = {
    apply: function(target, thisArg, args) {
      return constructDate(args);
    },
    construct: function(target, args, newTarget) {
      return constructDate(args);
    },
    get: function(target, property, receiver) {
      if (property == "now") {
        return mockDateNow;
      }
      if (property == "prototype") {
        return origDate.prototype;
      }
    },
  }

  Date = new Proxy(Date, handler);
})();

// Mock performance methods.
performance.now = function() { return 1.2; };
performance.mark = function() { return undefined; };
performance.measure = function() { return undefined; };
performance.measureMemory = function() { return []; };

// Mock readline so that test cases don't hang.
readline = function() { return "foo"; };

// Mock stack traces.
Error.prepareStackTrace = function(error, structuredStackTrace) {
  return "";
};
Object.defineProperty(
    Error, 'prepareStackTrace', { configurable: false, writable: false });

// Mock buffer access in float typed arrays because of varying NaN patterns.
(function() {
  const origArrayFrom = Array.from;
  const origArrayIsArray = Array.isArray;
  const origFunctionPrototype = Function.prototype;
  const origArrayMap = Array.prototype.map;
  const applyOrigArrayMap = origFunctionPrototype.apply.bind(origArrayMap);
  const origIsNaN = isNaN;
  const origIterator = Symbol.iterator;
  const deNaNify = function(value) { return origIsNaN(value) ? 1 : value; };
  const mock = function(type) {

    // Remove NaN values from parameters to "set" function.
    const set = type.prototype.set;
    type.prototype.set = function(array, offset) {
      if (origArrayIsArray(array)) {
        array = applyOrigArrayMap(array, [deNaNify]);
      }
      set.apply(this, [array, offset]);
    };

    const handler = {
      // Remove NaN values from parameters to constructor.
      construct: function(target, args) {
        for (let i = 0; i < args.length; i++) {
          if (args[i] != null &&
              typeof args[i][origIterator] === 'function') {
            // Consume iterators.
            args[i] = origArrayFrom(args[i]);
          }
          if (origArrayIsArray(args[i])) {
            args[i] = applyOrigArrayMap(args[i], [deNaNify]);
          }
        }

        const obj = new (
            origFunctionPrototype.bind.call(type, null, ...args));
        return new Proxy(obj, {
          get: function(x, prop) {
            if (typeof x[prop] == "function")
              return x[prop].bind(obj);
            return x[prop];
          },
          // Remove NaN values that get assigned.
          set: function(target, prop, value, receiver) {
            target[prop] = deNaNify(value);
            return value;
          }
        });
      },
    };
    return new Proxy(type, handler);
  }

  Float32Array = mock(Float32Array);
  Float64Array = mock(Float64Array);
})();

// Mock buffer access via DataViews because of varying NaN patterns.
(function() {
  const origIsNaN = isNaN;
  const deNaNify = function(value) { return origIsNaN(value) ? 1 : value; };
  const origSetFloat32 = DataView.prototype.setFloat32;
  DataView.prototype.setFloat32 = function(offset, value, ...rest) {
    origSetFloat32.call(this, offset, deNaNify(value), ...rest);
  };
  const origSetFloat64 = DataView.prototype.setFloat64;
  DataView.prototype.setFloat64 = function(offset, value, ...rest) {
    origSetFloat64.call(this, offset, deNaNify(value), ...rest);
  };
})();

// Mock maximum typed-array buffer and limit to 1MiB. Otherwise we might
// get range errors. We ignore those by crashing, but that reduces coverage,
// hence, let's reduce the range-error rate.
(function() {
  // Math.min might be manipulated in test cases.
  const min = Math.min;
  const maxBytes = 1048576;
  const mock = function(type) {
    const maxLength = maxBytes / (type.BYTES_PER_ELEMENT || 1);
    const handler = {
      construct: function(target, args) {
        if (args[0] && typeof args[0] != "object") {
          // Length used as first argument.
          args[0] = min(maxLength, Number(args[0]));
        } else if (args[0] instanceof ArrayBuffer && args.length > 1) {
          // Buffer used as first argument.
          const buffer = args[0];
          args[1] = Number(args[1]);
          // Ensure offset is multiple of bytes per element.
          args[1] = args[1] - (args[1] % type.BYTES_PER_ELEMENT);
          // Limit offset to length of buffer.
          args[1] = min(args[1], buffer.byteLength || 0);
          if (args.length > 2) {
            // If also length is given, limit it to the maximum that's possible
            // given buffer and offset. Avoid NaN offset turning the length
            // NaN, too.
            const maxBytesLeft = buffer.byteLength - (args[1] || 0);
            const maxLengthLeft = maxBytesLeft / type.BYTES_PER_ELEMENT;
            args[2] = min(Number(args[2]), maxLengthLeft);
          }
        }
        return new (Function.prototype.bind.apply(type, [null].concat(args)));
      },
    };
    return new Proxy(type, handler);
  }

  ArrayBuffer = mock(ArrayBuffer);
  SharedArrayBuffer = mock(SharedArrayBuffer);
  Int8Array = mock(Int8Array);
  Uint8Array = mock(Uint8Array);
  Uint8ClampedArray = mock(Uint8ClampedArray);
  Int16Array = mock(Int16Array);
  Uint16Array = mock(Uint16Array);
  Int32Array = mock(Int32Array);
  Uint32Array = mock(Uint32Array);
  BigInt64Array = mock(BigInt64Array);
  BigUint64Array = mock(BigUint64Array);
  Float32Array = mock(Float32Array);
  Float64Array = mock(Float64Array);
})();

// Mock typed array set function and cap offset to not throw a range error.
(function() {
  // Math.min might be manipulated in test cases.
  const min = Math.min;
  const types = [
    Int8Array,
    Uint8Array,
    Uint8ClampedArray,
    Int16Array,
    Uint16Array,
    Int32Array,
    Uint32Array,
    BigInt64Array,
    BigUint64Array,
    Float32Array,
    Float64Array,
  ];
  for (const type of types) {
    const set = type.prototype.set;
    type.prototype.set = function(array, offset) {
      if (Array.isArray(array)) {
        offset = Number(offset);
        offset = min(offset, this.length - array.length);
      }
      set.call(this, array, offset);
    };
  }
})();

// Mock Worker.
(function() {
  let index = 0;
  // TODO(machenbach): Randomize this for each test case, but keep stable
  // during comparison. Also data and random above.
  const workerMessages = [
    undefined, 0, -1, "", "foo", 42, [], {}, [0], {"x": 0}
  ];
  Worker = function(code){
    try {
      print(prettyPrinted(eval(code)));
    } catch(e) {
      print(prettyPrinted(e));
    }
    this.getMessage = function(){
      index = (index + 1) % 10;
      return workerMessages[index];
    }
    this.postMessage = function(msg){
      print(prettyPrinted(msg));
    }
  };
})();

// Mock Realm.
Realm.eval = function(realm, code) { return eval(code) };

// Mock the nondeterministic parts of WeakRef and FinalizationRegistry.
WeakRef.prototype.deref = function() { };
FinalizationRegistry = function(callback) { };
FinalizationRegistry.prototype.register = function(target, holdings) { };
FinalizationRegistry.prototype.unregister = function(unregisterToken) { };
FinalizationRegistry.prototype.cleanupSome = function() { };
FinalizationRegistry.prototype[Symbol.toStringTag] = "FinalizationRegistry";

// Mock the nondeterministic Atomics.waitAsync.
Atomics.waitAsync = function() {
  // Return a mock "Promise" whose "then" function will call the callback
  // immediately.
  return {'value': {'then': function (f) { f(); }}};
}

// Mock serializer API with no-ops.
d8.serializer = {'serialize': (x) => x, 'deserialize': (x) => x}
