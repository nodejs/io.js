'use strict';

const common = require('../common');
const { getCallSites } = require('node:util');
const assert = require('node:assert');

const bench = common.createBenchmark(main, {
  n: [1e6],
  method: ['ErrorCallSites', 'ErrorCallSitesSerialized', 'CPP'],
});

function ErrorGetCallSites() {
  const originalStackFormatter = Error.prepareStackTrace;
  Error.prepareStackTrace = (_err, stack) => {
    if (stack && stack.length > 1) {
      // Remove node:util
      return stack.slice(1);
    }
    return stack;
  };
  const err = new Error();
  // With the V8 Error API, the stack is not formatted until it is accessed
  err.stack; // eslint-disable-line no-unused-expressions
  Error.prepareStackTrace = originalStackFormatter;
  return err.stack;
}

function ErrorCallSitesSerialized() {
  const callSites = ErrorGetCallSites();
  const serialized = [];
  for (let i = 0; i < callSites.length; ++i) {
    serialized.push({
      functionName: callSites[i].getFunctionName(),
      scriptName: callSites[i].getFileName(),
      lineNumber: callSites[i].getLineNumber(),
      column: callSites[i].getColumnNumber(),
    });
  }
  return serialized;
}

function main({ n, method }) {
  let fn;
  switch (method) {
    case 'ErrorCallSites':
      fn = ErrorGetCallSites;
      break;
    case 'ErrorCallSitesSerialized':
      fn = ErrorCallSitesSerialized;
      break;
    case 'CPP':
      fn = getCallSites;
      break;
  }
  let lastStack = {};

  bench.start();
  for (let i = 0; i < n; i++) {
    const stack = fn();
    lastStack = stack;
  }
  bench.end(n);
  // Attempt to avoid dead-code elimination
  assert.ok(lastStack);
}
