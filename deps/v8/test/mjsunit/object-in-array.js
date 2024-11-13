// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-lazy-feedback-allocation --allow-natives-syntax

// The expected token (has to be a SMI so that we get SMI_ELEMENTS when
// it's in an array):
const T = 1234;

// The unexpected token:
const T2 = 'unexpected';

function warmup(f) {
  %PrepareFunctionForOptimization(f);
  for (let i = 0; i < 10; i++) {
    f();
  }
}

function test(creator, getter, setter, expectedToken = T) {
  // Warm up the creator function so there's feedback and the boilerplate
  // gets created.
  warmup(creator);

  const o1 = creator();
  assertEquals(expectedToken, getter(o1));

  if (setter == undefined) {
     return;
  }

  // Test that we did a deep copy by setting a field in o1, creating another
  // object, and asserting that it got a fresh copy of the data.
  setter(o1);
  assertNotEquals(expectedToken, getter(o1));

  const o2 = creator();
  assertEquals(expectedToken, getter(o2));
}

// Shallow array:
test(() => { return [T, 2, 3]; }, (o) => o[0], (o) => o[0] = T2);

// Shallow object:
test(() => { return {a: 1, b: T}; }, (o) => o.b, (o) => o.b = T2);

// Shallow object in array:
test(() => { return [0, {a: 10, b: T, c: 20 }, 1]; },
     (o) => o[1].b, (o) => o[1].b = T2);

// Nested array:
test(() => { return [0, 0, [1, 2, 3, T]]; },
     (o) => o[2][3], (o) => o[2][3] = T2);

// Nested object
test(() => { return {a: 0, b: 0, c: {d: 1, e: 2, f: 3, g: T}}; },
     (o) => o.c.g, (o) => o.c.g = T2);

// Array containing an object containing an array:
test(() => { return [{a: [1, 2, T]}]; },
     (o) => o[0].a[2], (o) => o[0].a[2] = T2);

// Object containing an object containing an array:
test(() => { return {a: {b: [1, 2, T]}}; },
(o) => o.a.b[2], (o) => o.a.b[2] = T2);

// Object with both properties and elements in array:
test(() => { return [{a: [1, 2, T], 0: 3}]; },
     (o) => o[0].a[2], (o) => o[0].a[2] = T2);

test(() => { return [{0: 3, a: [1, 2, T]}]; },
     (o) => o[0].a[2], (o) => o[0].a[2] = T2);

test(() => { return [{0: [1, 2, T], a: 3}]; },
     (o) => o[0][0][2], (o) => o[0][0][2] = T2);

test(() => { return [{a: 3, 0: [1, 2, T]}]; },
     (o) => o[0][0][2], (o) => o[0][0][2] = T2);

// Deep object in array:
test(() => { return [{a: {b: {c: T}}}]; },
     (o) => o[0].a.b.c, (o) => o[0].a.b.c = T2);

// Deep array in object:
test(() => { return {a: [[[T]]]}; },
     (o) => o.a[0][0][0], (o) => o.a[0][0][0] = T2);

// Object and arrays in array. Interestingly, {a: T} uses the AllocationSite
// of the main array, but {b: 2} uses the AllocationSite of [3, 4].
test(() => { return [1, 2, {a: T}, [3, 4], {b: 2}]; },
     (o) => o[2].a, (o) => o[2].a = T2);

test(() => { return [1, 2, {a: 1}, [3, 4], {b: T}]; },
     (o) => o[4].b, (o) => o[4].b = T2);

// Object with HeapNumbers in array:
test(() => { return [{a: 3.14159}] },
     (o) => o[0].a, (o) => o[0].a = T2, 3.14159);

// Empty array in array:
test(() => { return [[]]; }, (o) => o[0], (o) => o[0] = T2, []);

// Empty array in object:
test(() => { return {a: []}; }, (o) => o.a, (o) => o.a = T2, []);

// Empty array in object in array:
test(() => { return [{a: []}]; }, (o) => o[0].a, (o) => o[0].a = T2, []);

// Empty object in array:
test(() => { return [{}]; }, (o) => o[0], (o) => o[0] = T2, {});

// Different elements kinds in nested arrays:

// PACKED_SMI_ELEMENTS:
test(() => { return [[1, 2]];}, (o) => o[0][0], (o) => o[0][0] = 2, 1);

// HOLEY_SMI_ELEMENTS:
{
  function createHoleySmiArrayInArray() {
     return [[1, , 2]];
  }
  test(createHoleySmiArrayInArray, (o) => o[0][0], (o) => o[0][0] = 2, 1);
  const o1 = createHoleySmiArrayInArray();
  assertTrue(0 in o1[0]);
  assertFalse(1 in o1[0]);
  assertTrue(2 in o1[0]);
}

// PACKED_DOUBLE_ELEMENTS:
test(() => { return [[1, 2.3]];}, (o) => o[0][0], (o) => o[0][0] = 2, 1);

// HOLEY_DOUBLE_ELEMENTS:
{
  function createHoleyDoubleArrayInArray() {
     return [[1, , 2.3]];
  }
  test(createHoleyDoubleArrayInArray, (o) => o[0][0], (o) => o[0][0] = 2, 1);
  const o1 = createHoleyDoubleArrayInArray();
  assertTrue(0 in o1[0]);
  assertFalse(1 in o1[0]);
  assertTrue(2 in o1[0]);
}

// PACKED_ELEMENTS:
test(() => { return [['a', 'b']];}, (o) => o[0][0], (o) => o[0][0] = 'c', 'a');

// HOLEY_ELEMENTS:
{
  function createHoleyArrayInArray() {
     return [['a', , 'b']];
  }
  test(createHoleyArrayInArray, (o) => o[0][0], (o) => o[0][0] = 'c', 'a');
  const o1 = createHoleyArrayInArray();
  assertTrue(0 in o1[0]);
  assertFalse(1 in o1[0]);
  assertTrue(2 in o1[0]);
}

// Object literal with a custom __proto__. These are handled outside of the
// boilerplate cloning code; this test is here to make sure we don't break
// this case.
{
  function createObjectWithCustomProtoInArray() {
    return [{__proto__: {a: T}, b: 'b'}];
  }
  test(createObjectWithCustomProtoInArray,
       (o) => o[0].__proto__.a, (o) => o[0].__proto__.a = T2);
  const o1 = createObjectWithCustomProtoInArray();
  const o2 = createObjectWithCustomProtoInArray();
  assertNotSame(o1[0].__proto__, o2[0].__proto__);
  assertEquals(T, o1[0].a);
  assertEquals(T, o2[0].a);
}

// Similarly, objects with accessors are handled outside.
test(
    () => { return [
        {get a() { return this.a; }, set a(v) { this.a = v;}, a: T }
        ]; },
    (o) => o[0].a, (o) => o[0].a = T2);

// Similarly, function-valued properties are handled outside.
{
  function createObjectWithFunctionInArray() {
     return [{a: function() { return 0; }}];
  }
  const o1 = createObjectWithFunctionInArray();
  const o2 = createObjectWithFunctionInArray();
  assertNotSame(o1[0].a, o2[0].a);
  assertEquals(0, o1[0].a());
  assertEquals(0, o2[0].a());
}

// Dictionary mode object in array:
{
  function createObjectWithNullProtoInArray() {
    return [{__proto__: null, b: T}];
  }
  test(createObjectWithNullProtoInArray,
       (o) => o[0].b, (o) => o[0].b = T2);
  const o1 = createObjectWithNullProtoInArray();
  assertFalse(%HasFastProperties(o1[0]));
  assertEquals(null, Object.getPrototypeOf(o1[0]));
}

// Object with out-of-object properties in array:
function createLargeObjectInArrayCreator(size) {
  let code = "() => { return [{ ";
  for (let i = 0; i < size; i++) {
    if (i > 0) code += ",";
    code += 'a' + i + ':' + i;
  }
  code += "}];}";
  return eval(code);
}

test(createLargeObjectInArrayCreator(500),
     (o) => o[0].a140, (o) => o[0].a140 = T2, 140);

// Array with an object refrence (not object literal; should not be cloned).
// Likewise, the array boilerplate won't contain the object, but it's
// added outside.
{
   const outsideObject = {a: T, b: 2};
   function createObjectInNestedArray() {
     return [[outsideObject]];
   }
   test(createObjectInNestedArray, (o) => o[0][0].a);

   const o1 = createObjectInNestedArray();
   assertEquals(2, o1[0][0].b);
   outsideObject.b = 'new';
   assertEquals('new', o1[0][0].b);
}

// Object with a deprecated map in an array:
{
  const outside = {prop1: 123};
  function createObjectWithDeprecatedMapInArray() {
     return [{prop1: 123}];
  }
  test(createObjectWithDeprecatedMapInArray, o => o[0].prop1, undefined, 123);
  outside.prop1 = 3.4;
  test(createObjectWithDeprecatedMapInArray, o => o[0].prop1, undefined, 123);
}

// Assert we keep elements COW.
{
  function createNestedArray() {
     return [[1, 2, 3]];
  }
  warmup(createNestedArray);
  const o1 = createNestedArray();
  assertTrue(%HasCowElements(o1[0]));
}

{
  function createNestedArray() {
     return [[1, 2, , 3]];
  }
  warmup(createNestedArray);
  const o1 = createNestedArray();
  assertTrue(%HasCowElements(o1[0]));
  assertFalse(2 in o1);
}

{
  function createNestedArray() {
     return [[1, 2, 3, 'force PACKED_ELEMENTS']];
  }
  warmup(createNestedArray);
  const o1 = createNestedArray();
  assertTrue(%HasCowElements(o1[0]));
}

{
  function createNestedArray() {
     return [[1, 2,  , 3, 'force HOLEY_PACKED_ELEMENTS']];
  }
  warmup(createNestedArray);
  const o1 = createNestedArray();
  assertTrue(%HasCowElements(o1[0]));
  assertFalse(2 in o1);
}

{
  function createArrayInObjectInArray() {
     return [{a: [1, 2, 3] }];
  }
  warmup(createArrayInObjectInArray);
  const o1 = createArrayInObjectInArray();
  assertTrue(%HasCowElements(o1[0].a));
}

// Object with dictionary elements in array:
test(() => { return [{0: T, 10: 5, 1000000000: 5}]; },
     (o) => o[0][0], (o) => o[0][0] = T2);
