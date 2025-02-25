// Flags: --expose-internals

import { mustCall } from '../common/index.mjs';
import { strictEqual, notStrictEqual } from 'assert';
import internalAsyncHooks from 'internal/async_hooks';
import initHooks from './init-hooks.mjs';

const { newAsyncId, emitInit } = internalAsyncHooks;
const expectedId = newAsyncId();
const expectedTriggerId = newAsyncId();
const expectedType = 'test_emit_init_type';
const expectedResource = { key: 'test_emit_init_resource' };

const hooks1 = initHooks({
  oninit: mustCall((id, type, triggerAsyncId, resource) => {
    strictEqual(id, expectedId);
    strictEqual(type, expectedType);
    strictEqual(triggerAsyncId, expectedTriggerId);
    strictEqual(resource.key, expectedResource.key);
  })
});

hooks1.enable();

emitInit(expectedId, expectedType, expectedTriggerId,
                     expectedResource);

hooks1.disable();

initHooks({
  oninit: mustCall((id, type, triggerAsyncId, resource) => {
    strictEqual(id, expectedId);
    strictEqual(type, expectedType);
    notStrictEqual(triggerAsyncId, expectedTriggerId);
    strictEqual(resource.key, expectedResource.key);
  })
}).enable();

emitInit(expectedId, expectedType, null, expectedResource);
