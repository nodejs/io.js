import { mustCall, PIPE } from '../common/index.mjs';
import { strictEqual, ok } from 'assert';
import tick from '../common/tick.js';
import initHooks from './init-hooks.mjs';
import { checkInvocations } from './hook-checks.mjs';
import { refresh } from '../common/tmpdir.js';
import { createServer, connect } from 'net';
import process from 'process';

refresh();

const hooks = initHooks();
hooks.enable();
let pipe1, pipe2;
let pipeserver;
let pipeconnect;

const server = createServer(mustCall((c) => {
  c.end();
  server.close();
  process.nextTick(maybeOnconnect.bind(null, 'server'));
})).listen(PIPE, mustCall(onlisten));

function onlisten() {
  const pipeservers = hooks.activitiesOfTypes('PIPESERVERWRAP');
  let pipeconnects = hooks.activitiesOfTypes('PIPECONNECTWRAP');
  strictEqual(pipeservers.length, 1);
  strictEqual(pipeconnects.length, 0);

  connect(PIPE,
              mustCall(maybeOnconnect.bind(null, 'client')));

  const pipes = hooks.activitiesOfTypes('PIPEWRAP');
  pipeconnects = hooks.activitiesOfTypes('PIPECONNECTWRAP');
  strictEqual(pipes.length, 1);
  strictEqual(pipeconnects.length, 1);

  pipeserver = pipeservers[0];
  pipe1 = pipes[0];
  pipeconnect = pipeconnects[0];

  strictEqual(pipeserver.type, 'PIPESERVERWRAP');
  strictEqual(pipe1.type, 'PIPEWRAP');
  strictEqual(pipeconnect.type, 'PIPECONNECTWRAP');
  for (const a of [ pipeserver, pipe1, pipeconnect ]) {
    strictEqual(typeof a.uid, 'number');
    strictEqual(typeof a.triggerAsyncId, 'number');
    checkInvocations(a, { init: 1 }, 'after net.connect');
  }
}

const awaitOnconnectCalls = new Set(['server', 'client']);
function maybeOnconnect(source) {
  // Both server and client must call onconnect. On most OS's waiting for
  // the client is sufficient, but on CentOS 5 the sever needs to respond too.
  ok(awaitOnconnectCalls.size > 0);
  awaitOnconnectCalls.delete(source);
  if (awaitOnconnectCalls.size > 0) return;

  const pipes = hooks.activitiesOfTypes('PIPEWRAP');
  const pipeconnects = hooks.activitiesOfTypes('PIPECONNECTWRAP');

  strictEqual(pipes.length, 2);
  strictEqual(pipeconnects.length, 1);
  pipe2 = pipes[1];
  strictEqual(typeof pipe2.uid, 'number');
  strictEqual(typeof pipe2.triggerAsyncId, 'number');

  checkInvocations(pipeserver, { init: 1, before: 1, after: 1 },
                   'pipeserver, client connected');
  checkInvocations(pipe1, { init: 1 }, 'pipe1, client connected');
  checkInvocations(pipeconnect, { init: 1, before: 1 },
                   'pipeconnect, client connected');
  checkInvocations(pipe2, { init: 1 }, 'pipe2, client connected');
  tick(5);
}

process.on('exit', onexit);

function onexit() {
  hooks.disable();
  hooks.sanityCheck('PIPEWRAP');
  hooks.sanityCheck('PIPESERVERWRAP');
  hooks.sanityCheck('PIPECONNECTWRAP');
  // TODO(thlorenz) why have some of those 'before' and 'after' called twice
  checkInvocations(pipeserver, { init: 1, before: 1, after: 1, destroy: 1 },
                   'pipeserver, process exiting');
  checkInvocations(pipe1, { init: 1, before: 2, after: 2, destroy: 1 },
                   'pipe1, process exiting');
  checkInvocations(pipeconnect, { init: 1, before: 1, after: 1, destroy: 1 },
                   'pipeconnect, process exiting');
  checkInvocations(pipe2, { init: 1, before: 2, after: 2, destroy: 1 },
                   'pipe2, process exiting');
}
