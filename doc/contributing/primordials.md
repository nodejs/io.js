# Usage of primordials in core

The file `lib/internal/per_context/primordials.js` subclasses and stores the JS
built-ins that come from the VM so that Node.js built-in modules do not need to
later look these up from the global proxy, which can be mutated by users.

For some area of the codebase, performance and code readability are deemed more
important than reliability against prototype pollution:

* `node:http`
* `node:http2`
* `node:tls`
* `node:zlib`

Usage of primordials should be preferred for new code in other areas, but
replacing current code with primordials should be
[done with care](#primordials-with-known-performance-issues). It is highly
recommended to ping the relevant team when reviewing a pull request that touches
one of the subsystems they "own".

## Accessing primordials

The primordials are meant for internal use only, and are only accessible for
internal core modules. User code cannot use or rely on primordials. It is
usually fine to rely on ECMAScript built-ins and assume that it will behave as
specified.

If you would like to access the `primordials` object to help you with Node.js
core development or for tinkering, you can expose it on the global scope using
this combination of CLI flags:

```bash
node --expose-internals -r internal/test/binding
```

## Contents of primordials

### Properties of the global object

Objects and functions on the global object can be deleted or replaced. Using
them from primordials makes the code more reliable:

```js
globalThis.Array === primordials.Array; // true

globalThis.Array = function() {
  return [1, 2, 3];
};
globalThis.Array === primordials.Array; // false

primordials.Array(0); // []
globalThis.Array(0); // [1,2,3]
```

### Prototype methods

ECMAScript provides a group of methods available on built-in objects that are
used to interact with JavaScript objects.

```js
const array = [1, 2, 3];
array.push(4); // Here `push` refers to %Array.prototype.push%.
console.log(JSON.stringify(array)); // [1,2,3,4]

// %Array.prototype%.push is modified in userland.
Array.prototype.push = function push(val) {
  return this.unshift(val);
};

array.push(5); // Now `push` refers to the modified method.
console.log(JSON.stringify(array)); // [5,1,2,3,4]
```

Primordials wrap the original prototype functions with new functions that take
the `this` value as the first argument:

```js
const {
  ArrayPrototypePush,
} = primordials;

const array = [1, 2, 3];
ArrayPrototypePush(array, 4);
console.log(JSON.stringify(array)); // [1,2,3,4]

Array.prototype.push = function push(val) {
  return this.unshift(val);
};

ArrayPrototypePush(array, 5);
console.log(JSON.stringify(array)); // [1,2,3,4,5]
```

### Safe classes

Safe classes are classes that provide the same API as their equivalent class,
but whose implementation aims to avoid any reliance on user-mutable code.
Safe classes should not be exposed to user-land; use unsafe equivalent when
dealing with objects that are accessible from user-land.

### Variadic functions

There are some built-in functions that accept a variable number of arguments
(e.g.: `Math.max`, `%Array.prototype.push%`). It is sometimes useful to provide
the list of arguments as an array. You can use primordial function with the
suffix `Apply` (e.g.: `MathMaxApply`, `ArrayPrototypePushApply`) to do that.

## Primordials with known performance issues

One of the reasons why the current Node.js API is not completely tamper-proof is
performance: sometimes the use of primordials can cause performance regressions
with V8, which when in a hot code path, could significantly decrease the
performance of code in Node.js.

* Methods that mutate the internal state of arrays:
  * `ArrayPrototypePush`
  * `ArrayPrototypePop`
  * `ArrayPrototypeShift`
  * `ArrayPrototypeUnshift`
* Methods of the function prototype:
  * `FunctionPrototypeBind`
  * `FunctionPrototypeCall`: creates performance issues when used to invoke
    super constructors.
  * `FunctionPrototype`: use `() => {}` instead when referencing a no-op
    function.
* `SafeArrayIterator`
* `SafeStringIterator`
* `SafePromiseAll`
* `SafePromiseAllSettled`
* `SafePromiseAny`
* `SafePromiseRace`
* `SafePromisePrototypeFinally`: use `try {} finally {}` block instead.
* `ReflectConstruct`: Also affects `Reflect.construct`.
  `ReflectConstruct` creates new types of classes inside functions.
  Instead consider creating a shared class. See [nodejs/performance#109](https://github.com/nodejs/performance/issues/109).

In general, when sending or reviewing a PR that makes changes in a hot code
path, use extra caution and run extensive benchmarks.

## Implicit use of user-mutable methods

### Unsafe array iteration

There are many usual practices in JavaScript that rely on iteration. It's useful
to be aware of them when dealing with arrays (or `TypedArray`s) in core as array
iteration typically calls several user-mutable methods. This sections lists the
most common patterns in which ECMAScript code relies non-explicitly on array
iteration and how to avoid it.

<details>

<summary>Avoid for-of loops on arrays</summary>

```js
for (const item of array) {
  console.log(item);
}
```

This code is internally expanded into something that looks like:

```js
{
  // 1. Lookup @@iterator property on `array` (user-mutable if user-provided).
  // 2. Lookup @@iterator property on %Array.prototype% (user-mutable).
  // 3. Call that function.
  const iterator = array[Symbol.iterator]();
  // 1. Lookup `next` property on `iterator` (doesn't exist).
  // 2. Lookup `next` property on %ArrayIteratorPrototype% (user-mutable).
  // 3. Call that function.
  let { done, value: item } = iterator.next();
  while (!done) {
    console.log(item);
    // Repeat.
    ({ done, value: item } = iterator.next());
  }
}
```

Instead of utilizing iterators, you can use the more traditional but still very
performant `for` loop:

```js
for (let i = 0; i < array.length; i++) {
  console.log(array[i]);
}
```

The following code snippet illustrates how user-land code could impact the
behavior of internal modules:

```js
// User-land
Array.prototype[Symbol.iterator] = () => ({
  next: () => ({ done: true }),
});

// Core
let forOfLoopBlockExecuted = false;
let forLoopBlockExecuted = false;
const array = [1, 2, 3];
for (const item of array) {
  forOfLoopBlockExecuted = true;
}
for (let i = 0; i < array.length; i++) {
  forLoopBlockExecuted = true;
}
console.log(forOfLoopBlockExecuted); // false
console.log(forLoopBlockExecuted); // true
```

This only applies if you are working with a genuine array (or array-like
object). If you are instead expecting an iterator, a for-of loop may be a better
choice.

</details>

<details>

<summary>Avoid array destructuring assignment on arrays</summary>

```js
const [first, second] = array;
```

This is roughly equivalent to:

```js
// 1. Lookup @@iterator property on `array` (user-mutable if user-provided).
// 2. Lookup @@iterator property on %Array.prototype% (user-mutable).
// 3. Call that function.
const iterator = array[Symbol.iterator]();
// 1. Lookup `next` property on `iterator` (doesn't exist).
// 2. Lookup `next` property on %ArrayIteratorPrototype% (user-mutable).
// 3. Call that function.
const first = iterator.next().value;
// Repeat.
const second = iterator.next().value;
```

Instead you can use object destructuring:

```js
const { 0: first, 1: second } = array;
```

or

```js
const first = array[0];
const second = array[1];
```

This only applies if you are working with a genuine array (or array-like
object). If you are instead expecting an iterator, array destructuring is the
best choice.

</details>

<details>

<summary>Avoid spread operator on arrays</summary>

```js
// 1. Lookup @@iterator property on `array` (user-mutable if user-provided).
// 2. Lookup @@iterator property on %Array.prototype% (user-mutable).
// 3. Lookup `next` property on %ArrayIteratorPrototype% (user-mutable).
const arrayCopy = [...array];
func(...array);
```

Instead you can use other ECMAScript features to achieve the same result:

```js
const arrayCopy = ArrayPrototypeSlice(array);
ReflectApply(func, null, array);
```

</details>

<details>

<summary><code>%Array.prototype.concat%</code> looks up
         <code>@@isConcatSpreadable</code> property of the passed
         arguments and the <code>this</code> value.</summary>

```js
{
  // Unsafe code example:
  // 1. Lookup @@isConcatSpreadable property on `array` (user-mutable if
  //    user-provided).
  // 2. Lookup @@isConcatSpreadable property on `%Array.prototype%
  //    (user-mutable).
  // 2. Lookup @@isConcatSpreadable property on `%Object.prototype%
  //    (user-mutable).
  const array = [];
  ArrayPrototypeConcat(array);
}
```

```js
// User-land
Object.defineProperty(Object.prototype, Symbol.isConcatSpreadable, {
  get() {
    this.push(5);
    return true;
  },
});

// Core
{
  // Using ArrayPrototypeConcat does not produce the expected result:
  const a = [1, 2];
  const b = [3, 4];
  console.log(ArrayPrototypeConcat(a, b)); // [1, 2, 5, 3, 4, 5]
}
{
  // Concatenating two arrays can be achieved safely, e.g.:
  const a = [1, 2];
  const b = [3, 4];
  // Using %Array.prototype.push% and `SafeArrayIterator` to get the expected
  // outcome:
  const concatArray = [];
  ArrayPrototypePush(concatArray, ...new SafeArrayIterator(a),
                     ...new SafeArrayIterator(b));
  console.log(concatArray); // [1, 2, 3, 4]

  // Or using `ArrayPrototypePushApply` if it's OK to mutate the first array:
  ArrayPrototypePushApply(a, b);
  console.log(a); // [1, 2, 3, 4]
}
```

</details>

<details>

<summary><code>%Object.fromEntries%</code> iterate over an array</summary>

```js
{
  // Unsafe code example:
  // 1. Lookup @@iterator property on `array` (user-mutable if user-provided).
  // 2. Lookup @@iterator property on %Array.prototype% (user-mutable).
  // 3. Lookup `next` property on %ArrayIteratorPrototype% (user-mutable).
  const obj = ObjectFromEntries(array);
}

{
  // Safe example using `SafeArrayIterator`:
  const obj = ObjectFromEntries(new SafeArrayIterator(array));
}

{
  // Safe example without using `SafeArrayIterator`:
  const obj = {};
  for (let i = 0; i < array.length; i++) {
    obj[array[i][0]] = array[i][1];
  }
  // In a hot code path, this would be the preferred method.
}
```

</details>

<details>

<summary><code>%Promise.all%</code>,
         <code>%Promise.allSettled%</code>,
         <code>%Promise.any%</code>, and
         <code>%Promise.race%</code> iterate over an array</summary>

```js
// 1. Lookup @@iterator property on `array` (user-mutable if user-provided).
// 2. Lookup @@iterator property on %Array.prototype% (user-mutable).
// 3. Lookup `next` property on %ArrayIteratorPrototype% (user-mutable).
// 4. Lookup `then` property on %Array.Prototype% (user-mutable).
// 5. Lookup `then` property on %Object.Prototype% (user-mutable).
PromiseAll([]); // unsafe

// 1. Lookup `then` property on %Array.Prototype% (user-mutable).
// 2. Lookup `then` property on %Object.Prototype% (user-mutable).
PromiseAll(new SafeArrayIterator([])); // still unsafe
SafePromiseAll([]); // still unsafe

SafePromiseAllReturnVoid([]); // safe
SafePromiseAllReturnArrayLike([]); // safe

const array = [promise];
const set = new SafeSet().add(promise);
// When running one of these functions on a non-empty iterable, it will also:
// 1. Lookup `then` property on `promise` (user-mutable if user-provided).
// 2. Lookup `then` property on `%Promise.prototype%` (user-mutable).
// 3. Lookup `then` property on %Array.Prototype% (user-mutable).
// 4. Lookup `then` property on %Object.Prototype% (user-mutable).
PromiseAll(new SafeArrayIterator(array)); // unsafe
PromiseAll(set); // unsafe

SafePromiseAllReturnVoid(array); // safe
SafePromiseAllReturnArrayLike(array); // safe

// Some key differences between `SafePromise[...]` and `Promise[...]` methods:

// 1. SafePromiseAll, SafePromiseAllSettled, SafePromiseAny, SafePromiseRace,
//    SafePromiseAllReturnArrayLike, SafePromiseAllReturnVoid, and
//    SafePromiseAllSettledReturnVoid support passing a mapperFunction as second
//    argument.
SafePromiseAll(ArrayPrototypeMap(array, someFunction));
SafePromiseAll(array, someFunction); // Same as the above, but more efficient.

// 2. SafePromiseAll, SafePromiseAllSettled, SafePromiseAny, SafePromiseRace,
//    SafePromiseAllReturnArrayLike, SafePromiseAllReturnVoid, and
//    SafePromiseAllSettledReturnVoid only support arrays and array-like
//    objects, not iterables. Use ArrayFrom to convert an iterable to an array.
SafePromiseAllReturnVoid(set); // ignores set content.
SafePromiseAllReturnVoid(ArrayFrom(set)); // works

// 3. SafePromiseAllReturnArrayLike is safer than SafePromiseAll, however you
//    should not use them when its return value is passed to the user as it can
//    be surprising for them not to receive a genuine array.
SafePromiseAllReturnArrayLike(array).then((val) => val instanceof Array); // false
SafePromiseAll(array).then((val) => val instanceof Array); // true
```

</details>

<details>

<summary><code>%Map%</code>, <code>%Set%</code>, <code>%WeakMap%</code>, and
         <code>%WeakSet%</code> constructors iterate over an array</summary>

```js
// User-land
Array.prototype[Symbol.iterator] = () => ({
  next: () => ({ done: true }),
});

// Core

// 1. Lookup @@iterator property on %Array.prototype% (user-mutable).
// 2. Lookup `next` property on %ArrayIteratorPrototype% (user-mutable).
const set = new SafeSet([1, 2, 3]);

console.log(set.size); // 0
```

```js
// User-land
Array.prototype[Symbol.iterator] = () => ({
  next: () => ({ done: true }),
});

// Core
const set = new SafeSet();
set.add(1).add(2).add(3);
console.log(set.size); // 3
```

</details>

### Promise objects

<details>

<summary><code>%Promise.prototype.finally%</code> looks up <code>then</code>
         property of the Promise instance</summary>

```js
// User-land
Promise.prototype.then = function then(a, b) {
  return Promise.resolve();
};

// Core
let finallyBlockExecuted = false;
PromisePrototypeFinally(somePromiseThatEventuallySettles,
                        () => { finallyBlockExecuted = true; });
process.on('exit', () => console.log(finallyBlockExecuted)); // false
```

```js
// User-land
Promise.prototype.then = function then(a, b) {
  return Promise.resolve();
};

// Core
let finallyBlockExecuted = false;
(async () => {
  try {
    return await somePromiseThatEventuallySettles;
  } finally {
    finallyBlockExecuted = true;
  }
})();
process.on('exit', () => console.log(finallyBlockExecuted)); // true
```

</details>

<details>

<summary><code>%Promise.all%</code>,
         <code>%Promise.allSettled%</code>,
         <code>%Promise.any%</code>, and
         <code>%Promise.race%</code> look up <code>then</code>
         property of the Promise instances</summary>

You can use safe alternatives from primordials that differ slightly from the
original methods:

* It expects an array (or array-like object) instead of an iterable.
* It wraps each promise in `SafePromise` objects and wraps the result in a new
  `Promise` instance – which may come with a performance penalty.
* It accepts a `mapperFunction` as second argument.
* Because it doesn't look up `then` property, it may not be the right tool to
  handle user-provided promises (which may be instances of a subclass of
  `Promise`).

```js
// User-land
Promise.prototype.then = function then(a, b) {
  return Promise.resolve();
};

// Core
let thenBlockExecuted = false;
PromisePrototypeThen(
  PromiseAll(new SafeArrayIterator([PromiseResolve()])),
  () => { thenBlockExecuted = true; },
);
process.on('exit', () => console.log(thenBlockExecuted)); // false
```

```js
// User-land
Promise.prototype.then = function then(a, b) {
  return Promise.resolve();
};

// Core
let thenBlockExecuted = false;
PromisePrototypeThen(
  SafePromiseAll([PromiseResolve()]),
  () => { thenBlockExecuted = true; },
);
process.on('exit', () => console.log(thenBlockExecuted)); // true
```

A common pattern is to map on the array of `Promise`s to apply some
transformations, in that case it can be more efficient to pass a second argument
rather than invoking `%Array.prototype.map%`.

```js
SafePromiseAll(ArrayPrototypeMap(array, someFunction));
SafePromiseAll(array, someFunction); // Same as the above, but more efficient.
```

</details>

### (Async) Generator functions

Generators and async generators returned by generator functions and async
generator functions are relying on user-mutable methods; their use in core
should be avoided.

<details>

<summary><code>%GeneratorFunction.prototype.prototype%.next</code> is
         user-mutable</summary>

```js
// User-land
Object.getPrototypeOf(function* () {}).prototype.next = function next() {
  return { done: true };
};

// Core
function* someGenerator() {
  yield 1;
  yield 2;
  yield 3;
}
let loopCodeExecuted = false;
for (const nb of someGenerator()) {
  loopCodeExecuted = true;
}
console.log(loopCodeExecuted); // false
```

</details>

<details>

<summary><code>%AsyncGeneratorFunction.prototype.prototype%.next</code> is
         user-mutable</summary>

```js
// User-land
Object.getPrototypeOf(async function* () {}).prototype.next = function next() {
  return new Promise(() => {});
};

// Core
async function* someGenerator() {
  yield 1;
  yield 2;
  yield 3;
}
let finallyBlockExecuted = false;
async () => {
  try {
    for await (const nb of someGenerator()) {
      // some code;
    }
  } finally {
    finallyBlockExecuted = true;
  }
};
process.on('exit', () => console.log(finallyBlockExecuted)); // false
```

</details>

### Text processing

#### Unsafe string methods

| The string method             | looks up the property |
| ----------------------------- | --------------------- |
| `String.prototype.match`      | `Symbol.match`        |
| `String.prototype.matchAll`   | `Symbol.matchAll`     |
| `String.prototype.replace`    | `Symbol.replace`      |
| `String.prototype.replaceAll` | `Symbol.replace`      |
| `String.prototype.search`     | `Symbol.search`       |
| `String.prototype.split`      | `Symbol.split`        |

```js
// User-land
RegExp.prototype[Symbol.replace] = () => 'foo';
String.prototype[Symbol.replace] = () => 'baz';

// Core
console.log(StringPrototypeReplace('ber', /e/, 'a')); // 'foo'
console.log(StringPrototypeReplace('ber', 'e', 'a')); // 'baz'
console.log(RegExpPrototypeSymbolReplace(/e/, 'ber', 'a')); // 'bar'
```

#### Unsafe string iteration

As with arrays, iterating over strings calls several user-mutable methods. Avoid
iterating over strings when possible, or use `SafeStringIterator`.

#### Unsafe `RegExp` methods

Functions that lookup the `exec` property on the prototype chain:

* `RegExp.prototype[Symbol.match]`
* `RegExp.prototype[Symbol.matchAll]`
* `RegExp.prototype[Symbol.replace]`
* `RegExp.prototype[Symbol.search]`
* `RegExp.prototype[Symbol.split]`
* `RegExp.prototype.test`

```js
// User-land
RegExp.prototype.exec = () => null;

// Core
console.log(RegExpPrototypeTest(/o/, 'foo')); // false
console.log(RegExpPrototypeExec(/o/, 'foo') !== null); // true

console.log(RegExpPrototypeSymbolSearch(/o/, 'foo')); // -1
console.log(SafeStringPrototypeSearch('foo', /o/)); // 1
```

#### Don't trust `RegExp` flags

RegExp flags are not own properties of the regex instances, which means flags
can be reset from user-land.

<details>

<summary>List of <code>RegExp</code> methods that look up properties from
         mutable getters</summary>

| `RegExp` method                | looks up the following flag-related properties                     |
| ------------------------------ | ------------------------------------------------------------------ |
| `get RegExp.prototype.flags`   | `global`, `ignoreCase`, `multiline`, `dotAll`, `unicode`, `sticky` |
| `RegExp.prototype[@@match]`    | `global`, `unicode`                                                |
| `RegExp.prototype[@@matchAll]` | `flags`                                                            |
| `RegExp.prototype[@@replace]`  | `global`, `unicode`                                                |
| `RegExp.prototype[@@split]`    | `flags`                                                            |
| `RegExp.prototype.toString`    | `flags`                                                            |

</details>

```js
// User-land
Object.defineProperty(RegExp.prototype, 'global', { value: false });

// Core
console.log(RegExpPrototypeSymbolReplace(/o/g, 'foo', 'a')); // 'fao'
console.log(RegExpPrototypeSymbolReplace(hardenRegExp(/o/g), 'foo', 'a')); // 'faa'
```

### Defining object own properties

When defining property descriptor (to add or update an own property to a
JavaScript object), be sure to always use a null-prototype object to avoid
prototype pollution.

```js
// User-land
Object.prototype.get = function get() {};

// Core
try {
  ObjectDefineProperty({}, 'someProperty', { value: 0 });
} catch (err) {
  console.log(err); // TypeError: Invalid property descriptor.
}
```

```js
// User-land
Object.prototype.get = function get() {};

// Core
ObjectDefineProperty({}, 'someProperty', { __proto__: null, value: 0 });
console.log('no errors'); // no errors.
```

Same applies when trying to modify an existing property, e.g. trying to make a
read-only property enumerable:

```js
// User-land
Object.prototype.value = 'Unrelated user-provided data';

// Core
class SomeClass {
  get readOnlyProperty() { return 'genuine data'; }
}
ObjectDefineProperty(SomeClass.prototype, 'readOnlyProperty', { enumerable: true });
console.log(new SomeClass().readOnlyProperty); // Unrelated user-provided data
```

```js
// User-land
Object.prototype.value = 'Unrelated user-provided data';

// Core
const kEnumerableProperty = { __proto__: null, enumerable: true };
// In core, use const {kEnumerableProperty} = require('internal/util');
class SomeClass {
  get readOnlyProperty() { return 'genuine data'; }
}
ObjectDefineProperty(SomeClass.prototype, 'readOnlyProperty', kEnumerableProperty);
console.log(new SomeClass().readOnlyProperty); // genuine data
```

### Defining a `Proxy` handler

When defining a `Proxy`, the handler object could be at risk of prototype
pollution when using a plain object literal:

```js
// User-land
Object.prototype.get = () => 'Unrelated user-provided data';

// Core
const objectToProxy = { someProperty: 'genuine value' };

const proxyWithPlainObjectLiteral = new Proxy(objectToProxy, {
  has() { return false; },
});
console.log(proxyWithPlainObjectLiteral.someProperty); // Unrelated user-provided data

const proxyWithNullPrototypeObject = new Proxy(objectToProxy, {
  __proto__: null,
  has() { return false; },
});
console.log(proxyWithNullPrototypeObject.someProperty); // genuine value
```

### Checking if an object is an instance of a class

#### Using `instanceof` looks up the `@@hasInstance` property of the class

```js
// User-land
Object.defineProperty(Array, Symbol.hasInstance, {
  __proto__: null,
  value: () => true,
});
Object.defineProperty(Date, Symbol.hasInstance, {
  __proto__: null,
  value: () => false,
});

// Core
const {
  FunctionPrototypeSymbolHasInstance,
} = primordials;

console.log(new Date() instanceof Array); // true
console.log(new Date() instanceof Date); // false

console.log(FunctionPrototypeSymbolHasInstance(Array, new Date())); // false
console.log(FunctionPrototypeSymbolHasInstance(Date, new Date())); // true
```

Even without user mutations, the result of `instanceof` can be deceiving when
dealing with values from different realms:

```js
const vm = require('node:vm');

console.log(vm.runInNewContext('[]') instanceof Array); // false
console.log(vm.runInNewContext('[]') instanceof vm.runInNewContext('Array')); // false
console.log([] instanceof vm.runInNewContext('Array')); // false

console.log(Array.isArray(vm.runInNewContext('[]'))); // true
console.log(vm.runInNewContext('Array').isArray(vm.runInNewContext('[]'))); // true
console.log(vm.runInNewContext('Array').isArray([])); // true
```

In general, using `instanceof` (or `FunctionPrototypeSymbolHasInstance`) checks
is not recommended, consider checking for the presence of properties or methods
for more reliable results.
