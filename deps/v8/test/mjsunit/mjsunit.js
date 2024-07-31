// Copyright 2008 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

var MjsUnitAssertionError = class MjsUnitAssertionError {
  #cached_message = undefined;
  #message_func = undefined;

  constructor(message_func) {
    this.#message_func = message_func;
    // Temporarily install a custom stack trace formatter and restore the
    // previous value.
    let prevPrepareStackTrace = Error.prepareStackTrace;
    try {
      Error.prepareStackTrace = MjsUnitAssertionError.prepareStackTrace;
      // This allows fetching the stack trace using TryCatch::StackTrace.
      this.stack = new Error("MjsUnitAssertionError").stack;
    } finally {
      Error.prepareStackTrace = prevPrepareStackTrace;
    }
  }

  get message() {
    if (this.#cached_message === undefined) {
      this.#cached_message = this.#message_func();
    }
    return this.#cached_message
  }

  toString() {
    return this.message + "\n\nStack: " + this.stack;
  };
}

/*
 * This file is included in all mini jsunit test cases.  The test
 * framework expects lines that signal failed tests to start with
 * the f-word and ignore all other lines.
 */

// Expected and found values the same objects, or the same primitive
// values.
// For known primitive values, please use assertEquals.
var assertSame;

// Inverse of assertSame.
var assertNotSame;

// Expected and found values are identical primitive values or functions
// or similarly structured objects (checking internal properties
// of, e.g., Number and Date objects, the elements of arrays
// and the properties of non-Array objects).
var assertEquals;

// Deep equality predicate used by assertEquals.
var deepEquals;

// Expected and found values are not identical primitive values or functions
// or similarly structured objects (checking internal properties
// of, e.g., Number and Date objects, the elements of arrays
// and the properties of non-Array objects).
var assertNotEquals;

// The difference between expected and found value is within certain tolerance.
var assertEqualsDelta;

// The found object is an Array with the same length and elements
// as the expected object. The expected object doesn't need to be an Array,
// as long as it's "array-ish".
var assertArrayEquals;

// The found object must have the same enumerable properties as the
// expected object. The type of object isn't checked.
var assertPropertiesEqual;

// Assert that the string conversion of the found value is equal to
// the expected string. Only kept for backwards compatibility, please
// check the real structure of the found value.
var assertToStringEquals;

// Checks that the found value is true. Use with boolean expressions
// for tests that doesn't have their own assertXXX function.
var assertTrue;

// Checks that the found value is false.
var assertFalse;

// Checks that the found value is null. Kept for historical compatibility,
// please just use assertEquals(null, expected).
var assertNull;

// Checks that the found value is *not* null.
var assertNotNull;

// Assert that the passed function or eval code throws an exception.
// The optional second argument is an exception constructor that the
// thrown exception is checked against with "instanceof".
// The optional third argument is a message type string or RegExp object that is
// compared to the message of the thrown exception.
var assertThrows;

// Asserts that the found value is an exception of specific type with a specific
// error message. The optional second argument is an exception constructor that
// the thrown exception is checked against with "instanceof". The optional third
// argument is a message type string or RegExp object that is compared to the
// message of the thrown exception.
var assertException;

// Assert that the passed function throws an exception.
// The exception is checked against the second argument using assertEquals.
var assertThrowsEquals;

// Assert that the passed promise does not resolve, but eventually throws an
// exception. The optional second argument is an exception constructor that the
// thrown exception is checked against with "instanceof".
// The optional third argument is a message type string or RegExp object that is
// compared to the message of the thrown exception.
var assertThrowsAsync;

// Assert that the passed function or eval code does not throw an exception.
var assertDoesNotThrow;

// Assert that the passed code throws an early error (i.e. throws a SyntaxError
// at parse time).
var assertEarlyError;

// Assert that the passed code throws an exception when executed.
// Fails if the passed code throws an exception at parse time.
var assertThrowsAtRuntime;

// Asserts that the found value is an instance of the constructor passed
// as the second argument.
var assertInstanceof;

// Assert that this code is never executed (i.e., always fails if executed).
var assertUnreachable;

// Assert that the function code is (not) optimized.
// Only works with --allow-natives-syntax.
var assertOptimized;
var assertUnoptimized;

// Assert that a string contains another expected substring.
var assertContains;

// Assert that a string matches a given regex.
var assertMatches;

// Assert that a promise resolves or rejects.
// Parameters:
// {promise} - the promise
// {success} - optional - a callback which is called with the result of the
//             resolving promise.
//  {fail} -   optional - a callback which is called with the result of the
//             rejecting promise. If the promise is rejected but no {fail}
//             callback is set, the error is propagated out of the promise
//             chain.
var assertPromiseResult;

var promiseTestChain;

// These bits must be in sync with bits defined in Runtime_GetOptimizationStatus
var V8OptimizationStatus = {
  kIsFunction: 1 << 0,
  kNeverOptimize: 1 << 1,
  kAlwaysOptimize: 1 << 2,
  kMaybeDeopted: 1 << 3,
  kOptimized: 1 << 4,
  kMaglevved: 1 << 5,
  kTurboFanned: 1 << 6,
  kInterpreted: 1 << 7,
  kMarkedForOptimization: 1 << 8,
  kMarkedForConcurrentOptimization: 1 << 9,
  kOptimizingConcurrently: 1 << 10,
  kIsExecuting: 1 << 11,
  kTopmostFrameIsTurboFanned: 1 << 12,
  kLiteMode: 1 << 13,
  kMarkedForDeoptimization: 1 << 14,
  kBaseline: 1 << 15,
  kTopmostFrameIsInterpreted: 1 << 16,
  kTopmostFrameIsBaseline: 1 << 17,
  kIsLazy: 1 << 18,
  kTopmostFrameIsMaglev: 1 << 19,
  kOptimizeOnNextCallOptimizesToMaglev: 1 << 20,
};

// Returns true if --lite-mode is on and we can't ever turn on optimization.
var isNeverOptimizeLiteMode;

// Returns true if --no-turbofan mode is on.
var isNeverOptimize;

// Returns true if --always-turbofan mode is on.
var isAlwaysOptimize;

// Returns true if given function in lazily compiled.
var isLazy;

// Returns true if given function in interpreted.
var isInterpreted;

// Returns true if given function in baseline.
var isBaseline;

// Returns true if given function in unoptimized (interpreted or baseline).
var isUnoptimized;

// Returns true if given function is optimized.
var isOptimized;

// Returns true if given function will be compiled by Maglev.
var willBeMaglevved;

// Returns true if given function will be compiled by TurboFan.
var willBeTurbofanned;

// Returns true if given function is compiled by Maglev.
var isMaglevved;

// Returns true if given function is compiled by TurboFan.
var isTurboFanned;

// Returns true if the top frame in interpreted according to the status
// passed as a parameter.
var topFrameIsInterpreted;

// Returns true if the top frame in baseline according to the status
// passed as a parameter.
var topFrameIsBaseline;

// Returns true if the top frame in compiled by Maglev according to the
// status passed as a parameter.
var topFrameIsMaglevved;

// Returns true if the top frame in compiled by Turbofan according to the
// status passed as a parameter.
var topFrameIsTurboFanned;

// Monkey-patchable all-purpose failure handler.
var failWithMessage;

// Returns the formatted failure text.  Used by test-async.js.
var formatFailureText;

// Returns a pretty-printed string representation of the passed value.
var prettyPrinted;

(function () {  // Scope for utility functions.

  var ObjectPrototypeToString = Object.prototype.toString;
  var NumberPrototypeValueOf = Number.prototype.valueOf;
  var BooleanPrototypeValueOf = Boolean.prototype.valueOf;
  var StringPrototypeValueOf = String.prototype.valueOf;
  var DatePrototypeValueOf = Date.prototype.valueOf;
  var RegExpPrototypeToString = RegExp.prototype.toString;
  var ArrayPrototypeForEach = Array.prototype.forEach;
  var ArrayPrototypeJoin = Array.prototype.join;
  var ArrayPrototypeMap = Array.prototype.map;
  var ArrayPrototypePush = Array.prototype.push;
  var JSONStringify = JSON.stringify;

  var BigIntPrototypeValueOf;
  // TODO(neis): Remove try-catch once BigInts are enabled by default.
  try {
    BigIntPrototypeValueOf = BigInt.prototype.valueOf;
  } catch (e) {}

  function classOf(object) {
    // Argument must not be null or undefined.
    var string = ObjectPrototypeToString.call(object);
    // String has format [object <ClassName>].
    return string.substring(8, string.length - 1);
  }


  function ValueOf(value) {
    switch (classOf(value)) {
      case "Number":
        return NumberPrototypeValueOf.call(value);
      case "BigInt":
        return BigIntPrototypeValueOf.call(value);
      case "String":
        return StringPrototypeValueOf.call(value);
      case "Boolean":
        return BooleanPrototypeValueOf.call(value);
      case "Date":
        return DatePrototypeValueOf.call(value);
      default:
        return value;
    }
  }


  prettyPrinted = function prettyPrinted(value) {
    let visited = new Set();
    function prettyPrint(value) {
      try {
        switch (typeof value) {
          case "string":
            return JSONStringify(value);
          case "bigint":
            return String(value) + "n";
          case "number":
            if (value === 0 && (1 / value) < 0) return "-0";
            // FALLTHROUGH.
          case "boolean":
          case "undefined":
          case "function":
          case "symbol":
            return String(value);
          case "object":
            if (value === null) return "null";
            // Guard against re-visiting.
            if (visited.has(value)) return "<...>";
            visited.add(value);
            var objectClass = classOf(value);
            switch (objectClass) {
              case "Number":
              case "BigInt":
              case "String":
              case "Boolean":
              case "Date":
                return objectClass + "(" + prettyPrint(ValueOf(value)) + ")";
              case "RegExp":
                return RegExpPrototypeToString.call(value);
              case "Array":
                var mapped = ArrayPrototypeMap.call(
                    value, (v,i,array)=>{
                      if (v === undefined && !(i in array)) return "";
                      return prettyPrint(v, visited);
                    });
                var joined = ArrayPrototypeJoin.call(mapped, ",");
                return "[" + joined + "]";
              case "Int8Array":
              case "Uint8Array":
              case "Uint8ClampedArray":
              case "Int16Array":
              case "Uint16Array":
              case "Int32Array":
              case "Uint32Array":
              case "Float32Array":
              case "Float64Array":
              case "BigInt64Array":
              case "BigUint64Array":
                var joined = ArrayPrototypeJoin.call(value, ",");
                return objectClass + "([" + joined + "])";
              case "Object":
                break;
              default:
                return objectClass + "(" + String(value) + ")";
            }
            // classOf() returned "Object".
            var name = value.constructor?.name ?? "Object";
            var pretty_properties = [];
            for (let [k,v] of Object.entries(value)) {
              ArrayPrototypePush.call(
                  pretty_properties, `${k}:${prettyPrint(v, visited)}`);
            }
            var joined = ArrayPrototypeJoin.call(pretty_properties, ",");
            return `${name}({${joined}})`;
          default:
            return "-- unknown value --";
        }
      } catch (e) {
        // Guard against general exceptions (especially stack overflows).
        return "<error>"
      }
    }
    return prettyPrint(value);
  }

  failWithMessage = function failWithMessage(message) {
    throw new MjsUnitAssertionError(()=>message);
  }

  formatFailureText = function(expectedText, found, name_opt) {
    var message = "Fail" + "ure";
    if (name_opt) {
      // Fix this when we ditch the old test runner.
      message += " (" + name_opt + ")";
    }

    var foundText = prettyPrinted(found);
    if (expectedText.length <= 40 && foundText.length <= 40) {
      message += ": expected <" + expectedText + "> found <" + foundText + ">";
    } else {
      message += ":\nexpected:\n" + expectedText + "\nfound:\n" + foundText;
    }
    return message;
  }

  function fail(expectedText, found, name_opt) {
    throw new MjsUnitAssertionError(
        ()=>formatFailureText(expectedText, found, name_opt));
  }


  function deepObjectEquals(a, b) {
    // Note: This function does not check prototype equality.

    // For now, treat two objects the same even if some property is configured
    // differently (configurable, enumerable, writable).
    var aProps = Object.getOwnPropertyNames(a);
    aProps.sort();
    var bProps = Object.getOwnPropertyNames(b);
    bProps.sort();
    if (!deepEquals(aProps, bProps)) {
      return false;
    }
    for (var i = 0; i < aProps.length; i++) {
      if (!deepEquals(a[aProps[i]], b[aProps[i]])) {
        return false;
      }
    }
    return true;
  }

  deepEquals = function deepEquals(a, b) {
    if (a === b) {
      // Check for -0.
      if (a === 0) return (1 / a) === (1 / b);
      return true;
    }
    if (typeof a !== typeof b) return false;
    if (typeof a === 'number') return isNaN(a) && isNaN(b);
    if (typeof a !== 'object' && typeof a !== 'function') return false;
    // Neither a nor b is primitive.
    var objectClass = classOf(a);
    if (objectClass !== classOf(b)) return false;
    switch (objectClass) {
      case 'RegExp':
        // For RegExp, just compare pattern and flags using its toString.
        return RegExpPrototypeToString.call(a) ===
            RegExpPrototypeToString.call(b);
      case 'Function':
        // Functions are only identical to themselves.
        return false;
      case 'Array':
        if (a.length !== b.length) return false;
        for (var i = 0; i < a.length; i++) {
          if ((i in a) !== (i in b)) return false;
          if (!deepEquals(a[i], b[i])) return false;
        }
        return true;
      case 'Int8Array':
      case 'Uint8Array':
      case 'Uint8ClampedArray':
      case 'Int16Array':
      case 'Uint16Array':
      case 'Int32Array':
      case 'Uint32Array':
      case 'BigInt64Array':
      case 'BigUint64Array':
        if (a.length !== b.length) return false;
        for (let i = 0; i < a.length; i++) {
          if (a[i] !== b[i]) return false;
        }
        return true;
      case 'Float32Array':
      case 'Float64Array':
        if (a.length !== b.length) return false;
        for (let i = 0; i < a.length; i++) {
          if (!deepEquals(a[i], b[i])) return false;
        }
        return true;
      case 'String':
      case 'Number':
      case 'BigInt':
      case 'Boolean':
      case 'Date':
        return ValueOf(a) === ValueOf(b);
    }
    return deepObjectEquals(a, b);
  }

  assertSame = function assertSame(expected, found, name_opt) {
    if (Object.is(expected, found)) return;
    fail(prettyPrinted(expected), found, name_opt);
  };

  assertNotSame = function assertNotSame(expected, found, name_opt) {
    if (!Object.is(expected, found)) return;
    fail("not same as " + prettyPrinted(expected), found, name_opt);
  }

  assertEquals = function assertEquals(expected, found, name_opt) {
    if (!deepEquals(found, expected)) {
      fail(prettyPrinted(expected), found, name_opt);
    }
  };

  assertNotEquals = function assertNotEquals(expected, found, name_opt) {
    if (deepEquals(found, expected)) {
      fail("not equals to " + prettyPrinted(expected), found, name_opt);
    }
  };


  assertEqualsDelta =
      function assertEqualsDelta(expected, found, delta, name_opt) {
    if (Math.abs(expected - found) > delta) {
      fail(prettyPrinted(expected) + " +- " + prettyPrinted(delta), found, name_opt);
    }
  };


  assertArrayEquals = function assertArrayEquals(expected, found, name_opt) {
    var start = "";
    if (name_opt) {
      start = name_opt + " - ";
    }
    assertEquals(expected.length, found.length, start + "array length");
    if (expected.length === found.length) {
      for (var i = 0; i < expected.length; ++i) {
        assertEquals(expected[i], found[i],
                     start + "array element at index " + i);
      }
    }
  };


  assertPropertiesEqual = function assertPropertiesEqual(expected, found,
                                                         name_opt) {
    // Check properties only.
    if (!deepObjectEquals(expected, found)) {
      fail(expected, found, name_opt);
    }
  };


  assertToStringEquals = function assertToStringEquals(expected, found,
                                                       name_opt) {
    if (expected !== String(found)) {
      fail(expected, found, name_opt);
    }
  };


  assertTrue = function assertTrue(value, name_opt) {
    assertEquals(true, value, name_opt);
  };


  assertFalse = function assertFalse(value, name_opt) {
    assertEquals(false, value, name_opt);
  };


  assertNull = function assertNull(value, name_opt) {
    if (value !== null) {
      fail("null", value, name_opt);
    }
  };


  assertNotNull = function assertNotNull(value, name_opt) {
    if (value === null) {
      fail("not null", value, name_opt);
    }
  };

  function executeCode(code) {
    if (typeof code === 'function') return code();
    if (typeof code === 'string') return eval(code);
    failWithMessage(
        'Given code is neither function nor string, but ' + (typeof code) +
        ': <' + prettyPrinted(code) + '>');
  }

  assertException = function assertException(e, type_opt, cause_opt) {
    if (type_opt !== undefined) {
      assertEquals('function', typeof type_opt);
      assertInstanceof(e, type_opt);
    }
    if (RegExp !== undefined && cause_opt instanceof RegExp) {
      assertMatches(cause_opt, e.message, 'Error message');
    } else if (cause_opt !== undefined) {
      assertEquals(cause_opt, e.message, 'Error message');
    }
  }

  assertThrows = function assertThrows(code, type_opt, cause_opt) {
    if (arguments.length > 1 && type_opt === undefined) {
      failWithMessage('invalid use of assertThrows, unknown type_opt given');
    }
    if (type_opt !== undefined && typeof type_opt !== 'function') {
      failWithMessage(
          'invalid use of assertThrows, maybe you want assertThrowsEquals');
    }
    try {
      executeCode(code);
    } catch (e) {
      assertException(e, type_opt, cause_opt);
      return;
    }
    let msg = 'Did not throw exception';
    if (type_opt !== undefined && type_opt.name !== undefined)
      msg += ', expected ' + type_opt.name;
    failWithMessage(msg);
  };

  assertThrowsEquals = function assertThrowsEquals(fun, val) {
    try {
      fun();
    } catch (e) {
      assertSame(val, e);
      return;
    }
    failWithMessage('Did not throw exception, expected ' + prettyPrinted(val));
  };

  assertThrowsAsync = function assertThrowsAsync(promise, type_opt, cause_opt) {
    if (arguments.length > 1 && type_opt === undefined) {
      failWithMessage('invalid use of assertThrows, unknown type_opt given');
    }
    if (type_opt !== undefined && typeof type_opt !== 'function') {
      failWithMessage(
          'invalid use of assertThrows, maybe you want assertThrowsEquals');
    }
    let msg = 'Promise did not throw exception';
    if (type_opt !== undefined && type_opt.name !== undefined)
      msg += ', expected ' + type_opt.name;
    return assertPromiseResult(
        promise,
        // Use setTimeout to throw the error again to get out of the promise
        // chain.
        res => setTimeout(_ => fail('<throw>', res, msg), 0),
        e => assertException(e, type_opt, cause_opt));
  };

  assertEarlyError = function assertEarlyError(code) {
    try {
      new Function(code);
    } catch (e) {
      assertException(e, SyntaxError);
      return;
    }
    failWithMessage('Did not throw exception while parsing');
  }

  assertThrowsAtRuntime = function assertThrowsAtRuntime(code, type_opt) {
    const f = new Function(code);
    if (arguments.length > 1 && type_opt !== undefined) {
      assertThrows(f, type_opt);
    } else {
      assertThrows(f);
    }
  }

  assertInstanceof = function assertInstanceof(obj, type) {
    if (!(obj instanceof type)) {
      var actualTypeName = null;
      var actualConstructor = obj && Object.getPrototypeOf(obj).constructor;
      if (typeof actualConstructor === 'function') {
        actualTypeName = actualConstructor.name || String(actualConstructor);
      }
      failWithMessage(
          'Object <' + prettyPrinted(obj) + '> is not an instance of <' +
          (type.name || type) + '>' +
          (actualTypeName ? ' but of <' + actualTypeName + '>' : ''));
    }
  };

  assertDoesNotThrow = function assertDoesNotThrow(code, name_opt) {
    try {
      executeCode(code);
    } catch (e) {
      if (e instanceof MjsUnitAssertionError) throw e;
      failWithMessage("threw an exception: " + (e.message || e));
    }
  };

  assertUnreachable = function assertUnreachable(name_opt) {
    // Fix this when we ditch the old test runner.
    var message = "Fail" + "ure: unreachable";
    if (name_opt) {
      message += " - " + name_opt;
    }
    failWithMessage(message);
  };

  assertContains = function(sub, value, name_opt) {
    if (value == null ? (sub != null) : value.indexOf(sub) == -1) {
      fail("contains '" + String(sub) + "'", value, name_opt);
    }
  };

  assertMatches = function(regexp, str, name_opt) {
    if (!(regexp instanceof RegExp)) {
      regexp = new RegExp(regexp);
    }
    if (!str.match(regexp)) {
      fail("should match '" + regexp + "'", str, name_opt);
    }
  };

  function concatenateErrors(stack, exception) {
    // If the exception does not contain a stack trace, wrap it in a new Error.
    if (!exception.stack) exception = new Error(exception);

    // If the exception already provides a special stack trace, we do not modify
    // it.
    if (typeof exception.stack !== 'string') {
      return exception;
    }
    exception.stack = stack + '\n\n' + exception.stack;
    return exception;
  }

  assertPromiseResult = function(promise, success, fail) {
    if (success !== undefined) assertEquals('function', typeof success);
    if (fail !== undefined) assertEquals('function', typeof fail);
    assertInstanceof(promise, Promise);
    const stack = (new Error()).stack;

    var test_promise = promise.then(
        result => {
          try {
            if (success !== undefined) success(result);
          } catch (e) {
            // Use setTimeout to throw the error again to get out of the promise
            // chain.
            setTimeout(_ => {
              throw concatenateErrors(stack, e);
            }, 0);
          }
        },
        result => {
          try {
            if (fail === undefined) throw result;
            fail(result);
          } catch (e) {
            // Use setTimeout to throw the error again to get out of the promise
            // chain.
            setTimeout(_ => {
              throw concatenateErrors(stack, e);
            }, 0);
          }
        });

    if (!promiseTestChain) promiseTestChain = Promise.resolve();
    return promiseTestChain.then(test_promise);
  };

  var OptimizationStatusImpl = undefined;

  var OptimizationStatus = function(fun) {
    if (OptimizationStatusImpl === undefined) {
      try {
        OptimizationStatusImpl = new Function(
            "fun", "return %GetOptimizationStatus(fun);");
      } catch (e) {
        throw new Error("natives syntax not allowed");
      }
    }
    return OptimizationStatusImpl(fun);
  }

  assertUnoptimized = function assertUnoptimized(
      fun, name_opt, skip_if_maybe_deopted = true) {
    var opt_status = OptimizationStatus(fun);
    name_opt = name_opt ?? fun.name;
    // Tests that use assertUnoptimized() do not make sense if --always-turbofan
    // option is provided. Such tests must add --no-always-turbofan to flags comment.
    assertFalse((opt_status & V8OptimizationStatus.kAlwaysOptimize) !== 0,
                "test does not make sense with --always-turbofan");
    assertTrue((opt_status & V8OptimizationStatus.kIsFunction) !== 0, name_opt);
    if (skip_if_maybe_deopted &&
        (opt_status & V8OptimizationStatus.kMaybeDeopted) !== 0) {
      // When --deopt-every-n-times flag is specified it's no longer guaranteed
      // that particular function is still deoptimized, so keep running the test
      // to stress test the deoptimizer.
      return;
    }
    var is_optimized = (opt_status & V8OptimizationStatus.kOptimized) !== 0;
    if (is_optimized && (opt_status & V8OptimizationStatus.kMaglevved) &&
        (opt_status &
         V8OptimizationStatus.kOptimizeOnNextCallOptimizesToMaglev)) {
      // When --optimize-on-next-call-optimizes-to-maglev is used, we might emit
      // more generic code than optimization tests expect. In such cases,
      // assertUnoptimized may see optimized code, but we still want it to
      // succeed and continue the test.
      return;
    }
    assertFalse(is_optimized, 'should not be optimized: ' + name_opt);
  }

  assertOptimized = function assertOptimized(
      fun, name_opt, skip_if_maybe_deopted = true) {
    var opt_status = OptimizationStatus(fun);
    name_opt = name_opt ?? fun.name;
    // Tests that use assertOptimized() do not make sense for Lite mode where
    // optimization is always disabled, explicitly exit the test with a warning.
    if (opt_status & V8OptimizationStatus.kLiteMode) {
      print("Warning: Test uses assertOptimized in Lite mode, skipping test.");
      testRunner.quit(0);
    }
    // Tests that use assertOptimized() do not make sense if --no-turbofan
    // option is provided. Such tests must add --turbofan to flags comment.
    assertFalse((opt_status & V8OptimizationStatus.kNeverOptimize) !== 0,
                "test does not make sense with --no-turbofan");
    assertTrue(
        (opt_status & V8OptimizationStatus.kIsFunction) !== 0,
        'should be a function: ' + name_opt);
    if (skip_if_maybe_deopted &&
        (opt_status & V8OptimizationStatus.kMaybeDeopted) !== 0) {
      // When --deopt-every-n-times flag is specified it's no longer guaranteed
      // that particular function is still optimized, so keep running the test
      // to stress test the deoptimizer.
      return;
    }
    assertTrue(
        (opt_status & V8OptimizationStatus.kOptimized) !== 0,
        'should be optimized: ' + name_opt);
  }

  isNeverOptimizeLiteMode = function isNeverOptimizeLiteMode() {
    var opt_status = OptimizationStatus(undefined, "");
    return (opt_status & V8OptimizationStatus.kLiteMode) !== 0;
  }

  isNeverOptimize = function isNeverOptimize() {
    var opt_status = OptimizationStatus(undefined, "");
    return (opt_status & V8OptimizationStatus.kNeverOptimize) !== 0;
  }

  isAlwaysOptimize = function isAlwaysOptimize() {
    var opt_status = OptimizationStatus(undefined, "");
    return (opt_status & V8OptimizationStatus.kAlwaysOptimize) !== 0;
  }

  isLazy = function isLazy(fun) {
    var opt_status = OptimizationStatus(fun, '');
    assertTrue((opt_status & V8OptimizationStatus.kIsFunction) !== 0,
               "not a function");
    return (opt_status & V8OptimizationStatus.kIsLazy) !== 0;
  }

  isInterpreted = function isInterpreted(fun) {
    var opt_status = OptimizationStatus(fun, "");
    assertTrue((opt_status & V8OptimizationStatus.kIsFunction) !== 0,
               "not a function");
    return (opt_status & V8OptimizationStatus.kOptimized) === 0 &&
           (opt_status & V8OptimizationStatus.kInterpreted) !== 0;
  }

  isBaseline = function isBaseline(fun) {
    var opt_status = OptimizationStatus(fun, "");
    assertTrue((opt_status & V8OptimizationStatus.kIsFunction) !== 0,
               "not a function");
    return (opt_status & V8OptimizationStatus.kOptimized) === 0 &&
           (opt_status & V8OptimizationStatus.kBaseline) !== 0;
  }

  isUnoptimized = function isUnoptimized(fun) {
    return isInterpreted(fun) || isBaseline(fun);
  }

  isOptimized = function isOptimized(fun) {
    var opt_status = OptimizationStatus(fun, "");
    assertTrue((opt_status & V8OptimizationStatus.kIsFunction) !== 0,
               "not a function");
    return (opt_status & V8OptimizationStatus.kOptimized) !== 0;
  }

  isMaglevved = function isMaglevved(fun) {
    var opt_status = OptimizationStatus(fun, "");
    assertTrue((opt_status & V8OptimizationStatus.kIsFunction) !== 0,
               "not a function");
    return (opt_status & V8OptimizationStatus.kOptimized) !== 0 &&
           (opt_status & V8OptimizationStatus.kMaglevved) !== 0;
  }

  willBeMaglevved = function willBeMaglevved(fun) {
    var opt_status = OptimizationStatus(fun, "");
    assertTrue((opt_status & V8OptimizationStatus.kIsFunction) !== 0,
               "not a function");
    return (opt_status & V8OptimizationStatus.kOptimizeOnNextCallOptimizesToMaglev) !== 0;
  }

  willBeTurbofanned = function willBeTurbofanned(fun) {
    var opt_status = OptimizationStatus(fun, "");
    assertTrue((opt_status & V8OptimizationStatus.kIsFunction) !== 0,
               "not a function");
    return (opt_status & V8OptimizationStatus.kOptimizeOnNextCallOptimizesToMaglev) === 0;
  }

  isTurboFanned = function isTurboFanned(fun) {
    var opt_status = OptimizationStatus(fun, "");
    assertTrue((opt_status & V8OptimizationStatus.kIsFunction) !== 0,
               "not a function");
    return (opt_status & V8OptimizationStatus.kOptimized) !== 0 &&
           (opt_status & V8OptimizationStatus.kTurboFanned) !== 0;
  }

  topFrameIsInterpreted = function topFrameIsInterpreted(opt_status) {
    assertNotEquals(opt_status, undefined);
    return (opt_status & V8OptimizationStatus.kTopmostFrameIsInterpreted) !== 0;
  }

  topFrameIsBaseline = function topFrameIsBaseline(opt_status) {
    assertNotEquals(opt_status, undefined);
    return (opt_status & V8OptimizationStatus.kTopmostFrameIsBaseline) !== 0;
  }

  topFrameIsMaglevved = function topFrameIsMaglevved(opt_status) {
    assertNotEquals(opt_status, undefined);
    return (opt_status & V8OptimizationStatus.kTopmostFrameIsMaglev) !== 0;
  }

  topFrameIsTurboFanned = function topFrameIsTurboFanned(opt_status) {
    assertNotEquals(opt_status, undefined);
    return (opt_status & V8OptimizationStatus.kTopmostFrameIsTurboFanned) !== 0;
  }

  // Custom V8-specific stack trace formatter that is temporarily installed on
  // the Error object.
  MjsUnitAssertionError.prepareStackTrace = function(error, stack) {
    // Trigger default formatting with recursion.
    try {
      // Filter-out all but the first mjsunit frame.
      let filteredStack = [];
      let inMjsunit = true;
      for (let i = 0; i < stack.length; i++) {
        let frame = stack[i];
        if (inMjsunit) {
          let file = frame.getFileName();
          if (!file || !file.endsWith("mjsunit.js")) {
            inMjsunit = false;
            // Push the last mjsunit frame, typically containing the assertion
            // function.
            if (i > 0) ArrayPrototypePush.call(filteredStack, stack[i-1]);
            ArrayPrototypePush.call(filteredStack, stack[i]);
          }
          continue;
        }
        ArrayPrototypePush.call(filteredStack, frame);
      }
      stack = filteredStack;

      // Infer function names and calculate {max_name_length}
      let max_name_length = 0;
      ArrayPrototypeForEach.call(stack, each => {
        let name = each.getFunctionName();
        if (name == null) name = "";
        if (each.isEval()) {
          name = name;
        } else if (each.isConstructor()) {
          name = "new " + name;
        } else if (each.isNative()) {
          name = "native " + name;
        } else if (!each.isToplevel()) {
          name = each.getTypeName() + "." + name;
        }
        each.name = name;
        max_name_length = Math.max(name.length, max_name_length)
      });

      // Format stack frames.
      stack = ArrayPrototypeMap.call(stack, each => {
        let frame = "    at " + each.name.padEnd(max_name_length);
        let fileName = each.getFileName();
        if (each.isEval()) return frame + " " + each.getEvalOrigin();
        frame += " " + (fileName ? fileName : "");
        let line= each.getLineNumber();
        frame += " " + (line ? line : "");
        let column = each.getColumnNumber();
        frame += (column ? ":" + column : "");
        return frame;
      });
      return "" + error.message + "\n" + ArrayPrototypeJoin.call(stack, "\n");
    } catch (e) {};
    return error.stack;
  }
})();
