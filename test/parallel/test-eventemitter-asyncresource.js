'use strict';

const common = require('../common');
const { EventEmitterAsyncResource } = require('events');
const {
  createHook,
  executionAsyncId,
} = require('async_hooks');

const {
  deepStrictEqual,
  strictEqual,
  throws,
} = require('assert');

const {
  setImmediate: tick,
} = require('timers/promises');

function makeHook(trackedTypes) {
  const eventMap = new Map();

  function log(asyncId, name) {
    const entry = eventMap.get(asyncId);
    if (entry !== undefined) entry.push({ name });
  }

  const hook = createHook({
    init(asyncId, type, triggerAsyncId, resource) {
      if (trackedTypes.includes(type)) {
        eventMap.set(asyncId, [
          {
            name: 'init',
            type,
            triggerAsyncId,
            resource,
          },
        ]);
      }
    },

    before(asyncId) { log(asyncId, 'before'); },
    after(asyncId) { log(asyncId, 'after'); },
    destroy(asyncId) { log(asyncId, 'destroy'); },
  }).enable();

  return {
    done() {
      hook.disable();
      return new Set(eventMap.values());
    },
    ids() {
      return new Set(eventMap.keys());
    },
  };
}

// Tracks emit() calls correctly using async_hooks
(async () => {
  const tracer = makeHook(['Foo']);

  class Foo extends EventEmitterAsyncResource {}

  const origExecutionAsyncId = executionAsyncId();
  const foo = new Foo();

  foo.on('someEvent', common.mustCall());
  foo.emit('someEvent');

  deepStrictEqual([foo.asyncId], [...tracer.ids()]);
  strictEqual(foo.triggerAsyncId, origExecutionAsyncId);
  strictEqual(foo.asyncResource.eventEmitter, foo);

  foo.emitDestroy();

  await tick();

  deepStrictEqual(tracer.done(), new Set([
    [
      {
        name: 'init',
        type: 'Foo',
        triggerAsyncId: origExecutionAsyncId,
        resource: foo.asyncResource,
      },
      { name: 'before' },
      { name: 'after' },
      { name: 'destroy' },
    ],
  ]));
})().then(common.mustCall());

// Can explicitly specify name as positional arg
(async () => {
  const tracer = makeHook(['ResourceName']);

  const origExecutionAsyncId = executionAsyncId();
  class Foo extends EventEmitterAsyncResource {}

  const foo = new Foo('ResourceName');

  deepStrictEqual(tracer.done(), new Set([
    [
      {
        name: 'init',
        type: 'ResourceName',
        triggerAsyncId: origExecutionAsyncId,
        resource: foo.asyncResource,
      },
    ],
  ]));
})().then(common.mustCall());

// Can explicitly specify name as option
(async () => {
  const tracer = makeHook(['ResourceName']);

  const origExecutionAsyncId = executionAsyncId();
  class Foo extends EventEmitterAsyncResource {}

  const foo = new Foo({ name: 'ResourceName' });

  deepStrictEqual(tracer.done(), new Set([
    [
      {
        name: 'init',
        type: 'ResourceName',
        triggerAsyncId: origExecutionAsyncId,
        resource: foo.asyncResource,
      },
    ],
  ]));
})().then(common.mustCall());

throws(
  () => EventEmitterAsyncResource.prototype.emit(),
  { name: 'TypeError', message: /Cannot read private member/ }
);

throws(
  () => EventEmitterAsyncResource.prototype.emitDestroy(),
  { name: 'TypeError', message: /Cannot read private member/ }
);

['asyncId', 'triggerAsyncId', 'asyncResource'].forEach((getter) => {
  throws(
    () => Reflect.get(EventEmitterAsyncResource.prototype, getter, {}),
    {
      name: 'TypeError',
      message: /Cannot read private member/,
      stack: new RegExp(`at get ${getter}`),
    }
  );
});
