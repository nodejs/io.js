import { mustCall } from '../common/index.mjs';
import process from 'process';
import initHooks from './init-hooks.mjs';

const hooks = initHooks();
hooks.enable();

setImmediate(() => {
  throw new Error();
});

setTimeout(() => {
  throw new Error();
}, 1);

process.on('uncaughtException', mustCall(2));
