// Adapted from SES/Caja - Copyright (C) 2011 Google Inc.
// Copyright (C) 2018 Agoric

// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// SPDX-License-Identifier: Apache-2.0

// Based upon:
// https://github.com/google/caja/blob/HEAD/src/com/google/caja/ses/startSES.js
// https://github.com/google/caja/blob/HEAD/src/com/google/caja/ses/repairES5.js
// https://github.com/tc39/proposal-ses/blob/e5271cc42a257a05dcae2fd94713ed2f46c08620/shim/src/freeze.js

'use strict';

const {
  AggregateError,
  AggregateErrorPrototype,
  Array,
  ArrayBuffer,
  ArrayBufferPrototype,
  ArrayIteratorPrototype,
  ArrayPrototype,
  ArrayPrototypeForEach,
  ArrayPrototypePush,
  Atomics,
  BigInt,
  BigInt64Array,
  BigInt64ArrayPrototype,
  BigIntPrototype,
  BigUint64Array,
  BigUint64ArrayPrototype,
  Boolean,
  BooleanPrototype,
  DataView,
  DataViewPrototype,
  Date,
  DatePrototype,
  Error,
  ErrorPrototype,
  EvalError,
  EvalErrorPrototype,
  FinalizationRegistry,
  FinalizationRegistryPrototype,
  Float32Array,
  Float32ArrayPrototype,
  Float64Array,
  Float64ArrayPrototype,
  Function,
  FunctionPrototype,
  Int16Array,
  Int16ArrayPrototype,
  Int32Array,
  Int32ArrayPrototype,
  Int8Array,
  Int8ArrayPrototype,
  IteratorPrototype,
  Map,
  MapPrototype,
  Number,
  NumberPrototype,
  Object,
  ObjectDefineProperty,
  ObjectFreeze,
  ObjectGetOwnPropertyDescriptor,
  ObjectGetOwnPropertyDescriptors,
  ObjectGetOwnPropertyNames,
  ObjectGetOwnPropertySymbols,
  ObjectGetPrototypeOf,
  ObjectPrototype,
  ObjectPrototypeHasOwnProperty,
  Promise,
  PromisePrototype,
  Proxy,
  RangeError,
  RangeErrorPrototype,
  ReferenceError,
  ReferenceErrorPrototype,
  ReflectOwnKeys,
  RegExp,
  RegExpPrototype,
  SafeSet,
  Set,
  SetPrototype,
  String,
  StringIteratorPrototype,
  StringPrototype,
  Symbol,
  SymbolIterator,
  SymbolMatchAll,
  SymbolPrototype,
  SyntaxError,
  SyntaxErrorPrototype,
  TypeError,
  TypeErrorPrototype,
  TypedArray,
  TypedArrayPrototype,
  URIError,
  URIErrorPrototype,
  Uint16Array,
  Uint16ArrayPrototype,
  Uint32Array,
  Uint32ArrayPrototype,
  Uint8Array,
  Uint8ArrayPrototype,
  Uint8ClampedArray,
  Uint8ClampedArrayPrototype,
  WeakMap,
  WeakMapPrototype,
  WeakRef,
  WeakRefPrototype,
  WeakSet,
  WeakSetPrototype,
  decodeURI,
  decodeURIComponent,
  encodeURI,
  encodeURIComponent,
  escape,
  globalThis,
  unescape,
} = primordials;

const {
  Intl,
  SharedArrayBuffer,
  WebAssembly,
} = globalThis;

module.exports = function() {
  const { Console } = require('internal/console/constructor');
  const console = require('internal/console/global');
  const {
    clearImmediate,
    clearInterval,
    clearTimeout,
    setImmediate,
    setInterval,
    setTimeout,
  } = require('timers');

  const intrinsicPrototypes = [
    // 20 Fundamental Objects
    ObjectPrototype, // 20.1
    FunctionPrototype, // 20.2
    BooleanPrototype, // 20.3
    SymbolPrototype, // 20.4

    ErrorPrototype, // 20.5
    AggregateErrorPrototype,
    EvalErrorPrototype,
    RangeErrorPrototype,
    ReferenceErrorPrototype,
    SyntaxErrorPrototype,
    TypeErrorPrototype,
    URIErrorPrototype,

    // 21 Numbers and Dates
    NumberPrototype, // 21.1
    BigIntPrototype, // 21.2
    DatePrototype, // 21.4

    // 22 Text Processing
    StringPrototype, // 22.1
    StringIteratorPrototype, // 22.1.5
    RegExpPrototype, // 22.2
    // 22.2.7 RegExpStringIteratorPrototype
    ObjectGetPrototypeOf(/e/[SymbolMatchAll]()),

    // 23 Indexed Collections
    ArrayPrototype, // 23.1
    ArrayIteratorPrototype, // 23.1.5

    TypedArrayPrototype, // 23.2
    Int8ArrayPrototype,
    Uint8ArrayPrototype,
    Uint8ClampedArrayPrototype,
    Int16ArrayPrototype,
    Uint16ArrayPrototype,
    Int32ArrayPrototype,
    Uint32ArrayPrototype,
    Float32ArrayPrototype,
    Float64ArrayPrototype,
    BigInt64ArrayPrototype,
    BigUint64ArrayPrototype,

    // 24 Keyed Collections
    MapPrototype, // 24.1
    // 24.1.5 MapIteratorPrototype
    ObjectGetPrototypeOf(new Map()[SymbolIterator]()),
    SetPrototype, // 24.2
    // 24.2.5 SetIteratorPrototype
    ObjectGetPrototypeOf(new Set()[SymbolIterator]()),
    WeakMapPrototype, // 24.3
    WeakSetPrototype, // 24.4

    // 25 Structured Data
    ArrayBufferPrototype, // 25.1
    DataViewPrototype, // 25.3

    // 26 Managing Memory
    WeakRefPrototype, // 26.1
    FinalizationRegistryPrototype, // 26.2

    // 27 Control Abstraction Objects
    // 27.1 Iteration
    IteratorPrototype, // 27.1.2 IteratorPrototype
    // 27.1.3 AsyncIteratorPrototype
    ObjectGetPrototypeOf(ObjectGetPrototypeOf(ObjectGetPrototypeOf(
      (async function*() {})(),
    ))),
    PromisePrototype, // 27.2

    // Other APIs / Web Compatibility
    Console.prototype,
  ];
  const intrinsics = [
    // 10.2.4.1 ThrowTypeError
    ObjectGetOwnPropertyDescriptor(FunctionPrototype, 'caller').get,

    // 19 The Global Object
    // 19.2 Function Properties of the Global Object
    // eslint-disable-next-line node-core/prefer-primordials
    eval,
    // eslint-disable-next-line node-core/prefer-primordials
    isFinite,
    // eslint-disable-next-line node-core/prefer-primordials
    isNaN,
    // eslint-disable-next-line node-core/prefer-primordials
    parseFloat,
    // eslint-disable-next-line node-core/prefer-primordials
    parseInt,
    // 19.2.6 URI Handling Functions
    decodeURI,
    decodeURIComponent,
    encodeURI,
    encodeURIComponent,

    // 20 Fundamental Objects
    Object, // 20.1
    Function, // 20.2
    Boolean, // 20.3
    Symbol, // 20.4

    Error, // 20.5
    AggregateError,
    EvalError,
    RangeError,
    ReferenceError,
    SyntaxError,
    TypeError,
    URIError,

    // 21 Numbers and Dates
    Number, // 21.1
    BigInt, // 21.2
    // eslint-disable-next-line node-core/prefer-primordials
    Math, // 21.3
    Date, // 21.4

    // 22 Text Processing
    String, // 22.1
    StringIteratorPrototype, // 22.1.5
    RegExp, // 22.2
    // 22.2.7 RegExpStringIteratorPrototype
    ObjectGetPrototypeOf(/e/[SymbolMatchAll]()),

    // 23 Indexed Collections
    Array, // 23.1
    ArrayIteratorPrototype, // 23.1.5
    // 23.2 TypedArray
    TypedArray,
    Int8Array,
    Uint8Array,
    Uint8ClampedArray,
    Int16Array,
    Uint16Array,
    Int32Array,
    Uint32Array,
    Float32Array,
    Float64Array,
    BigInt64Array,
    BigUint64Array,

    // 24 Keyed Collections
    Map, // 24.1
    // 24.1.5 MapIteratorPrototype
    ObjectGetPrototypeOf(new Map()[SymbolIterator]()),
    Set, // 24.2
    // 24.2.5 SetIteratorPrototype
    ObjectGetPrototypeOf(new Set()[SymbolIterator]()),
    WeakMap, // 24.3
    WeakSet, // 24.4

    // 25 Structured Data
    ArrayBuffer, // 25.1
    DataView, // 25.3
    Atomics, // 25.4
    // eslint-disable-next-line node-core/prefer-primordials
    JSON, // 25.5

    // 26 Managing Memory
    WeakRef, // 26.1
    FinalizationRegistry, // 26.2

    // 27 Control Abstraction Objects
    // 27.1 Iteration
    ObjectGetPrototypeOf(ArrayIteratorPrototype), // 27.1.2 IteratorPrototype
    // 27.1.3 AsyncIteratorPrototype
    ObjectGetPrototypeOf(ObjectGetPrototypeOf(ObjectGetPrototypeOf(
      (async function*() {})(),
    ))),
    Promise, // 27.2
    // 27.3 GeneratorFunction
    ObjectGetPrototypeOf(function* () {}),
    // 27.4 AsyncGeneratorFunction
    ObjectGetPrototypeOf(async function* () {}),
    // 27.7 AsyncFunction
    ObjectGetPrototypeOf(async function() {}),

    // 28 Reflection
    // eslint-disable-next-line node-core/prefer-primordials
    Reflect, // 28.1
    Proxy, // 28.2

    // B.2.1
    escape,
    unescape,

    // Other APIs / Web Compatibility
    clearImmediate,
    clearInterval,
    clearTimeout,
    setImmediate,
    setInterval,
    setTimeout,
    console,
  ];

  if (typeof SharedArrayBuffer !== 'undefined') { // 25.2
    ArrayPrototypePush(intrinsicPrototypes, SharedArrayBuffer.prototype);
    ArrayPrototypePush(intrinsics, SharedArrayBuffer);
  }

  if (typeof WebAssembly !== 'undefined') {
    ArrayPrototypePush(intrinsicPrototypes,
                       WebAssembly.Module.prototype,
                       WebAssembly.Instance.prototype,
                       WebAssembly.Table.prototype,
                       WebAssembly.Memory.prototype,
                       WebAssembly.CompileError.prototype,
                       WebAssembly.LinkError.prototype,
                       WebAssembly.RuntimeError.prototype,
    );
    ArrayPrototypePush(intrinsics, WebAssembly);
  }

  if (typeof Intl !== 'undefined') {
    ArrayPrototypePush(intrinsicPrototypes,
                       Intl.Collator.prototype,
                       Intl.DateTimeFormat.prototype,
                       Intl.ListFormat.prototype,
                       Intl.NumberFormat.prototype,
                       Intl.PluralRules.prototype,
                       Intl.RelativeTimeFormat.prototype,
    );
    ArrayPrototypePush(intrinsics, Intl);
  }

  ArrayPrototypeForEach(intrinsicPrototypes, enableDerivedOverrides);

  const frozenSet = new WeakSet();
  ArrayPrototypeForEach(intrinsics, deepFreeze);

  // 19.1 Value Properties of the Global Object
  ObjectDefineProperty(globalThis, 'globalThis', {
    __proto__: null,
    configurable: false,
    writable: false,
    value: globalThis,
  });

  // Objects that are deeply frozen.
  function deepFreeze(root) {
    /**
     * "innerDeepFreeze()" acts like "Object.freeze()", except that:
     *
     * To deepFreeze an object is to freeze it and all objects transitively
     * reachable from it via transitive reflective property and prototype
     * traversal.
     */
    function innerDeepFreeze(node) {
      // Objects that we have frozen in this round.
      const freezingSet = new SafeSet();

      // If val is something we should be freezing but aren't yet,
      // add it to freezingSet.
      function enqueue(val) {
        if (Object(val) !== val) {
          // ignore primitives
          return;
        }
        const type = typeof val;
        if (type !== 'object' && type !== 'function') {
          // NB: handle for any new cases in future
        }
        if (frozenSet.has(val) || freezingSet.has(val)) {
          // TODO: Use uncurried form
          // Ignore if already frozen or freezing
          return;
        }
        freezingSet.add(val); // TODO: Use uncurried form
      }

      function doFreeze(obj) {
        // Immediately freeze the object to ensure reactive
        // objects such as proxies won't add properties
        // during traversal, before they get frozen.

        // Object are verified before being enqueued,
        // therefore this is a valid candidate.
        // Throws if this fails (strict mode).
        ObjectFreeze(obj);

        // We rely upon certain commitments of Object.freeze and proxies here

        // Get stable/immutable outbound links before a Proxy has a chance to do
        // something sneaky.
        const proto = ObjectGetPrototypeOf(obj);
        const descs = ObjectGetOwnPropertyDescriptors(obj);
        enqueue(proto);
        ArrayPrototypeForEach(ReflectOwnKeys(descs), (name) => {
          const desc = descs[name];
          if (ObjectPrototypeHasOwnProperty(desc, 'value')) {
            // todo uncurried form
            enqueue(desc.value);
          } else {
            enqueue(desc.get);
            enqueue(desc.set);
          }
        });
      }

      function dequeue() {
        // New values added before forEach() has finished will be visited.
        freezingSet.forEach(doFreeze); // TODO: Curried forEach
      }

      function commit() {
        // TODO: Curried forEach
        // We capture the real WeakSet.prototype.add above, in case someone
        // changes it. The two-argument form of forEach passes the second
        // argument as the 'this' binding, so we add to the correct set.
        freezingSet.forEach(frozenSet.add, frozenSet);
      }

      enqueue(node);
      dequeue();
      commit();
    }

    innerDeepFreeze(root);
    return root;
  }

  /**
   * For a special set of properties (defined below), it ensures that the
   * effect of freezing does not suppress the ability to override these
   * properties on derived objects by simple assignment.
   *
   * Because of lack of sufficient foresight at the time, ES5 unfortunately
   * specified that a simple assignment to a non-existent property must fail if
   * it would override a non-writable data property of the same name. (In
   * retrospect, this was a mistake, but it is now too late and we must live
   * with the consequences.) As a result, simply freezing an object to make it
   * tamper proof has the unfortunate side effect of breaking previously correct
   * code that is considered to have followed JS best practices, if this
   * previous code used assignment to override.
   *
   * To work around this mistake, deepFreeze(), prior to freezing, replaces
   * selected configurable own data properties with accessor properties which
   * simulate what we should have specified -- that assignments to derived
   * objects succeed if otherwise possible.
   */
  function enableDerivedOverride(obj, prop, desc) {
    if (ObjectPrototypeHasOwnProperty(desc, 'value') && desc.configurable) {
      const value = desc.value;

      function getter() {
        return value;
      }

      // Re-attach the data property on the object so
      // it can be found by the deep-freeze traversal process.
      getter.value = value;

      function setter(newValue) {
        if (obj === this) {
          // eslint-disable-next-line no-restricted-syntax
          throw new TypeError(
            `Cannot assign to read only property '${prop}' of object '${obj}'`,
          );
        }
        if (ObjectPrototypeHasOwnProperty(this, prop)) {
          this[prop] = newValue;
        } else {
          ObjectDefineProperty(this, prop, {
            __proto__: null,
            value: newValue,
            writable: true,
            enumerable: true,
            configurable: true,
          });
        }
      }

      ObjectDefineProperty(obj, prop, {
        __proto__: null,
        get: getter,
        set: setter,
        enumerable: desc.enumerable,
        configurable: desc.configurable,
      });
    }
  }

  function enableDerivedOverrides(obj) {
    if (!obj) {
      return;
    }
    const descs = ObjectGetOwnPropertyDescriptors(obj);
    if (!descs) {
      return;
    }
    ArrayPrototypeForEach(ObjectGetOwnPropertyNames(obj), (prop) => {
      return enableDerivedOverride(obj, prop, descs[prop]);
    });
    ArrayPrototypeForEach(ObjectGetOwnPropertySymbols(obj), (prop) => {
      return enableDerivedOverride(obj, prop, descs[prop]);
    });
  }
};
