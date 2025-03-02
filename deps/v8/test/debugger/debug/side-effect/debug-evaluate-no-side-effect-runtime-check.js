// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --js-staging

Debug = debug.Debug;

// StaCurrentContextSlot
success(10, `(function(){
  const x = 10;
  function f1() {return x;}
  return x;
})()`);

// SetNamedProperty
var a = {name: 'foo'};
function set_name(a) {
  a.name = 'bar';
  return a.name;
}

fail(`set_name(a)`);
success('bar', `set_name({name: 'foo'})`);

// DefineNamedOwnProperty
var name_value = 'value';
function create_object_literal() {
  var obj = {name: name_value};
  return obj.name;
};

success('value', `create_object_literal()`);

// SetKeyedProperty
var arrayValue = 1;
function create_array_literal() {
  return [arrayValue];
}
var b = { 1: 2 };

success([arrayValue], `create_array_literal()`)
fail(`b[1] ^= 2`);

// StaInArrayLiteral
function return_array_use_spread(a) {
  return [...a];
}

success([1], `return_array_use_spread([1])`);

// CallAccessorSetter
var array = [1,2,3];
fail(`array.length = 2`);
success(2, `[1,2,3].length = 2`);

// DefineKeyedOwnPropertyInLiteral
function return_literal_with_data_property(a) {
  return {[a] : 1};
}

success({foo: 1}, `return_literal_with_data_property('foo')`);

// Set builtins with temporary objects
var set = new Set([1,2]);
fail(`set.add(3).size`);
success(1, `new Set().add(1).size`);

success(0, `(() => { const s = new Set([1]); s.delete(1); return s.size; })()`);
fail(`set.delete(1)`);

success(0, `(() => { const s = new Set([1]); s.clear(); return s.size; })()`);
fail(`set.clear()`);

// new set
success(3, `(() => {
  let s = 0;
  for (const a of new Set([1,2]))
    s += a;
  return s;
})()`);
// existing set
success(3, `(() => {
  let s = 0;
  for (const a of set)
    s += a;
  return s;
})()`);
// existing iterator
var setIterator = set.entries();
fail(`(() => {
  let s = 0;
  for (const a of setIterator)
    s += a;
  return s;
})()`);

// Array builtins with temporary objects
success([1,1,1], '[1,2,3].fill(1)');
fail(`array.fill(1)`);

success([1], `(() => { const a = []; a.push(1); return a; })()`);
fail(`array.push(1)`);

success([1], `(() => { const a = [1,2]; a.pop(); return a; })()`);
fail(`array.pop()`);

success([3,2,1], `[1,2,3].reverse()`);
fail(`array.reverse()`);

success([1,2,3], `[2,1,3].sort()`);
fail(`array.sort()`);

success([2,3], `[1,2,3].splice(1,2)`);
fail(`array.splice(1,2)`);

success([1,2], `(() => { const a = [2]; a.unshift(1); return a; })()`);
fail(`array.unshift(1)`);
success(1, `[1,2].shift()`);
fail(`array.shift()`);

// new array
success(6, `(() => {
  let s = 0;
  for (const a of [1,2,3])
    s += a;
  return s;
})()`);
// existing array
success(6, `(() => {
  let s = 0;
  for (const a of array)
    s += a;
  return s;
})()`);
// existing iterator
var arrayIterator = array.entries();
fail(`(() => {
  let s = 0;
  for (const a of arrayIterator)
    s += a;
  return s;
})()`);

success(6, `array.reduce((a,b) => a + b, 0)`);

// Map builtins with temporary objects
var map = new Map([[1,2]]);
fail(`map.set(3, 4).size`);
success(1, `new Map().set(1, 2).size`);

success(0, `(() => {
  const m = new Map([[1, 2]]);
  m.delete(1);
  return m.size;
})()`);
fail(`map.delete(1)`);

success(0, `(() => {
  const m = new Map([[1, 2]]);
  m.clear();
  return m.size;
})()`);
fail(`map.clear()`);

// new set
success(2, `(() => {
  let s = 0;
  for (const [a, b] of new Map([[1,2]]))
    s += b;
  return s;
})()`);
// existing set
success(2, `(() => {
  let s = 0;
  for (const [a,b] of map)
    s += b;
  return s;
})()`);
// existing iterator
var mapIterator = map.entries();
fail(`(() => {
  let s = 0;
  for (const [a,b] of mapIterator)
    s += a;
  return s;
})()`);

// Regexps
var regExp = /a/;
success(true, `/a/.test('a')`);
fail(`/a/.test({toString: () => {map.clear(); return 'a';}})`)
fail(`regExp.test('a')`);

// DisposableStack use().
let disposable_stack = new DisposableStack();
fail(`(() => {
  const disposable = {
    value: 1,
    [Symbol.dispose]() {
      return 43;
    }
  };
  disposable_stack.use(disposable);
})()`);
success(42, `(() => {
  let stack = new DisposableStack();
  const disposable = {
    value: 1,
    [Symbol.dispose]() {
      return 43;
    }
  };
  stack.use(disposable);
  return 42;
})()`)

// DisposableStack dispose().
fail(`disposable_stack.dispose()`);
success(42, `(() => {
  let stack = new DisposableStack();
  stack.dispose();
  return 42;
})()`);

// DisposableStack adopt().
fail(`disposable_stack.adopt(42, function(v) {return v})`);
success(42, `(() => {
  let stack = new DisposableStack();
  stack.adopt(42, function(v) {return v});
  return 42;
})()`);

// DisposableStack defer().
fail(`disposable_stack.defer(() => console.log(42))`);
success(42, `(() => {
  let stack = new DisposableStack();
  stack.defer(() => console.log(42));
  return 42;
})()`);

// DisposableStack move().
fail(`let new_disposable_stack = disposable_stack.move()`);
success(42, `(() => {
  let stack = new DisposableStack();
  const disposable = {
    value: 1,
    [Symbol.dispose]() {
      return 43;
    }
  };
  stack.use(disposable);
  let new_disposable_stack = stack.move()
  return 42;
})()`);

// AsyncDisposableStack use().
let async_disposable_stack = new AsyncDisposableStack();
fail(`(() => {
  const async_disposable = {
    value: 1,
    [Symbol.asyncDispose]() {
      return 43;
    }
  };
  async_disposable_stack.use(async_disposable);
})()`);
success(42, `(() => {
  let stack = new AsyncDisposableStack();
  const async_disposable = {
    value: 1,
    [Symbol.asyncDispose]() {
      return 43;
    }
  };
  stack.use(async_disposable);
  return 42;
})()`)

// AsyncDisposableStack disposeAsync().
fail(`async_disposable_stack.disposeAsync()`);
success(42, `(() => {
  let stack = new AsyncDisposableStack();
  stack.disposeAsync();
  return 42;
})()`);

// AsyncDisposableStack adopt().
fail(`async_disposable_stack.adopt(42, function(v) {return v})`);
success(42, `(() => {
  let stack = new AsyncDisposableStack();
  stack.adopt(42, function(v) {return v});
  return 42;
})()`);

// AsyncDisposableStack defer().
fail(`async_disposable_stack.defer(() => console.log(42))`);
success(42, `(() => {
  let stack = new AsyncDisposableStack();
  stack.defer(() => console.log(42));
  return 42;
})()`);

// AsyncDisposableStack move().
fail(`let new_async_Sdisposable_stack = async_disposable_stack.move()`);
success(42, `(() => {
  let stack = new AsyncDisposableStack();
  const async_disposable = {
    value: 1,
    [Symbol.asyncDispose]() {
      return 43;
    }
  };
  stack.use(async_disposable);
  let new_async_disposable_stack = stack.move()
  return 42;
})()`);

function success(expectation, source) {
  const result = Debug.evaluateGlobal(source, true).value();
  if (expectation !== undefined) assertEquals(expectation, result);
}

function fail(source) {
  assertThrows(() => Debug.evaluateGlobal(source, true),
               EvalError);
}
