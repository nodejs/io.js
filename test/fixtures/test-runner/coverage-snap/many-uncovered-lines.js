'use strict';
// Here we can't import common module as the coverage will be different based on the system

// Empty functions that don't do anything
function doNothing1() {
  // Not implemented
}

function doNothing2() {
  // No logic here
}

function unusedFunction1() {
  // Intentionally left empty
}

function unusedFunction2() {
  // Another empty function
}

// Unused variables
const unusedVariable1 = 'This is never used';
const unusedVariable2 = 42;
let unusedVariable3;

// Empty class with no methods
class UnusedClass {
  constructor() {
    // Constructor does nothing
  }
}

// Empty object literal
const emptyObject = {};

// Empty array
const emptyArray = [];

// Function with parameters but no body
function doNothingWithParams(param1, param2) {
  // No implementation
}

// Function that returns nothing
function returnsNothing() {
  // No return statement
}

// Another unused function
function unusedFunction3() {
  // More empty code
}

// Empty functions with different signatures
function doNothing4() {
  // No logic here
}

function doNothing5() {
  // Another placeholder
}

function doNothingWithRestParams(...args) {
  // Function with rest parameters but no logic
}

function doNothingWithCallback(callback) {
  // Callback not called
}

// Unused variables of different types
const unusedVariable7 = null;
const unusedVariable8 = undefined;
const unusedVariable9 = Symbol('unused');
let unusedVariable10;

// Another empty class
class YetAnotherUnusedClass {
  // No properties or methods
}

// Unused object with nested array
const unusedObjectWithArray = {
  innerArray: [],
};

// Another empty array
const anotherEmptyArray = [];

// Function with default and rest parameters
function anotherComplexFunction(param1 = 10, ...rest) {
  // No implementation
}

// More unused functions
function unusedFunction5() {
  // Placeholder
}

function unusedFunction6() {
  // Empty again
}

function unusedFunction7() {
  // Still nothing here
}

// Empty static method in class
class UnusedClassWithStaticMethod {
  static unusedStaticMethod() {
    // No logic inside static method
  }
}

// Empty getter and setter in a class
class ClassWithGetterSetter {
  get unusedGetter() {
    // Empty getter
  }

  set unusedSetter(value) {
    // Empty setter
  }
}

// Unused function returning undefined
function returnsUndefined() {
  return undefined;
}

// Empty promise-returning function
function emptyPromiseFunction() {
  return new Promise((resolve, reject) => {
    // No promise logic
  });
}

// Empty immediately-invoked function expression (IIFE)
(function emptyIIFE() {
  // No implementation
})();

// Unused generator function with parameters
function* unusedGeneratorFunctionWithParams(param1, param2) {
  // No yielding of values
}

// Unused async arrow function
const unusedAsyncArrowFunction = async () => {
  // No async logic here
};

// Unused map function with no logic
const unusedMapFunction = new Map();

// Unused set function with no logic
const unusedSetFunction = new Set();

// Empty for loop
for (let i = 0; i < 10; i++) {
  // Loop does nothing
}

// Empty while loop
while (false) {
  // Never executes
}

// Empty try-catch-finally block
try {
  // Nothing to try
} catch (error) {
  // No error handling
} finally {
  // Nothing to finalize
}

// Empty if-else block
if (false) {
  // No logic here
} else {
  // Nothing here either
}

// Empty switch statement
switch (false) {
  case true:
    // No case logic
    break;
  default:
    // Default does nothing
    break;
}

// Empty nested function
function outerFunction() {
  function innerFunction() {
    // Empty inner function
  }
}

// More unused arrow functions
const unusedArrowFunction1 = () => {};
const unusedArrowFunction2 = (param) => {};

// Empty function with a try-catch block
function emptyTryCatchFunction() {
  try {
    // Nothing to try
  } catch (error) {
    // No error handling
  }
}

// Unused async generator function
async function* unusedAsyncGenerator() {
  // No async yielding
}

// Unused function returning an empty array
function returnsEmptyArray() {
  return [];
}

// Unused function returning an empty object
function returnsEmptyObject() {
  return {};
}

// Empty arrow function with destructuring
const unusedArrowFunctionWithDestructuring = ({ key1, key2 }) => {
  // No logic here
};

// Unused function with default parameters
function unusedFunctionWithDefaults(param1 = 'default', param2 = 100) {
  // No implementation
}

// Unused function returning an empty Map
function returnsEmptyMap() {
  return new Map();
}

// Unused function returning an empty Set
function returnsEmptySet() {
  return new Set();
}

// Unused async function with try-catch
async function unusedAsyncFunctionWithTryCatch() {
  try {
    // Nothing to try
  } catch (error) {
    // No error handling
  }
}

// Function with spread operator but no logic
function unusedFunctionWithSpread(...args) {
  // No implementation
}

// Function with object spread but no logic
function unusedFunctionWithObjectSpread(obj) {
  const newObj = { ...obj };
  // No logic here
}

// Empty function that takes an array and does nothing
function unusedFunctionWithArrayParam(arr) {
  // No logic here
}

// Empty function that returns a function
function unusedFunctionReturningFunction() {
  return function() {
    // Empty function returned
  };
}

// Empty function with destructuring and rest parameters
function unusedFunctionWithDestructuringAndRest({ a, b, ...rest }) {
  // No logic here
}

// Unused async function that returns a promise
async function unusedAsyncFunctionReturningPromise() {
  return Promise.resolve();
}

// Empty recursive function
function unusedRecursiveFunction() {
  // No recursive logic
}

// Empty function using template literals
function unusedFunctionWithTemplateLiterals(param) {
  const message = `Message: ${param}`;
  // No further logic
}

// Function with complex destructuring and no logic
function unusedComplexDestructuringFunction({ a: { b, c }, d }) {
  // No logic here
}

// Empty function that uses bitwise operations
function unusedFunctionWithBitwiseOperations(num) {
  const result = num & 0xff;
  // No further logic
}

// Unused function with optional chaining
function unusedFunctionWithOptionalChaining(obj) {
  const value = obj?.property?.nestedProperty;
  // No further logic
}

// Unused function with nullish coalescing operator
function unusedFunctionWithNullishCoalescing(param) {
  const value = param ?? 'default';
  // No further logic
}

// Unused generator function returning nothing
function* unusedGeneratorFunction() {
  // No yielding
}

// Function using try-catch with finally, but no logic
function unusedFunctionWithTryCatchFinally() {
  try {
    // Nothing to try
  } catch (e) {
    // No error handling
  } finally {
    // Nothing in finally
  }
}

// Function with chaining but no logic
function unusedFunctionWithChaining() {
  const obj = {
    method1: () => ({ method2: () => {} }),
  };
  obj.method1().method2();
}

// Empty function returning a new Date object
function unusedFunctionReturningDate() {
  return new Date();
}
