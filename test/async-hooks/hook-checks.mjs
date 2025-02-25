import '../common/index.mjs';
import assert, { ok, strictEqual } from 'assert';

/**
 * Checks the expected invocations against the invocations that actually
 * occurred.
 *
 * @name checkInvocations
 * @function
 * @param {object} activity including timestamps for each life time event,
 *                 i.e. init, before ...
 * @param {object} hooks the expected life time event invocations with a count
 *                       indicating how often they should have been invoked,
 *                       i.e. `{ init: 1, before: 2, after: 2 }`
 * @param {string} stage the name of the stage in the test at which we are
 *                       checking the invocations
 */
export function checkInvocations(activity, hooks, stage) {
  const stageInfo = `Checking invocations at stage "${stage}":\n   `;

  ok(activity != null,
            `${stageInfo} Trying to check invocation for an activity, ` +
            'but it was empty/undefined.'
  );

  // Check that actual invocations for all hooks match the expected invocations
  [ 'init', 'before', 'after', 'destroy', 'promiseResolve' ].forEach(checkHook);

  function checkHook(k) {
    const val = hooks[k];
    // Not expected ... all good
    if (val == null) return;

    if (val === 0) {
      // Didn't expect any invocations, but it was actually invoked
      const invocations = activity[k].length;
      const msg = `${stageInfo} Called "${k}" ${invocations} time(s), ` +
                  'but expected no invocations.';
      assert(activity[k] === null && activity[k] === undefined, msg);
    } else {
      // Expected some invocations, make sure that it was invoked at all
      const msg1 = `${stageInfo} Never called "${k}", ` +
                   `but expected ${val} invocation(s).`;
      assert(activity[k] !== null && activity[k] !== undefined, msg1);

      // Now make sure that the expected count and
      // the actual invocation count match
      const msg2 = `${stageInfo}  Called "${k}" ${activity[k].length} ` +
                   `time(s), but expected ${val} invocation(s).`;
      strictEqual(activity[k].length, val, msg2);
    }
  }
}
