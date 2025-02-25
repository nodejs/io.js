import '../common/index.mjs';
import initHooks from './init-hooks.mjs';
import verifyGraph from './verify-graph.mjs';
import { watchFile, unwatchFile } from 'fs';
import { createRequire } from 'module';
import process from 'process';
import { fileURLToPath } from 'url';

const require = createRequire(import.meta.url);
const commonPath = require.resolve('../common/index.js');
const __filename = fileURLToPath(import.meta.url);

const hooks = initHooks();
hooks.enable();

function onchange() { }
// Install first file watcher
watchFile(__filename, onchange);

// Install second file watcher
watchFile(commonPath, onchange);

// Remove first file watcher
unwatchFile(__filename);

// Remove second file watcher
unwatchFile(commonPath);

process.on('exit', onexit);

function onexit() {
  hooks.disable();
  verifyGraph(
    hooks,
    [ { type: 'STATWATCHER', id: 'statwatcher:1', triggerAsyncId: null },
      { type: 'STATWATCHER', id: 'statwatcher:2', triggerAsyncId: null } ]
  );
}
