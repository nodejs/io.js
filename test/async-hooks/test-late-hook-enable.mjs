import { mustNotCall, mustCall } from '../common/index.mjs';
import { strictEqual } from 'assert';
import { createHook } from 'async_hooks';
import process from 'process';

// Checks that enabling async hooks in a callback actually
// triggers after & destroy as expected.

const fnsToTest = [setTimeout, (cb) => {
  setImmediate(() => {
    cb();

    // We need to keep the event loop open for this to actually work
    // since destroy hooks are triggered in unrefed Immediates
    setImmediate(() => {
      hook.disable();
    });
  });
}, (cb) => {
  setImmediate(() => {
    process.nextTick(() => {
      cb();

      // We need to keep the event loop open for this to actually work
      // since destroy hooks are triggered in unrefed Immediates
      setImmediate(() => {
        hook.disable();
        strictEqual(fnsToTest.length, 0);
      });
    });
  });
}];

const hook = createHook({
  before: mustNotCall(),
  after: mustCall(() => {}, 3),
  destroy: mustCall(() => {
    hook.disable();
    nextTest();
  }, 3)
});

nextTest();

function nextTest() {
  if (fnsToTest.length > 0) {
    fnsToTest.shift()(mustCall(() => {
      hook.enable();
    }));
  }
}
