// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

'use strict';
const common = require('../common');
const assert = require('assert');
const cluster = require('cluster');

if (cluster.isWorker) {

  // Just keep the worker alive
  process.send(process.argv[2]);

} else if (cluster.isPrimary) {

  const checks = {
    args: false,
    setupEvent: false,
    settingsObject: false
  };

  const totalWorkers = 2;
  let settings;

  // Setup primary
  cluster.setupPrimary({
    args: ['custom argument'],
    silent: true
  });

  cluster.once('setup', function() {
    checks.setupEvent = true;

    settings = cluster.settings;
    if (settings?.args && settings.args[0] === 'custom argument' &&
        settings.silent === true &&
        settings.exec === process.argv[1]) {
      checks.settingsObject = true;
    }
  });

  let correctInput = 0;

  cluster.on('online', common.mustCall(function listener(worker) {

    worker.once('message', function(data) {
      correctInput += (data === 'custom argument' ? 1 : 0);
      if (correctInput === totalWorkers) {
        checks.args = true;
      }
      worker.kill();
    });

  }, totalWorkers));

  // Start all workers
  cluster.fork();
  cluster.fork();

  // Check all values
  process.once('exit', function() {
    const argsMsg = 'Arguments was not send for one or more worker. ' +
                    `${correctInput} workers receive argument, ` +
                    `but ${totalWorkers} were expected.`;
    assert.ok(checks.args, argsMsg);

    assert.ok(checks.setupEvent, 'The setup event was never emitted');

    const settingObjectMsg = 'The settingsObject do not have correct ' +
                             `properties : ${JSON.stringify(settings)}`;
    assert.ok(checks.settingsObject, settingObjectMsg);
  });

}
