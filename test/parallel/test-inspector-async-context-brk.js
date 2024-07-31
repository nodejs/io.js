'use strict';
const common = require('../common');
const { AsyncLocalStorage } = require('async_hooks');
const als = new AsyncLocalStorage();

function getStore() {
  return als.getStore();
}

common.skipIfInspectorDisabled();

const assert = require('assert');
const { Session } = require('inspector');
const path = require('path');
const { pathToFileURL } = require('url');

let valueInFunction = 0;
let valueInBreakpoint = 0;

function debugged() {
  valueInFunction = getStore();
  return 42;
}

async function test() {
  const session = new Session();

  session.connect();
  session.post('Debugger.enable');

  session.on('Debugger.paused', () => {
    valueInBreakpoint = getStore();
  });

  await new Promise((resolve, reject) => {
    session.post('Debugger.setBreakpointByUrl', {
      'lineNumber': 22,
      'url': pathToFileURL(path.resolve(__dirname, __filename)).toString(),
      'columnNumber': 0,
      'condition': ''
    }, (error, result) => {
      return error ? reject(error) : resolve(result);
    });
  });

  als.run(1, debugged);
  assert.strictEqual(valueInFunction, valueInBreakpoint);
  assert.strictEqual(valueInFunction, 1);

  session.disconnect();
}

const interval = setInterval(() => {}, 1000);
test().then(common.mustCall(() => {
  clearInterval(interval);
}));
