// Originally from narwhal.js (http://narwhaljs.org)
// Copyright (c) 2009 Thomas Robinson <280north.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the 'Software'), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

'use strict';

const {
  ArrayBufferIsView,
  ArrayBufferPrototypeGetByteLength,
  ArrayFrom,
  ArrayIsArray,
  ArrayPrototypeIndexOf,
  ArrayPrototypeJoin,
  ArrayPrototypePush,
  ArrayPrototypeSlice,
  DataViewPrototypeGetBuffer,
  DataViewPrototypeGetByteLength,
  DataViewPrototypeGetByteOffset,
  Error,
  FunctionPrototypeCall,
  MapPrototypeGet,
  MapPrototypeGetSize,
  MapPrototypeHas,
  NumberIsNaN,
  ObjectAssign,
  ObjectIs,
  ObjectKeys,
  ObjectPrototypeIsPrototypeOf,
  ObjectPrototypeToString,
  ReflectApply,
  ReflectHas,
  ReflectOwnKeys,
  RegExpPrototypeExec,
  SafeArrayIterator,
  SafeMap,
  SafeSet,
  SafeWeakSet,
  SetPrototypeGetSize,
  String,
  StringPrototypeIndexOf,
  StringPrototypeSlice,
  StringPrototypeSplit,
  Symbol,
  SymbolIterator,
  TypedArrayPrototypeGetLength,
  Uint8Array,
} = primordials;

const {
  codes: {
    ERR_AMBIGUOUS_ARGUMENT,
    ERR_INVALID_ARG_TYPE,
    ERR_INVALID_ARG_VALUE,
    ERR_INVALID_RETURN_VALUE,
    ERR_MISSING_ARGS,
  },
} = require('internal/errors');
const AssertionError = require('internal/assert/assertion_error');
const { inspect } = require('internal/util/inspect');
const { Buffer } = require('buffer');
const {
  isArrayBuffer,
  isDataView,
  isKeyObject,
  isPromise,
  isRegExp,
  isMap,
  isSet,
  isDate,
  isWeakSet,
  isWeakMap,
  isSharedArrayBuffer,
  isAnyArrayBuffer,
} = require('internal/util/types');
const { isError, deprecate, emitExperimentalWarning } = require('internal/util');
const { innerOk } = require('internal/assert/utils');

const CallTracker = require('internal/assert/calltracker');
const {
  validateFunction,
} = require('internal/validators');
const { isURL } = require('internal/url');

let isDeepEqual;
let isDeepStrictEqual;

function lazyLoadComparison() {
  const comparison = require('internal/util/comparisons');
  isDeepEqual = comparison.isDeepEqual;
  isDeepStrictEqual = comparison.isDeepStrictEqual;
}

let warned = false;

// The assert module provides functions that throw
// AssertionError's when particular conditions are not met. The
// assert module must conform to the following interface.

const assert = module.exports = ok;

const NO_EXCEPTION_SENTINEL = {};

// All of the following functions must throw an AssertionError
// when a corresponding condition is not met, with a message that
// may be undefined if not provided. All assertion methods provide
// both the actual and expected values to the assertion error for
// display purposes.

function innerFail(obj) {
  if (obj.message instanceof Error) throw obj.message;

  throw new AssertionError(obj);
}

/**
 * @param {any} actual
 * @param {any} expected
 * @param {string | Error} [message]
 * @param {string} [operator]
 * @param {Function} [stackStartFn]
 */
function fail(actual, expected, message, operator, stackStartFn) {
  const argsLen = arguments.length;

  let internalMessage = false;
  if (actual == null && argsLen <= 1) {
    internalMessage = true;
    message = 'Failed';
  } else if (argsLen === 1) {
    message = actual;
    actual = undefined;
  } else {
    if (warned === false) {
      warned = true;
      process.emitWarning(
        'assert.fail() with more than one argument is deprecated. ' +
          'Please use assert.strictEqual() instead or only pass a message.',
        'DeprecationWarning',
        'DEP0094',
      );
    }
    if (argsLen === 2)
      operator = '!=';
  }

  if (message instanceof Error) throw message;

  const errArgs = {
    actual,
    expected,
    operator: operator === undefined ? 'fail' : operator,
    stackStartFn: stackStartFn || fail,
    message,
  };
  const err = new AssertionError(errArgs);
  if (internalMessage) {
    err.generatedMessage = true;
  }
  throw err;
}

assert.fail = fail;

// The AssertionError is defined in internal/error.
assert.AssertionError = AssertionError;

/**
 * Pure assertion tests whether a value is truthy, as determined
 * by !!value.
 * @param {...any} args
 * @returns {void}
 */
function ok(...args) {
  innerOk(ok, args.length, ...args);
}
assert.ok = ok;

/**
 * The equality assertion tests shallow, coercive equality with ==.
 * @param {any} actual
 * @param {any} expected
 * @param {string | Error} [message]
 * @returns {void}
 */
/* eslint-disable no-restricted-properties */
assert.equal = function equal(actual, expected, message) {
  if (arguments.length < 2) {
    throw new ERR_MISSING_ARGS('actual', 'expected');
  }
  // eslint-disable-next-line eqeqeq
  if (actual != expected && (!NumberIsNaN(actual) || !NumberIsNaN(expected))) {
    innerFail({
      actual,
      expected,
      message,
      operator: '==',
      stackStartFn: equal,
    });
  }
};

/**
 * The non-equality assertion tests for whether two objects are not
 * equal with !=.
 * @param {any} actual
 * @param {any} expected
 * @param {string | Error} [message]
 * @returns {void}
 */
assert.notEqual = function notEqual(actual, expected, message) {
  if (arguments.length < 2) {
    throw new ERR_MISSING_ARGS('actual', 'expected');
  }
  // eslint-disable-next-line eqeqeq
  if (actual == expected || (NumberIsNaN(actual) && NumberIsNaN(expected))) {
    innerFail({
      actual,
      expected,
      message,
      operator: '!=',
      stackStartFn: notEqual,
    });
  }
};

/**
 * The deep equivalence assertion tests a deep equality relation.
 * @param {any} actual
 * @param {any} expected
 * @param {string | Error} [message]
 * @returns {void}
 */
assert.deepEqual = function deepEqual(actual, expected, message) {
  if (arguments.length < 2) {
    throw new ERR_MISSING_ARGS('actual', 'expected');
  }
  if (isDeepEqual === undefined) lazyLoadComparison();
  if (!isDeepEqual(actual, expected)) {
    innerFail({
      actual,
      expected,
      message,
      operator: 'deepEqual',
      stackStartFn: deepEqual,
    });
  }
};

/**
 * The deep non-equivalence assertion tests for any deep inequality.
 * @param {any} actual
 * @param {any} expected
 * @param {string | Error} [message]
 * @returns {void}
 */
assert.notDeepEqual = function notDeepEqual(actual, expected, message) {
  if (arguments.length < 2) {
    throw new ERR_MISSING_ARGS('actual', 'expected');
  }
  if (isDeepEqual === undefined) lazyLoadComparison();
  if (isDeepEqual(actual, expected)) {
    innerFail({
      actual,
      expected,
      message,
      operator: 'notDeepEqual',
      stackStartFn: notDeepEqual,
    });
  }
};
/* eslint-enable */

/**
 * The deep strict equivalence assertion tests a deep strict equality
 * relation.
 * @param {any} actual
 * @param {any} expected
 * @param {string | Error} [message]
 * @returns {void}
 */
assert.deepStrictEqual = function deepStrictEqual(actual, expected, message) {
  if (arguments.length < 2) {
    throw new ERR_MISSING_ARGS('actual', 'expected');
  }
  if (isDeepEqual === undefined) lazyLoadComparison();
  if (!isDeepStrictEqual(actual, expected)) {
    innerFail({
      actual,
      expected,
      message,
      operator: 'deepStrictEqual',
      stackStartFn: deepStrictEqual,
    });
  }
};

/**
 * The deep strict non-equivalence assertion tests for any deep strict
 * inequality.
 * @param {any} actual
 * @param {any} expected
 * @param {string | Error} [message]
 * @returns {void}
 */
assert.notDeepStrictEqual = notDeepStrictEqual;
function notDeepStrictEqual(actual, expected, message) {
  if (arguments.length < 2) {
    throw new ERR_MISSING_ARGS('actual', 'expected');
  }
  if (isDeepEqual === undefined) lazyLoadComparison();
  if (isDeepStrictEqual(actual, expected)) {
    innerFail({
      actual,
      expected,
      message,
      operator: 'notDeepStrictEqual',
      stackStartFn: notDeepStrictEqual,
    });
  }
}

/**
 * The strict equivalence assertion tests a strict equality relation.
 * @param {any} actual
 * @param {any} expected
 * @param {string | Error} [message]
 * @returns {void}
 */
assert.strictEqual = function strictEqual(actual, expected, message) {
  if (arguments.length < 2) {
    throw new ERR_MISSING_ARGS('actual', 'expected');
  }
  if (!ObjectIs(actual, expected)) {
    innerFail({
      actual,
      expected,
      message,
      operator: 'strictEqual',
      stackStartFn: strictEqual,
    });
  }
};

/**
 * The strict non-equivalence assertion tests for any strict inequality.
 * @param {any} actual
 * @param {any} expected
 * @param {string | Error} [message]
 * @returns {void}
 */
assert.notStrictEqual = function notStrictEqual(actual, expected, message) {
  if (arguments.length < 2) {
    throw new ERR_MISSING_ARGS('actual', 'expected');
  }
  if (ObjectIs(actual, expected)) {
    innerFail({
      actual,
      expected,
      message,
      operator: 'notStrictEqual',
      stackStartFn: notStrictEqual,
    });
  }
};

function isSpecial(obj) {
  return obj == null || typeof obj !== 'object' || isError(obj) || isRegExp(obj) || isDate(obj);
}

const typesToCallDeepStrictEqualWith = [
  isKeyObject, isWeakSet, isWeakMap, Buffer.isBuffer, isSharedArrayBuffer, isURL,
];

function partiallyCompareMaps(actual, expected, comparedObjects) {
  if (MapPrototypeGetSize(expected) > MapPrototypeGetSize(actual)) {
    return false;
  }

  comparedObjects ??= new SafeWeakSet();
  const expectedIterator = FunctionPrototypeCall(SafeMap.prototype[SymbolIterator], expected);

  for (const { 0: key, 1: expectedValue } of expectedIterator) {
    if (!MapPrototypeHas(actual, key)) {
      return false;
    }

    const actualValue = MapPrototypeGet(actual, key);

    if (!compareBranch(actualValue, expectedValue, comparedObjects)) {
      return false;
    }
  }

  return true;
}

function partiallyCompareArrayBuffersOrViews(actual, expected) {
  let actualView, expectedView, expectedViewLength;

  if (!ArrayBufferIsView(actual)) {
    let actualViewLength;

    if (isArrayBuffer(actual) && isArrayBuffer(expected)) {
      actualViewLength = ArrayBufferPrototypeGetByteLength(actual);
      expectedViewLength = ArrayBufferPrototypeGetByteLength(expected);
    } else if (isSharedArrayBuffer(actual) && isSharedArrayBuffer(expected)) {
      actualViewLength = actual.byteLength;
      expectedViewLength = expected.byteLength;
    } else {
      // Cannot compare ArrayBuffers with SharedArrayBuffers
      return false;
    }

    if (expectedViewLength > actualViewLength) {
      return false;
    }
    actualView = new Uint8Array(actual);
    expectedView = new Uint8Array(expected);

  } else if (isDataView(actual)) {
    if (!isDataView(expected)) {
      return false;
    }
    const actualByteLength = DataViewPrototypeGetByteLength(actual);
    expectedViewLength = DataViewPrototypeGetByteLength(expected);
    if (expectedViewLength > actualByteLength) {
      return false;
    }

    actualView = new Uint8Array(
      DataViewPrototypeGetBuffer(actual),
      DataViewPrototypeGetByteOffset(actual),
      actualByteLength,
    );
    expectedView = new Uint8Array(
      DataViewPrototypeGetBuffer(expected),
      DataViewPrototypeGetByteOffset(expected),
      expectedViewLength,
    );
  } else {
    if (ObjectPrototypeToString(actual) !== ObjectPrototypeToString(expected)) {
      return false;
    }
    actualView = actual;
    expectedView = expected;
    expectedViewLength = TypedArrayPrototypeGetLength(expected);

    if (expectedViewLength > TypedArrayPrototypeGetLength(actual)) {
      return false;
    }
  }

  for (let i = 0; i < expectedViewLength; i++) {
    if (!ObjectIs(actualView[i], expectedView[i])) {
      return false;
    }
  }

  return true;
}

function partiallyCompareSets(actual, expected, comparedObjects) {
  if (SetPrototypeGetSize(expected) > SetPrototypeGetSize(actual)) {
    return false; // `expected` can't be a subset if it has more elements
  }

  if (isDeepEqual === undefined) lazyLoadComparison();

  const actualArray = ArrayFrom(FunctionPrototypeCall(SafeSet.prototype[SymbolIterator], actual));
  const expectedIterator = FunctionPrototypeCall(SafeSet.prototype[SymbolIterator], expected);
  const usedIndices = new SafeSet();

  expectedIteration: for (const expectedItem of expectedIterator) {
    for (let actualIdx = 0; actualIdx < actualArray.length; actualIdx++) {
      if (!usedIndices.has(actualIdx) && isDeepStrictEqual(actualArray[actualIdx], expectedItem)) {
        usedIndices.add(actualIdx);
        continue expectedIteration;
      }
    }
    return false;
  }

  return true;
}

const minusZeroSymbol = Symbol('-0');
const zeroSymbol = Symbol('0');

// Helper function to get a unique key for 0, -0 to avoid collisions
function getZeroKey(item) {
  if (item === 0) {
    return ObjectIs(item, -0) ? minusZeroSymbol : zeroSymbol;
  }
  return item;
}

function partiallyCompareArrays(actual, expected, comparedObjects) {
  if (expected.length > actual.length) {
    return false;
  }

  if (isDeepEqual === undefined) lazyLoadComparison();

  // Create a map to count occurrences of each element in the expected array
  const expectedCounts = new SafeMap();
  const safeExpected = new SafeArrayIterator(expected);

  for (const expectedItem of safeExpected) {
    // Check if the item is a zero or a -0, as these need to be handled separately
    if (expectedItem === 0) {
      const zeroKey = getZeroKey(expectedItem);
      expectedCounts.set(zeroKey, (expectedCounts.get(zeroKey)?.count || 0) + 1);
    } else {
      let found = false;
      for (const { 0: key, 1: count } of expectedCounts) {
        if (isDeepStrictEqual(key, expectedItem)) {
          expectedCounts.set(key, count + 1);
          found = true;
          break;
        }
      }
      if (!found) {
        expectedCounts.set(expectedItem, 1);
      }
    }
  }

  const safeActual = new SafeArrayIterator(actual);

  for (const actualItem of safeActual) {
    // Check if the item is a zero or a -0, as these need to be handled separately
    if (actualItem === 0) {
      const zeroKey = getZeroKey(actualItem);

      if (expectedCounts.has(zeroKey)) {
        const count = expectedCounts.get(zeroKey);
        if (count === 1) {
          expectedCounts.delete(zeroKey);
        } else {
          expectedCounts.set(zeroKey, count - 1);
        }
      }
    } else {
      for (const { 0: expectedItem, 1: count } of expectedCounts) {
        if (isDeepStrictEqual(expectedItem, actualItem)) {
          if (count === 1) {
            expectedCounts.delete(expectedItem);
          } else {
            expectedCounts.set(expectedItem, count - 1);
          }
          break;
        }
      }
    }
  }

  return expectedCounts.size === 0;
}

/**
 * Compares two objects or values recursively to check if they are equal.
 * @param {any} actual - The actual value to compare.
 * @param {any} expected - The expected value to compare.
 * @param {Set} [comparedObjects=new Set()] - Set to track compared objects for handling circular references.
 * @returns {boolean} - Returns `true` if the actual value matches the expected value, otherwise `false`.
 * @example
 * compareBranch({a: 1, b: 2, c: 3}, {a: 1, b: 2}); // true
 */
function compareBranch(
  actual,
  expected,
  comparedObjects,
) {
  // Checking for the simplest case possible.
  if (actual === expected) {
    return true;
  }
  // Check for Map object equality
  if (isMap(actual) && isMap(expected)) {
    return partiallyCompareMaps(actual, expected, comparedObjects);
  }

  if (
    ArrayBufferIsView(actual) ||
    isAnyArrayBuffer(actual) ||
    ArrayBufferIsView(expected) ||
    isAnyArrayBuffer(expected)
  ) {
    return partiallyCompareArrayBuffersOrViews(actual, expected);
  }

  for (const type of typesToCallDeepStrictEqualWith) {
    if (type(actual) || type(expected)) {
      if (isDeepStrictEqual === undefined) lazyLoadComparison();
      return isDeepStrictEqual(actual, expected);
    }
  }

  // Check for Set object equality
  if (isSet(actual) && isSet(expected)) {
    return partiallyCompareSets(actual, expected, comparedObjects);
  }

  // Check if expected array is a subset of actual array
  if (ArrayIsArray(actual) && ArrayIsArray(expected)) {
    return partiallyCompareArrays(actual, expected, comparedObjects);
  }

  // Comparison done when at least one of the values is not an object
  if (isSpecial(actual) || isSpecial(expected)) {
    if (isDeepEqual === undefined) {
      lazyLoadComparison();
    }
    return isDeepStrictEqual(actual, expected);
  }

  // Use Reflect.ownKeys() instead of Object.keys() to include symbol properties
  const keysExpected = ReflectOwnKeys(expected);

  comparedObjects ??= new SafeWeakSet();

  // Handle circular references
  if (comparedObjects.has(actual)) {
    return true;
  }
  comparedObjects.add(actual);

  // Check if all expected keys and values match
  for (let i = 0; i < keysExpected.length; i++) {
    const key = keysExpected[i];
    if (!ReflectHas(actual, key)) {
      return false;
    }
    if (!compareBranch(actual[key], expected[key], comparedObjects)) {
      return false;
    }
  }

  return true;
}

/**
 * The strict equivalence assertion test between two objects
 * @param {any} actual
 * @param {any} expected
 * @param {string | Error} [message]
 * @returns {void}
 */
assert.partialDeepStrictEqual = function partialDeepStrictEqual(
  actual,
  expected,
  message,
) {
  emitExperimentalWarning('assert.partialDeepStrictEqual');
  if (arguments.length < 2) {
    throw new ERR_MISSING_ARGS('actual', 'expected');
  }

  if (!compareBranch(actual, expected)) {
    innerFail({
      actual,
      expected,
      message,
      operator: 'partialDeepStrictEqual',
      stackStartFn: partialDeepStrictEqual,
    });
  }
};

class Comparison {
  constructor(obj, keys, actual) {
    for (const key of keys) {
      if (key in obj) {
        if (actual !== undefined &&
            typeof actual[key] === 'string' &&
            isRegExp(obj[key]) &&
            RegExpPrototypeExec(obj[key], actual[key]) !== null) {
          this[key] = actual[key];
        } else {
          this[key] = obj[key];
        }
      }
    }
  }
}

function compareExceptionKey(actual, expected, key, message, keys, fn) {
  if (!(key in actual) || !isDeepStrictEqual(actual[key], expected[key])) {
    if (!message) {
      // Create placeholder objects to create a nice output.
      const a = new Comparison(actual, keys);
      const b = new Comparison(expected, keys, actual);

      const err = new AssertionError({
        actual: a,
        expected: b,
        operator: 'deepStrictEqual',
        stackStartFn: fn,
      });
      err.actual = actual;
      err.expected = expected;
      err.operator = fn.name;
      throw err;
    }
    innerFail({
      actual,
      expected,
      message,
      operator: fn.name,
      stackStartFn: fn,
    });
  }
}

function expectedException(actual, expected, message, fn) {
  let generatedMessage = false;
  let throwError = false;

  if (typeof expected !== 'function') {
    // Handle regular expressions.
    if (isRegExp(expected)) {
      const str = String(actual);
      if (RegExpPrototypeExec(expected, str) !== null)
        return;

      if (!message) {
        generatedMessage = true;
        message = 'The input did not match the regular expression ' +
                  `${inspect(expected)}. Input:\n\n${inspect(str)}\n`;
      }
      throwError = true;
      // Handle primitives properly.
    } else if (typeof actual !== 'object' || actual === null) {
      const err = new AssertionError({
        actual,
        expected,
        message,
        operator: 'deepStrictEqual',
        stackStartFn: fn,
      });
      err.operator = fn.name;
      throw err;
    } else {
      // Handle validation objects.
      const keys = ObjectKeys(expected);
      // Special handle errors to make sure the name and the message are
      // compared as well.
      if (expected instanceof Error) {
        ArrayPrototypePush(keys, 'name', 'message');
      } else if (keys.length === 0) {
        throw new ERR_INVALID_ARG_VALUE('error',
                                        expected, 'may not be an empty object');
      }
      if (isDeepEqual === undefined) lazyLoadComparison();
      for (const key of keys) {
        if (typeof actual[key] === 'string' &&
            isRegExp(expected[key]) &&
            RegExpPrototypeExec(expected[key], actual[key]) !== null) {
          continue;
        }
        compareExceptionKey(actual, expected, key, message, keys, fn);
      }
      return;
    }
  // Guard instanceof against arrow functions as they don't have a prototype.
  // Check for matching Error classes.
  } else if (expected.prototype !== undefined && actual instanceof expected) {
    return;
  } else if (ObjectPrototypeIsPrototypeOf(Error, expected)) {
    if (!message) {
      generatedMessage = true;
      message = 'The error is expected to be an instance of ' +
        `"${expected.name}". Received `;
      if (isError(actual)) {
        const name = (actual.constructor?.name) ||
                     actual.name;
        if (expected.name === name) {
          message += 'an error with identical name but a different prototype.';
        } else {
          message += `"${name}"`;
        }
        if (actual.message) {
          message += `\n\nError message:\n\n${actual.message}`;
        }
      } else {
        message += `"${inspect(actual, { depth: -1 })}"`;
      }
    }
    throwError = true;
  } else {
    // Check validation functions return value.
    const res = ReflectApply(expected, {}, [actual]);
    if (res !== true) {
      if (!message) {
        generatedMessage = true;
        const name = expected.name ? `"${expected.name}" ` : '';
        message = `The ${name}validation function is expected to return` +
          ` "true". Received ${inspect(res)}`;

        if (isError(actual)) {
          message += `\n\nCaught error:\n\n${actual}`;
        }
      }
      throwError = true;
    }
  }

  if (throwError) {
    const err = new AssertionError({
      actual,
      expected,
      message,
      operator: fn.name,
      stackStartFn: fn,
    });
    err.generatedMessage = generatedMessage;
    throw err;
  }
}

function getActual(fn) {
  validateFunction(fn, 'fn');
  try {
    fn();
  } catch (e) {
    return e;
  }
  return NO_EXCEPTION_SENTINEL;
}

function checkIsPromise(obj) {
  // Accept native ES6 promises and promises that are implemented in a similar
  // way. Do not accept thenables that use a function as `obj` and that have no
  // `catch` handler.
  return isPromise(obj) ||
    (obj !== null && typeof obj === 'object' &&
    typeof obj.then === 'function' &&
    typeof obj.catch === 'function');
}

async function waitForActual(promiseFn) {
  let resultPromise;
  if (typeof promiseFn === 'function') {
    // Return a rejected promise if `promiseFn` throws synchronously.
    resultPromise = promiseFn();
    // Fail in case no promise is returned.
    if (!checkIsPromise(resultPromise)) {
      throw new ERR_INVALID_RETURN_VALUE('instance of Promise',
                                         'promiseFn', resultPromise);
    }
  } else if (checkIsPromise(promiseFn)) {
    resultPromise = promiseFn;
  } else {
    throw new ERR_INVALID_ARG_TYPE(
      'promiseFn', ['Function', 'Promise'], promiseFn);
  }

  try {
    await resultPromise;
  } catch (e) {
    return e;
  }
  return NO_EXCEPTION_SENTINEL;
}

function expectsError(stackStartFn, actual, error, message) {
  if (typeof error === 'string') {
    if (arguments.length === 4) {
      throw new ERR_INVALID_ARG_TYPE('error',
                                     ['Object', 'Error', 'Function', 'RegExp'],
                                     error);
    }
    if (typeof actual === 'object' && actual !== null) {
      if (actual.message === error) {
        throw new ERR_AMBIGUOUS_ARGUMENT(
          'error/message',
          `The error message "${actual.message}" is identical to the message.`,
        );
      }
    } else if (actual === error) {
      throw new ERR_AMBIGUOUS_ARGUMENT(
        'error/message',
        `The error "${actual}" is identical to the message.`,
      );
    }
    message = error;
    error = undefined;
  } else if (error != null &&
             typeof error !== 'object' &&
             typeof error !== 'function') {
    throw new ERR_INVALID_ARG_TYPE('error',
                                   ['Object', 'Error', 'Function', 'RegExp'],
                                   error);
  }

  if (actual === NO_EXCEPTION_SENTINEL) {
    let details = '';
    if (error?.name) {
      details += ` (${error.name})`;
    }
    details += message ? `: ${message}` : '.';
    const fnType = stackStartFn === assert.rejects ? 'rejection' : 'exception';
    innerFail({
      actual: undefined,
      expected: error,
      operator: stackStartFn.name,
      message: `Missing expected ${fnType}${details}`,
      stackStartFn,
    });
  }

  if (!error)
    return;

  expectedException(actual, error, message, stackStartFn);
}

function hasMatchingError(actual, expected) {
  if (typeof expected !== 'function') {
    if (isRegExp(expected)) {
      const str = String(actual);
      return RegExpPrototypeExec(expected, str) !== null;
    }
    throw new ERR_INVALID_ARG_TYPE(
      'expected', ['Function', 'RegExp'], expected,
    );
  }
  // Guard instanceof against arrow functions as they don't have a prototype.
  if (expected.prototype !== undefined && actual instanceof expected) {
    return true;
  }
  if (ObjectPrototypeIsPrototypeOf(Error, expected)) {
    return false;
  }
  return ReflectApply(expected, {}, [actual]) === true;
}

function expectsNoError(stackStartFn, actual, error, message) {
  if (actual === NO_EXCEPTION_SENTINEL)
    return;

  if (typeof error === 'string') {
    message = error;
    error = undefined;
  }

  if (!error || hasMatchingError(actual, error)) {
    const details = message ? `: ${message}` : '.';
    const fnType = stackStartFn === assert.doesNotReject ?
      'rejection' : 'exception';
    innerFail({
      actual,
      expected: error,
      operator: stackStartFn.name,
      message: `Got unwanted ${fnType}${details}\n` +
               `Actual message: "${actual?.message}"`,
      stackStartFn,
    });
  }
  throw actual;
}

/**
 * Expects the function `promiseFn` to throw an error.
 * @param {() => any} promiseFn
 * @param {...any} [args]
 * @returns {void}
 */
assert.throws = function throws(promiseFn, ...args) {
  expectsError(throws, getActual(promiseFn), ...args);
};

/**
 * Expects `promiseFn` function or its value to reject.
 * @param {() => Promise<any>} promiseFn
 * @param {...any} [args]
 * @returns {Promise<void>}
 */
assert.rejects = async function rejects(promiseFn, ...args) {
  expectsError(rejects, await waitForActual(promiseFn), ...args);
};

/**
 * Asserts that the function `fn` does not throw an error.
 * @param {() => any} fn
 * @param {...any} [args]
 * @returns {void}
 */
assert.doesNotThrow = function doesNotThrow(fn, ...args) {
  expectsNoError(doesNotThrow, getActual(fn), ...args);
};

/**
 * Expects `fn` or its value to not reject.
 * @param {() => Promise<any>} fn
 * @param {...any} [args]
 * @returns {Promise<void>}
 */
assert.doesNotReject = async function doesNotReject(fn, ...args) {
  expectsNoError(doesNotReject, await waitForActual(fn), ...args);
};

/**
 * Throws `value` if the value is not `null` or `undefined`.
 * @param {any} err
 * @returns {void}
 */
assert.ifError = function ifError(err) {
  if (err !== null && err !== undefined) {
    let message = 'ifError got unwanted exception: ';
    if (typeof err === 'object' && typeof err.message === 'string') {
      if (err.message.length === 0 && err.constructor) {
        message += err.constructor.name;
      } else {
        message += err.message;
      }
    } else {
      message += inspect(err);
    }

    const newErr = new AssertionError({
      actual: err,
      expected: null,
      operator: 'ifError',
      message,
      stackStartFn: ifError,
    });

    // Make sure we actually have a stack trace!
    const origStack = err.stack;

    if (typeof origStack === 'string') {
      // This will remove any duplicated frames from the error frames taken
      // from within `ifError` and add the original error frames to the newly
      // created ones.
      const origStackStart = StringPrototypeIndexOf(origStack, '\n    at');
      if (origStackStart !== -1) {
        const originalFrames = StringPrototypeSplit(
          StringPrototypeSlice(origStack, origStackStart + 1),
          '\n',
        );
        // Filter all frames existing in err.stack.
        let newFrames = StringPrototypeSplit(newErr.stack, '\n');
        for (const errFrame of originalFrames) {
          // Find the first occurrence of the frame.
          const pos = ArrayPrototypeIndexOf(newFrames, errFrame);
          if (pos !== -1) {
            // Only keep new frames.
            newFrames = ArrayPrototypeSlice(newFrames, 0, pos);
            break;
          }
        }
        const stackStart = ArrayPrototypeJoin(newFrames, '\n');
        const stackEnd = ArrayPrototypeJoin(originalFrames, '\n');
        newErr.stack = `${stackStart}\n${stackEnd}`;
      }
    }

    throw newErr;
  }
};

function internalMatch(string, regexp, message, fn) {
  if (!isRegExp(regexp)) {
    throw new ERR_INVALID_ARG_TYPE(
      'regexp', 'RegExp', regexp,
    );
  }
  const match = fn === assert.match;
  if (typeof string !== 'string' ||
      RegExpPrototypeExec(regexp, string) !== null !== match) {
    if (message instanceof Error) {
      throw message;
    }

    const generatedMessage = !message;

    // 'The input was expected to not match the regular expression ' +
    message ||= (typeof string !== 'string' ?
      'The "string" argument must be of type string. Received type ' +
        `${typeof string} (${inspect(string)})` :
      (match ?
        'The input did not match the regular expression ' :
        'The input was expected to not match the regular expression ') +
          `${inspect(regexp)}. Input:\n\n${inspect(string)}\n`);
    const err = new AssertionError({
      actual: string,
      expected: regexp,
      message,
      operator: fn.name,
      stackStartFn: fn,
    });
    err.generatedMessage = generatedMessage;
    throw err;
  }
}

/**
 * Expects the `string` input to match the regular expression.
 * @param {string} string
 * @param {RegExp} regexp
 * @param {string | Error} [message]
 * @returns {void}
 */
assert.match = function match(string, regexp, message) {
  internalMatch(string, regexp, message, match);
};

/**
 * Expects the `string` input not to match the regular expression.
 * @param {string} string
 * @param {RegExp} regexp
 * @param {string | Error} [message]
 * @returns {void}
 */
assert.doesNotMatch = function doesNotMatch(string, regexp, message) {
  internalMatch(string, regexp, message, doesNotMatch);
};

assert.CallTracker = deprecate(CallTracker, 'assert.CallTracker is deprecated.', 'DEP0173');

/**
 * Expose a strict only variant of assert.
 * @param {...any} args
 * @returns {void}
 */
function strict(...args) {
  innerOk(strict, args.length, ...args);
}

assert.strict = ObjectAssign(strict, assert, {
  equal: assert.strictEqual,
  deepEqual: assert.deepStrictEqual,
  notEqual: assert.notStrictEqual,
  notDeepEqual: assert.notDeepStrictEqual,
});

assert.strict.strict = assert.strict;
