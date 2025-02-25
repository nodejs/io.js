import { mustCall } from '../common/index.mjs';
import initHooks from './init-hooks.mjs';
import verifyGraph from './verify-graph.mjs';
import { readFile } from 'fs';
import process from 'process';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);

const hooks = initHooks();

hooks.enable();
readFile(__filename, mustCall(onread));

function onread() {}

process.on('exit', onexit);

function onexit() {
  hooks.disable();
  verifyGraph(
    hooks,
    [ { type: 'FSREQCALLBACK', id: 'fsreq:1', triggerAsyncId: null },
      { type: 'FSREQCALLBACK', id: 'fsreq:2', triggerAsyncId: 'fsreq:1' },
      { type: 'FSREQCALLBACK', id: 'fsreq:3', triggerAsyncId: 'fsreq:2' },
      { type: 'FSREQCALLBACK', id: 'fsreq:4', triggerAsyncId: 'fsreq:3' } ]
  );
}
