'use strict';

const {
  FunctionPrototypeBind,
  SafeMap,
} = primordials;

const {
  ErrnoException,
} = require('internal/errors');

const { signals } = internalBinding('constants').os;

let Signal;
const signalWraps = new SafeMap();

function isSignal(event) {
  return typeof event === 'string' && signals[event] !== undefined;
}

// Detect presence of a listener for the special signal types
function startListeningIfSignal(type) {
  if (isSignal(type) && !signalWraps.has(type)) {
    if (Signal === undefined)
      Signal = internalBinding('signal_wrap').Signal;
    const wrap = new Signal();

    wrap.unref();

    wrap.onsignal = FunctionPrototypeBind(process.emit, process, type, type);

    const signum = signals[type];
    const err = wrap.start(signum);
    if (err) {
      wrap.close();
      throw new ErrnoException(err, 'uv_signal_start');
    }

    signalWraps.set(type, wrap);
  }
}

function stopListeningIfSignal(type) {
  const wrap = signalWraps.get(type);
  if (wrap !== undefined && process.listenerCount(type) === 0) {
    wrap.close();
    signalWraps.delete(type);
  }
}

module.exports = {
  startListeningIfSignal,
  stopListeningIfSignal,
};
