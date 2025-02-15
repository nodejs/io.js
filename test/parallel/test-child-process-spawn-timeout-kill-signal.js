'use strict';

const { mustCall } = require('../common');
const { strictEqual, throws } = require('assert');
const fixtures = require('../common/fixtures');
const { spawn } = require('child_process');
const { getEventListeners } = require('events');

const aliveForeverFile = 'child-process-stay-alive-forever.js';
{
  // Verify default signal + closes
  const cp = spawn(process.execPath, [fixtures.path(aliveForeverFile)], {
    timeout: 5,
  });
  cp.on('exit', mustCall((code, ks) => strictEqual(ks, 'SIGTERM')));
}

{
  // Verify SIGKILL signal + closes
  const cp = spawn(process.execPath, [fixtures.path(aliveForeverFile)], {
    timeout: 6,
    killSignal: 'SIGKILL',
  });
  cp.on('exit', mustCall((code, ks) => strictEqual(ks, 'SIGKILL')));
}

{
  // Verify timeout verification
  throws(() => spawn(process.execPath, [fixtures.path(aliveForeverFile)], {
    timeout: 'badValue',
  }), /ERR_OUT_OF_RANGE/);

  throws(() => spawn(process.execPath, [fixtures.path(aliveForeverFile)], {
    timeout: {},
  }), /ERR_OUT_OF_RANGE/);
}

{
  // Verify timedOut flag is set true
  const cp = spawn(process.execPath, [fixtures.path(aliveForeverFile)], {
    timeout: 6,
  });
  strictEqual(cp.timedOut, false);
  cp.on('exit', mustCall(() => {
    strictEqual(cp.timedOut, true);
  }));
}

{
  // Verify abort signal gets unregistered
  const controller = new AbortController();
  const { signal } = controller;
  const cp = spawn(process.execPath, [fixtures.path(aliveForeverFile)], {
    timeout: 6,
    signal,
  });
  strictEqual(getEventListeners(signal, 'abort').length, 1);
  cp.on('exit', mustCall(() => {
    strictEqual(getEventListeners(signal, 'abort').length, 0);
  }));
}
