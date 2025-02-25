// Flags: --no-force-async-hooks-checks --expose-internals
import { isMainThread, skip } from '../common/index.mjs';

if (!isMainThread)
  skip('Workers don\'t inherit per-env state like the check flag');

import internalAsyncHooks from 'internal/async_hooks';
const { emitInit, emitBefore, emitAfter, emitDestroy } = internalAsyncHooks;

// Negative asyncIds and invalid type name
emitInit(-1, null, -1, {});
emitBefore(-1, -1);
emitAfter(-1);
emitDestroy(-1);
