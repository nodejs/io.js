import { mustCall } from '../common/index.mjs';
import process from 'process';
import initHooks from './init-hooks.mjs';
import verifyGraph from './verify-graph.mjs';

const TIMEOUT = 1;

const hooks = initHooks();
hooks.enable();

let count = 0;
const iv1 = setInterval(mustCall(onfirstInterval, 3), TIMEOUT);
let iv2;

function onfirstInterval() {
  if (++count === 3) {
    clearInterval(iv1);
    iv2 = setInterval(mustCall(onsecondInterval, 1), TIMEOUT + 1);
  }
}

function onsecondInterval() {
  clearInterval(iv2);
}

process.on('exit', onexit);

function onexit() {
  hooks.disable();
  verifyGraph(
    hooks,
    [ { type: 'Timeout', id: 'timeout:1', triggerAsyncId: null },
      { type: 'Timeout', id: 'timeout:2', triggerAsyncId: 'timeout:1' }]
  );
}
