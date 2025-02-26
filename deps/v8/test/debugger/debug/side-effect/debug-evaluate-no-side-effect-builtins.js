// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --js-staging

Debug = debug.Debug

var exception = null;
var object = {"foo": "bar"};
var object_with_symbol_key = {[Symbol("a")]: 1};
var object_with_callbacks = { toString: () => "string", valueOf: () => 3};
var symbol_for_a = Symbol.for("a");
var typed_array = new Uint8Array([1, 2, 3]);
var array_buffer = new ArrayBuffer(3);
var data_view = new DataView(new ArrayBuffer(8), 0, 8);
var array = [1,2,3];
var pure_function = function(x) { return x * x; };
var unpure_function = function(x) { array.push(x); };
var stack = new DisposableStack();
var regexp = /\d/g;
var async_stack = new AsyncDisposableStack();

function listener(event, exec_state, event_data, data) {
  if (event != Debug.DebugEvent.Break) return;
  try {
    function success(expectation, source) {
      var result = exec_state.frame(0).evaluate(source, true).value();
      if (expectation !== undefined) assertEquals(expectation, result);
    }
    function fail(source) {
      assertThrows(() => exec_state.frame(0).evaluate(source, true),
                   EvalError);
    }

    // Test some Object functions.
    success({}, `new Object()`);
    success({p : 3}, `Object.create({}, { p: { value: 3 } })`);
    success("[[\"a\",1],[\"b\",2]]",
            `JSON.stringify(Object.entries({a:1, b:2}))`);
    success({value: 1, writable: true, enumerable: true, configurable: true},
            `Object.getOwnPropertyDescriptor({a: 1}, "a")`);
    success("{\"a\":{\"value\":1,\"writable\":true," +
            "\"enumerable\":true,\"configurable\":true}}",
            `JSON.stringify(Object.getOwnPropertyDescriptors({a: 1}))`);
    success(["a"], `Object.getOwnPropertyNames({a: 1})`);
    success(undefined, `Object.getOwnPropertySymbols(object_with_symbol_key)`);
    success({}, `Object.getPrototypeOf(Object.create({}))`);
    success(true, `Object.is(Object, Object)`);
    success(true, `Object.isExtensible({})`);
    success(false, `Object.isFrozen({})`);
    success(false, `Object.isSealed({})`);
    success([1, 2], `Object.values({a:1, b:2})`);
    success(["a", 1, "b", 2], `Object.entries({a:1, b:2}).flat()`);
    success(["a", "b"], `Object.keys({a:1, b:2})`);
    success(
        '{"1":[1],"2":[2]}', `JSON.stringify(Object.groupBy([1,2], x => x))`);

    fail(`Object.assign({}, {})`);
    fail(`Object.defineProperties({}, [{p:{value:3}}])`);
    fail(`Object.defineProperty({}, {p:{value:3}})`);
    fail(`Object.freeze({})`);
    fail(`Object.preventExtensions({})`);
    fail(`Object.seal({})`);
    fail(`Object.setPrototypeOf({}, {})`);

    // Test some Object.prototype functions.
    success(true, `({a:1}).hasOwnProperty("a")`);
    success(true, `Object.prototype.isPrototypeOf({})`);
    success(true, `({a:1}).propertyIsEnumerable("a")`);
    success("[object Object]", `({a:1}).toString()`);
    success("[object Object]", `({a:1}).toLocaleString()`);
    success("string", `(object_with_callbacks).toString()`);
    success(3, `(object_with_callbacks).valueOf()`);

    // Test Array functions.
    success(true, `Array.isArray([1, 2, 3])`);
    success([], `new Array()`);
    success([undefined, undefined], `new Array(2)`);
    success([1, 2], `new Array(1, 2)`);
    success([1, 2, 3], `Array.from([1, 2, 3])`);
    success(3, `Array.from([1, 2, 3]).pop()`);
    success([1, 2, 3], `Array.of(1, 2, 3)`);
    success(3, `Array.of(1, 2, 3).pop()`);
    var function_param = [
      "flatMap", "forEach", "every", "some", "reduce", "reduceRight", "find",
      "filter", "map", "findIndex", "findLast", "findLastIndex", "group",
      "groupToMap"
    ];
    var fails = ["pop", "push", "reverse", "shift", "unshift", "splice",
      "sort", "copyWithin", "fill"];
    for (f of Object.getOwnPropertyNames(Array.prototype)) {
      if (typeof Array.prototype[f] === "function") {
        if (fails.includes(f)) {
          if (function_param.includes(f)) {
            fail(`array.${f}(()=>{});`);
          } else {
            fail(`array.${f}();`);
          }
        } else if (function_param.includes(f)) {
          exec_state.frame(0).evaluate(`array.${f}(()=>{});`, true);
        } else {
          exec_state.frame(0).evaluate(`array.${f}();`, true);
        }
      }
    }

    // Test ArrayBuffer functions.
    success(3, `array_buffer.byteLength`);
    success(2, `array_buffer.slice(1, 3).byteLength`);
    success(true, `ArrayBuffer.isView(typed_array)`);

    // Test DataView functions.
    success(undefined, `new DataView(array_buffer, 1, 2)`);
    success(undefined, `data_view.buffer`);
    success(undefined, `data_view.byteLength`);
    success(undefined, `data_view.byteOffset`);
    for (f of Object.getOwnPropertyNames(DataView.prototype)) {
      if (typeof data_view[f] === 'function') {
        if (f.startsWith('getBig')) {
          success(0n, `data_view.${f}()`);
        } else if (f.startsWith('get')) {
          success(0, `data_view.${f}()`);
        }
      }
    }

    // Test TypedArray functions.
    success({}, `new Uint8Array()`);
    success({0: 0, 1: 0}, `new Uint8Array(2)`);
    success({0: 1, 1: 2, 2: 3}, `new Uint8Array(typed_array)`);
    success(true, `!!typed_array.buffer`);
    success(0, `typed_array.byteOffset`);
    success(3, `typed_array.byteLength`);
    success({0: 1, 1: 2}, `Uint8Array.of(1, 2)`);
    function_param = [
      "forEach", "every", "some", "reduce", "reduceRight", "find", "filter",
      "map", "findIndex", "findLast", "findLastIndex",
    ];
    fails = ["reverse", "sort", "copyWithin", "fill", "set"];
    var typed_proto_proto = Object.getPrototypeOf(Object.getPrototypeOf(new Uint8Array()));
    for (f of Object.getOwnPropertyNames(typed_proto_proto)) {
      if (typeof typed_array[f] === "function" && f !== "constructor") {
        if (fails.includes(f)) {
          if (function_param.includes(f)) {
            fail(`typed_array.${f}(()=>{});`);
          } else {
            fail(`typed_array.${f}();`);
          }
        } else if (function_param.includes(f)) {
          exec_state.frame(0).evaluate(`typed_array.${f}(()=>{});`, true);
        } else {
          exec_state.frame(0).evaluate(`typed_array.${f}();`, true);
        }
      }
    }

    // Test Math functions.
    for (f of Object.getOwnPropertyNames(Math)) {
      if (f !== "random" && typeof Math[f] === "function") {
        var result = exec_state.frame(0).evaluate(
                         `Math.${f}(0.5, -0.5);`, true).value();
        assertEquals(Math[f](0.5, -0.5), result);
      }
    }
    fail("Math.random();");

    // Test Number functions.
    success(new Number(0), `new Number()`);
    for (f of Object.getOwnPropertyNames(Number)) {
      if (typeof Number[f] === "function") {
        success(Number[f](0.5), `Number.${f}(0.5);`);
      }
    }
    for (f of Object.getOwnPropertyNames(Number.prototype)) {
      if (typeof Number.prototype[f] === "function") {
        if (f == "toLocaleString" && typeof Intl === "undefined") continue;
        success(Number(0.5)[f](5), `Number(0.5).${f}(5);`);
      }
    }

    // Test String functions.
    for (f of Object.getOwnPropertyNames(String.prototype)) {
      if (typeof String.prototype[f] === "function") {
        // Do not expect locale-specific or regexp-related functions to work.
        // {Lower,Upper}Case (Locale-specific or not) do not work either
        // if Intl is enabled.
        if (f.indexOf("locale") >= 0) continue;
        if (f.indexOf("Locale") >= 0) continue;
        switch (f) {
          case "match":
          case "split":
          case "matchAll":
          case "normalize":
          case "search":
            case "toLowerCase":
            case "toUpperCase":
            continue;
        }
        success("abcd"[f](2), `"abcd".${f}(2);`);
      }
    }

    success(new String(), `new String()`);
    success(" ", "String.fromCodePoint(0x20)");
    success(" ", "String.fromCharCode(0x20)");
    success("abcd", "'abCd'.toLocaleLowerCase()");
    success("ABCD", "'abcd'.toLocaleUpperCase()");
    success("abcd", "'abCd'.toLowerCase()");
    success("ABCD", "'abcd'.toUpperCase()");
    success("a", "'abcd'.match('a')[0]");
    success("a", "'abcd'.match(/a/)[0]");
    fail("'1234'.match(regexp)");
    success("[object RegExp String Iterator]", "'abcd'.matchAll('a').toString()");
    success("[object RegExp String Iterator]", "'abcd'.matchAll(/a/g).toString()");
    fail("'1234'.matchAll(regexp)");
    success("ebcd", "'abcd'.replace('a', 'e')");
    success("ebcd", "'abcd'.replace(/a/, 'e')");
    fail("'135'.replace(regexp, 'e')");
    success("ebcd", "'abcd'.replaceAll('a', 'e')");
    success("ebcd", "'abcd'.replaceAll(/a/g, 'e')");
    fail("'135'.replaceAll(regexp, 'e')");
    success(1, "'abcd'.search('b')");
    success(1, "'abcd'.search(/b/)");
    fail("'12a34b'.search(regexp)");
    success(["a", "cd"], "'abcd'.split('b')");
    success(["a", "cd"], "'abcd'.split(/b/)");
    fail("'12a34b'.split(regexp)");
    success(-1, "'abcd'.localeCompare('abce')");
    success('abcd', "'abcd'.normalize('NFC')");

    // Test RegExp functions.
    fail(`/a/.compile()`);
    success('a', `/a/.exec('abc')[0]`);
    success(true, `/a/.test('abc')`);
    fail(`/a/.toString()`);

    // Test JSON functions.
    success('{"abc":[1,2]}', "JSON.stringify(JSON.parse('{\"abc\":[1,2]}'))");

    // Test Symbol functions.
    success(undefined, `Symbol("a")`);
    fail(`Symbol.for("a")`);  // Symbol.for can be observed via Symbol.keyFor.
    success("a", `Symbol.keyFor(symbol_for_a)`);
    success("Symbol(a)", `symbol_for_a.valueOf().toString()`);
    success("Symbol(a)", `symbol_for_a[Symbol.toPrimitive]().toString()`);

    // Test Reflect functions.
    success(4, `Reflect.apply(pure_function, undefined, [2])`);
    fail(`Reflect.apply(unpure_function, undefined, [2])`);
    success("foo", `Reflect.construct(String, ["foo"]).toString()`);
    fail(`Reflect.construct(unpure_function, ["foo"])`);
    success("bar", `Reflect.getOwnPropertyDescriptor(object, "foo").value`);
    success(true, `Reflect.getPrototypeOf(object) === Object.prototype`);
    success(true, `Reflect.has(object, "foo")`);
    success(true, `Reflect.isExtensible(object)`);
    success("foo", `Reflect.ownKeys(object)[0]`);
    fail(`Reflect.defineProperty(object, "baz", {})`);
    fail(`Reflect.deleteProperty(object, "foo")`);
    fail(`Reflect.preventExtensions(object)`);
    fail(`Reflect.set(object, "great", "expectations")`);
    fail(`Reflect.setPrototypeOf(object, Array.prototype)`);

    // Test some Map functions
    success('[1]', `JSON.stringify(Map.groupBy([1,2], x => x).get(1))`);

    // Test DisposableStack functions.
    success({}, `new DisposableStack()`);
    success(false, `stack.disposed`);

    // Test AsyncDisposableStack functions.
    success({}, `new AsyncDisposableStack()`);
    success(false, `async_stack.disposed`);
  } catch (e) {
    exception = e;
    print(e, e.stack);
  };
};

// Add the debug event listener.
Debug.setListener(listener);

function f() {
  debugger;
};

f();

assertNull(exception);
