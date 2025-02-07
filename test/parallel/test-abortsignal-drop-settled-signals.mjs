// Flags: --expose_gc
//
import '../common/index.mjs';
import { gcUntil } from '../common/gc.js';
import { describe, it } from 'node:test';

function makeSubsequentCalls(limit, done, holdReferences = false) {
  let dependantSymbol;
  let signalRef;
  const ac = new AbortController();
  const retainedSignals = [];
  const handler = () => { };

  function run(iteration) {
    if (iteration > limit) {
      // This setImmediate is necessary to ensure that in the last iteration the remaining signal is GCed (if not
      // retained)
      setImmediate(() => {
        globalThis.gc();
        done(ac.signal, dependantSymbol);
      });
      return;
    }

    if (holdReferences) {
      retainedSignals.push(AbortSignal.any([ac.signal]));
    } else {
      // Using a WeakRef to avoid retaining information that will interfere with the test
      signalRef = new WeakRef(AbortSignal.any([ac.signal]));
      signalRef.deref().addEventListener('abort', handler);
    }

    dependantSymbol ??= Object.getOwnPropertySymbols(ac.signal).find(
      (s) => s.toString() === 'Symbol(kDependantSignals)'
    );

    setImmediate(() => {
      // Removing the event listener at some moment in the future
      // Which will then allow the signal to be GCed
      signalRef?.deref()?.removeEventListener('abort', handler);
      run(iteration + 1);
    });
  }

  run(1);
};

function runShortLivedSourceSignal(limit, done) {
  const signalRefs = new Set();

  function run(iteration) {
    if (iteration > limit) {
      globalThis.gc();
      done(signalRefs);
      return;
    }

    const ac = new AbortController();
    signalRefs.add(new WeakRef(ac.signal));
    AbortSignal.any([ac.signal]);

    setImmediate(() => run(iteration + 1));
  }

  run(1);
};

function runWithOrphanListeners(limit, done) {
  let composedSignalRef;
  const composedSignalRefs = [];
  const handler = () => { };

  function run(iteration) {
    const ac = new AbortController();
    if (iteration > limit) {
      setImmediate(() => {
        globalThis.gc();
        setImmediate(() => {
          globalThis.gc();

          done(composedSignalRefs);
        });
      });
      return;
    }

    composedSignalRef = new WeakRef(AbortSignal.any([ac.signal]));
    composedSignalRef.deref().addEventListener('abort', handler);

    const otherComposedSignalRef = new WeakRef(AbortSignal.any([composedSignalRef.deref()]));
    otherComposedSignalRef.deref().addEventListener('abort', handler);

    composedSignalRefs.push(composedSignalRef, otherComposedSignalRef);

    setImmediate(() => {
      run(iteration + 1);
    });
  }

  run(1);
}

const limit = 10_000;

describe('when there is a long-lived signal', () => {
  it('drops settled dependant signals', (t, done) => {
    makeSubsequentCalls(limit, (signal, depandantSignalsKey) => {
      setImmediate(() => {
        t.assert.strictEqual(signal[depandantSignalsKey].size, 0);
        done();
      });
    });
  });

  it('keeps all active dependant signals', (t, done) => {
    makeSubsequentCalls(limit, (signal, depandantSignalsKey) => {
      t.assert.strictEqual(signal[depandantSignalsKey].size, limit);

      done();
    }, true);
  });
});

it('does not prevent source signal from being GCed if it is short-lived', (t, done) => {
  runShortLivedSourceSignal(limit, (signalRefs) => {
    setImmediate(() => {
      const unGCedSignals = [...signalRefs].filter((ref) => ref.deref());

      t.assert.strictEqual(unGCedSignals.length, 0);
      done();
    });
  });
});

it('drops settled dependant signals when signal is composite', (t, done) => {
  const controllers = Array.from({ length: 2 }, () => new AbortController());

  // Using WeakRefs to avoid this test to retain information that will make the test fail
  const composedSignal1 = new WeakRef(AbortSignal.any([controllers[0].signal]));
  const composedSignalRef = new WeakRef(AbortSignal.any([composedSignal1.deref(), controllers[1].signal]));

  const kDependantSignals = Object.getOwnPropertySymbols(controllers[0].signal).find(
    (s) => s.toString() === 'Symbol(kDependantSignals)'
  );

  t.assert.strictEqual(controllers[0].signal[kDependantSignals].size, 2);
  t.assert.strictEqual(controllers[1].signal[kDependantSignals].size, 1);

  setImmediate(() => {
    globalThis.gc({ execution: 'async' }).then(async () => {
      await gcUntil('all signals are GCed', () => {
        const totalDependantSignals = Math.max(
          controllers[0].signal[kDependantSignals].size,
          controllers[1].signal[kDependantSignals].size
        );

        return composedSignalRef.deref() === undefined && totalDependantSignals === 0;
      });

      done();
    });
  });
});

it('drops settled signals even when there are listeners', (t, done) => {
  runWithOrphanListeners(limit, async (signalRefs) => {
    await gcUntil('all signals are GCed', () => {
      const unGCedSignals = [...signalRefs].filter((ref) => ref.deref());

      return unGCedSignals.length === 0;
    });

    done();
  });
});
