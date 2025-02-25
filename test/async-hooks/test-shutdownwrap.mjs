import { mustCall } from '../common/index.mjs';
import { strictEqual } from 'assert';
import process from 'process';
import initHooks from './init-hooks.mjs';
import { checkInvocations } from './hook-checks.mjs';

import { createServer, connect } from 'net';

const hooks = initHooks();
hooks.enable();

const server = createServer(onconnection)
  .on('listening', mustCall(onlistening));
server.listen();
function onlistening() {
  connect(server.address().port, mustCall(onconnected));
}

// It is non-deterministic in which order onconnection and onconnected fire.
// Therefore we track here if we ended the connection already or not.
let endedConnection = false;
function onconnection(c) {
  strictEqual(hooks.activitiesOfTypes('SHUTDOWNWRAP').length, 0);
  c.end();
  process.nextTick(() => {
    endedConnection = true;
    const as = hooks.activitiesOfTypes('SHUTDOWNWRAP');
    strictEqual(as.length, 1);
    checkInvocations(as[0], { init: 1 }, 'after ending client connection');
    this.close(onserverClosed);
  });
}

function onconnected() {
  if (endedConnection) {
    strictEqual(hooks.activitiesOfTypes('SHUTDOWNWRAP').length, 1);

  } else {
    strictEqual(hooks.activitiesOfTypes('SHUTDOWNWRAP').length, 0);
  }
}

function onserverClosed() {
  const as = hooks.activitiesOfTypes('SHUTDOWNWRAP');
  checkInvocations(as[0], { init: 1, before: 1, after: 1, destroy: 1 },
                   'when server closed');
}

process.on('exit', onexit);

function onexit() {
  hooks.disable();
  hooks.sanityCheck('SHUTDOWNWRAP');
  const as = hooks.activitiesOfTypes('SHUTDOWNWRAP');
  const a = as[0];
  strictEqual(a.type, 'SHUTDOWNWRAP');
  strictEqual(typeof a.uid, 'number');
  strictEqual(typeof a.triggerAsyncId, 'number');
  checkInvocations(as[0], { init: 1, before: 1, after: 1, destroy: 1 },
                   'when process exits');
}
