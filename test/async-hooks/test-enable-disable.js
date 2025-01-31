//  Test Steps Explained
//  ====================
//
//  Initializing hooks:
//
//  We initialize 3 hooks. For hook2 and hook3 we register a callback for the
//  "before" and in case of hook3 also for the "after" invocations.
//
//  Enabling hooks initially:
//
//  We only enable hook1 and hook3 initially.
//
//  Enabling hook2:
//
//  When hook3's "before" invocation occurs we enable hook2.  Since this
//  happens right before calling `onfirstImmediate` hook2 will miss all hook
//  invocations until then, including the "init" and "before" of the first
//  Immediate.
//  However afterwards it collects all invocations that follow on the first
//  Immediate as well as all invocations on the second Immediate.
//
//  This shows that a hook can enable another hook inside a life time event
//  callback.
//
//
//  Disabling hook1
//
//  Since we registered the "before" callback for hook2 it will execute it
//  right before `onsecondImmediate` is called.
//  At that point we disable hook1 which is why it will miss all invocations
//  afterwards and thus won't include the second "after" as well as the
//  "destroy" invocations
//
//  This shows that a hook can disable another hook inside a life time event
//  callback.
//
//  Disabling hook3
//
//  When the second "after" invocation occurs (after onsecondImmediate), hook3
//  disables itself.
//  As a result it will not receive the "destroy" invocation.
//
//  This shows that a hook can disable itself inside a life time event callback.
//
//  Sample Test Log
//  ===============
//
//  - setting up first Immediate
//  hook1.init.uid-5
//  hook3.init.uid-5
//  - finished setting first Immediate
//
//  hook1.before.uid-5
//  hook3.before.uid-5
//  - enabled hook2
//  - entering onfirstImmediate
//
//  - setting up second Immediate
//  hook1.init.uid-6
//  hook3.init.uid-6
//  hook2.init.uid-6
//  - finished setting second Immediate
//
//  - exiting onfirstImmediate
//  hook1.after.uid-5
//  hook3.after.uid-5
//  hook2.after.uid-5
//  hook1.destroy.uid-5
//  hook3.destroy.uid-5
//  hook2.destroy.uid-5
//  hook1.before.uid-6
//  hook3.before.uid-6
//  hook2.before.uid-6
//  - disabled hook1
//  - entering onsecondImmediate
//  - exiting onsecondImmediate
//  hook3.after.uid-6
//  - disabled hook3
//  hook2.after.uid-6
//  hook2.destroy.uid-6


'use strict';

const common = require('../common');
const assert = require('assert');
const tick = require('../common/tick');
const initHooks = require('./init-hooks');
const { checkInvocations } = require('./hook-checks');
const { isMainThread } = require('worker_threads');

if (!isMainThread)
  common.skip('Worker bootstrapping works differently -> different timing');

// Include "Unknown"s because hook2 will not be able to identify
// the type of the first Immediate  since it will miss its `init` invocation.
const types = [ 'Immediate', 'Unknown' ];

//
// Initializing hooks
//
const hook1 = initHooks();
const hook2 = initHooks({ onbefore: onhook2Before, allowNoInit: true });
const hook3 = initHooks({ onbefore: onhook3Before, onafter: onhook3After });

//
// Enabling hook1 and hook3 only, hook2 is still disabled
//
hook1.enable();
// Verify that the hook is enabled even if .enable() is called twice.
hook1.enable();
hook3.enable();

//
// Enabling hook2
//
let enabledHook2 = false;
function onhook3Before() {
  if (enabledHook2) return;
  hook2.enable();
  enabledHook2 = true;
}

//
// Disabling hook1
//
let disabledHook3 = false;
function onhook2Before() {
  if (disabledHook3) return;
  hook1.disable();
  // Verify that the hook is disabled even if .disable() is called twice.
  hook1.disable();
  disabledHook3 = true;
}

//
// Disabling hook3 during the second "after" invocations it sees
//
let count = 2;
function onhook3After() {
  if (!--count) {
    hook3.disable();
  }
}

setImmediate(common.mustCall(onfirstImmediate));

//
// onfirstImmediate is called after all "init" and "before" callbacks of the
// active hooks were invoked
//
function onfirstImmediate() {
  const as1 = hook1.activitiesOfTypes(types);
  const as2 = hook2.activitiesOfTypes(types);
  const as3 = hook3.activitiesOfTypes(types);
  assert.strictEqual(as1.length, 1);
  // hook2 was not enabled yet .. it is enabled after hook3's "before" completed
  assert.strictEqual(as2.length, 0);
  assert.strictEqual(as3.length, 1);

  // Check that hook1 and hook3 captured the same Immediate and that it is valid
  const firstImmediate = as1[0];
  assert.strictEqual(as3[0].uid, as1[0].uid);
  assert.strictEqual(firstImmediate.type, 'Immediate');
  assert.strictEqual(typeof firstImmediate.uid, 'number');
  assert.strictEqual(typeof firstImmediate.triggerAsyncId, 'number');
  checkInvocations(as1[0], { init: 1, before: 1 },
                   'hook1[0]: on first immediate');
  checkInvocations(as3[0], { init: 1, before: 1 },
                   'hook3[0]: on first immediate');

  // Setup the second Immediate, note that now hook2 is enabled and thus
  // will capture all lifetime events of this Immediate
  setImmediate(common.mustCall(onsecondImmediate));
}

//
// Once we exit onfirstImmediate the "after" callbacks of the active hooks are
// invoked
//

let hook1First, hook2First, hook3First;
let hook1Second, hook2Second, hook3Second;

//
// onsecondImmediate is called after all "before" callbacks of the active hooks
// are invoked again
//
function onsecondImmediate() {
  const as1 = hook1.activitiesOfTypes(types);
  const as2 = hook2.activitiesOfTypes(types);
  const as3 = hook3.activitiesOfTypes(types);
  assert.strictEqual(as1.length, 2);
  assert.strictEqual(as2.length, 2);
  assert.strictEqual(as3.length, 2);

  // Assign the info collected by each hook for each immediate for easier
  // reference.
  // hook2 saw the "init" of the second immediate before the
  // "after" of the first which is why they are ordered the opposite way
  hook1First = as1[0];
  hook1Second = as1[1];
  hook2First = as2[1];
  hook2Second = as2[0];
  hook3First = as3[0];
  hook3Second = as3[1];

  // Check that all hooks captured the same Immediate and that it is valid
  const secondImmediate = hook1Second;
  assert.strictEqual(hook2Second.uid, hook3Second.uid);
  assert.strictEqual(hook1Second.uid, hook3Second.uid);
  assert.strictEqual(secondImmediate.type, 'Immediate');
  assert.strictEqual(typeof secondImmediate.uid, 'number');
  assert.strictEqual(typeof secondImmediate.triggerAsyncId, 'number');

  checkInvocations(hook1First, { init: 1, before: 1, after: 1, destroy: 1 },
                   'hook1First: on second immediate');
  checkInvocations(hook1Second, { init: 1, before: 1 },
                   'hook1Second: on second immediate');
  // hook2 missed the "init" and "before" since it was enabled after they
  // occurred
  checkInvocations(hook2First, { after: 1, destroy: 1 },
                   'hook2First: on second immediate');
  checkInvocations(hook2Second, { init: 1, before: 1 },
                   'hook2Second: on second immediate');
  checkInvocations(hook3First, { init: 1, before: 1, after: 1, destroy: 1 },
                   'hook3First: on second immediate');
  checkInvocations(hook3Second, { init: 1, before: 1 },
                   'hook3Second: on second immediate');
  tick(1);
}

//
// Once we exit onsecondImmediate the "after" callbacks of the active hooks are
// invoked again.
// During this second "after" invocation hook3 disables itself
// (see onhook3After).
//

process.on('exit', onexit);

function onexit() {
  hook1.disable();
  hook2.disable();
  hook3.disable();
  hook1.sanityCheck();
  hook2.sanityCheck();
  hook3.sanityCheck();

  checkInvocations(hook1First, { init: 1, before: 1, after: 1, destroy: 1 },
                   'hook1First: when process exits');
  // hook1 was disabled during hook2's "before" of the second immediate
  // and thus did not see "after" and "destroy"
  checkInvocations(hook1Second, { init: 1, before: 1 },
                   'hook1Second: when process exits');
  // hook2 missed the "init" and "before" since it was enabled after they
  // occurred
  checkInvocations(hook2First, { after: 1, destroy: 1 },
                   'hook2First: when process exits');
  checkInvocations(hook2Second, { init: 1, before: 1, after: 1, destroy: 1 },
                   'hook2Second: when process exits');
  checkInvocations(hook3First, { init: 1, before: 1, after: 1, destroy: 1 },
                   'hook3First: when process exits');
  // We don't see a "destroy" invocation here since hook3 disabled itself
  // during its "after" invocation
  checkInvocations(hook3Second, { init: 1, before: 1, after: 1 },
                   'hook3Second: when process exits');
}
