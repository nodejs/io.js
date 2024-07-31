'use strict';
const common = require('../common');

common.skipIfInspectorDisabled();

const assert = require('assert');
const { Session } = require('inspector');
const path = require('path');
const { pathToFileURL } = require('url');

function debugged() {
  return 42;
}

async function test() {
  const session1 = new Session();
  const session2 = new Session();

  session1.connect();
  session2.connect();

  let session1Paused = false;
  let session2Paused = false;

  session1.on('Debugger.paused', () => session1Paused = true);
  session2.on('Debugger.paused', () => session2Paused = true);

  console.log('Connected');

  session1.post('Debugger.enable');
  session2.post('Debugger.enable');
  console.log('Debugger was enabled');

  await new Promise((resolve, reject) => {
    session1.post('Debugger.setBreakpointByUrl', {
      'lineNumber': 12,
      'url': pathToFileURL(path.resolve(__dirname, __filename)).toString(),
      'columnNumber': 0,
      'condition': ''
    }, (error, result) => {
      return error ? reject(error) : resolve(result);
    });
  });
  console.log('Breakpoint was set');

  debugged();

  // Both sessions will receive the paused event
  assert(session1Paused);
  assert(session2Paused);
  console.log('Breakpoint was hit');

  session1.disconnect();
  session2.disconnect();
  console.log('Sessions were disconnected');
}

const interval = setInterval(() => {}, 1000);
test().then(common.mustCall(() => {
  clearInterval(interval);
  console.log('Done!');
}));
