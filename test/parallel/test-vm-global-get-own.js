'use strict';
require('../common');
const assert = require('assert');
const vm = require('vm');

// These assertions check that we can set new keys to the global context,
// get them back and also list them via getOwnProperty* or in.
//
// Related to:
// - https://github.com/nodejs/node/issues/45983

const contextGlobal = vm.runInContext('this', vm.createContext());

function runAssertions(data, property, viaDefine, value1, value2, value3) {
  // Define the property for the first time
  setPropertyAndAssert(data, property, viaDefine, value1);
  // Update the property
  setPropertyAndAssert(data, property, viaDefine, value2);
  // Delete the property
  deletePropertyAndAssert(data, property);
  // Re-define the property
  setPropertyAndAssert(data, property, viaDefine, value3);
  // Delete the property again
  deletePropertyAndAssert(data, property);
}

const fun1 = () => 1;
const fun2 = () => 2;
const fun3 = () => 3;

function runAssertionsOnSandbox(builder) {
  const sandboxContext = vm.createContext({ runAssertions, fun1, fun2, fun3 });
  vm.runInContext(builder('this'), sandboxContext);
  vm.runInContext(builder('{}'), sandboxContext);
}

// Assertions on: define property
runAssertions(contextGlobal, 'toto', true, 1, 2, 3);
runAssertions(contextGlobal, Symbol.for('toto'), true, 1, 2, 3);
runAssertions(contextGlobal, 'tutu', true, fun1, fun2, fun3);
runAssertions(contextGlobal, Symbol.for('tutu'), true, fun1, fun2, fun3);
runAssertions(contextGlobal, 'tyty', true, fun1, 2, 3);
runAssertions(contextGlobal, Symbol.for('tyty'), true, fun1, 2, 3);

// Assertions on: direct assignment
runAssertions(contextGlobal, 'titi', false, 1, 2, 3);
runAssertions(contextGlobal, Symbol.for('titi'), false, 1, 2, 3);
runAssertions(contextGlobal, 'tata', false, fun1, fun2, fun3);
runAssertions(contextGlobal, Symbol.for('tata'), false, fun1, fun2, fun3);
runAssertions(contextGlobal, 'tztz', false, fun1, 2, 3);
runAssertions(contextGlobal, Symbol.for('tztz'), false, fun1, 2, 3);

// Assertions on: define property from sandbox
runAssertionsOnSandbox(
  (variable) => `
    runAssertions(${variable}, 'toto', true, 1, 2, 3);
    runAssertions(${variable}, Symbol.for('toto'), true, 1, 2, 3);
    runAssertions(${variable}, 'tutu', true, fun1, fun2, fun3);
    runAssertions(${variable}, Symbol.for('tutu'), true, fun1, fun2, fun3);
    runAssertions(${variable}, 'tyty', true, fun1, 2, 3);
    runAssertions(${variable}, Symbol.for('tyty'), true, fun1, 2, 3);`
);

// Assertions on: direct assignment from sandbox
runAssertionsOnSandbox(
  (variable) => `
    runAssertions(${variable}, 'titi', false, 1, 2, 3);
    runAssertions(${variable}, Symbol.for('titi'), false, 1, 2, 3);
    runAssertions(${variable}, 'tata', false, fun1, fun2, fun3);
    runAssertions(${variable}, Symbol.for('tata'), false, fun1, fun2, fun3);
    runAssertions(${variable}, 'tztz', false, fun1, 2, 3);
    runAssertions(${variable}, Symbol.for('tztz'), false, fun1, 2, 3);`
);

// Helpers

// Set the property on data and assert it worked
function setPropertyAndAssert(data, property, viaDefine, value) {
  if (viaDefine) {
    Object.defineProperty(data, property, {
      enumerable: true,
      writable: true,
      value: value,
      configurable: true,
    });
  } else {
    data[property] = value;
  }
  assert.strictEqual(data[property], value);
  assert.ok(property in data);
  if (typeof property === 'string') {
    assert.ok(Object.getOwnPropertyNames(data).includes(property));
  } else {
    assert.ok(Object.getOwnPropertySymbols(data).includes(property));
  }
}

// Delete the property from data and assert it worked
function deletePropertyAndAssert(data, property) {
  delete data[property];
  assert.strictEqual(data[property], undefined);
  assert.ok(!(property in data));
  assert.ok(!Object.getOwnPropertyNames(data).includes(property));
  assert.ok(!Object.getOwnPropertySymbols(data).includes(property));
}
