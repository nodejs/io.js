import { mustCall } from '../common/index.mjs';
import { strictEqual } from 'assert';
import process from 'process';
import tick from '../common/tick.js';
import initHooks from './init-hooks.mjs';
import { checkInvocations } from './hook-checks.mjs';
import { createSocket } from 'dgram';

const hooks = initHooks();

hooks.enable();
const sock = createSocket('udp4');

const as = hooks.activitiesOfTypes('UDPWRAP');
const udpwrap = as[0];
strictEqual(as.length, 1);
strictEqual(udpwrap.type, 'UDPWRAP');
strictEqual(typeof udpwrap.uid, 'number');
strictEqual(typeof udpwrap.triggerAsyncId, 'number');
checkInvocations(udpwrap, { init: 1 }, 'after dgram.createSocket call');

sock.close(mustCall(onsockClosed));

function onsockClosed() {
  checkInvocations(udpwrap, { init: 1 }, 'when socket is closed');
  tick(2);
}

process.on('exit', onexit);

function onexit() {
  hooks.disable();
  hooks.sanityCheck('UDPWRAP');
  checkInvocations(udpwrap, { init: 1, destroy: 1 },
                   'when process exits');
}
