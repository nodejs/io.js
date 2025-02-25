// Flags: --test-udp-no-try-send
import { mustCall } from '../common/index.mjs';
import { strictEqual } from 'assert';
import process from 'process';
import initHooks from './init-hooks.mjs';
import { checkInvocations } from './hook-checks.mjs';
import { createSocket } from 'dgram';

const hooks = initHooks();

hooks.enable();
let send;

const sock = createSocket('udp4')
  .on('listening', mustCall(onlistening))
  .bind();

function onlistening() {
  sock.send(
    Buffer.alloc(2), 0, 2, sock.address().port,
    undefined, mustCall(onsent));

  // Init not called synchronously because dns lookup always wraps
  // callback in a next tick even if no lookup is needed
  // TODO (trevnorris) submit patch to fix creation of tick objects and instead
  // create the send wrap synchronously.
  strictEqual(hooks.activitiesOfTypes('UDPSENDWRAP').length, 0);
}

function onsent() {
  const as = hooks.activitiesOfTypes('UDPSENDWRAP');
  send = as[0];

  strictEqual(as.length, 1);
  strictEqual(send.type, 'UDPSENDWRAP');
  strictEqual(typeof send.uid, 'number');
  strictEqual(typeof send.triggerAsyncId, 'number');
  checkInvocations(send, { init: 1, before: 1 }, 'when message sent');

  sock.close(mustCall(onsockClosed));
}

function onsockClosed() {
  checkInvocations(send, { init: 1, before: 1, after: 1 }, 'when sock closed');
}

process.on('exit', onexit);

function onexit() {
  hooks.disable();
  hooks.sanityCheck('UDPSENDWRAP');
  checkInvocations(send, { init: 1, before: 1, after: 1, destroy: 1 },
                   'when process exits');
}
