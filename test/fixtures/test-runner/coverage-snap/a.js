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
