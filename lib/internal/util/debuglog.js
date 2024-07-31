'use strict';

const {
  MathFloor,
  Number,
  NumberPrototypeToFixed,
  ObjectDefineProperty,
  RegExp,
  RegExpPrototypeExec,
  SafeArrayIterator,
  SafeMap,
  StringPrototypePadStart,
  StringPrototypeSplit,
  StringPrototypeToLowerCase,
  StringPrototypeToUpperCase,
} = primordials;
const {
  CHAR_LOWERCASE_B: kTraceBegin,
  CHAR_LOWERCASE_E: kTraceEnd,
  CHAR_LOWERCASE_N: kTraceInstant,
} = require('internal/constants');
const { inspect, format, formatWithOptions } = require('internal/util/inspect');
const { getCategoryEnabledBuffer, trace } = internalBinding('trace_events');

// `debugImpls` and `testEnabled` are deliberately not initialized so any call
// to `debuglog()` before `initializeDebugEnv()` is called will throw.
let debugImpls;
let testEnabled;


// `debugEnv` is initial value of process.env.NODE_DEBUG
function initializeDebugEnv(debugEnv) {
  debugImpls = { __proto__: null };
  if (debugEnv) {
    // This is run before any user code, it's OK not to use primordials.
    debugEnv = debugEnv.replace(/[|\\{}()[\]^$+?.]/g, '\\$&')
      .replaceAll('*', '.*')
      .replaceAll(',', '$|^');
    const debugEnvRegex = new RegExp(`^${debugEnv}$`, 'i');
    testEnabled = (str) => RegExpPrototypeExec(debugEnvRegex, str) !== null;
  } else {
    testEnabled = () => false;
  }
}

// Emits warning when user sets
// NODE_DEBUG=http or NODE_DEBUG=http2.
function emitWarningIfNeeded(set) {
  if ('HTTP' === set || 'HTTP2' === set) {
    process.emitWarning('Setting the NODE_DEBUG environment variable ' +
      'to \'' + StringPrototypeToLowerCase(set) + '\' can expose sensitive ' +
      'data (such as passwords, tokens and authentication headers) ' +
      'in the resulting log.');
  }
}

const noop = () => {};

let utilColors;
function lazyUtilColors() {
  utilColors ??= require('internal/util/colors');
  return utilColors;
}

function debuglogImpl(enabled, set, args) {
  if (debugImpls[set] === undefined) {
    if (enabled) {
      const pid = process.pid;
      emitWarningIfNeeded(set);
      debugImpls[set] = function debug(...args) {
        const colors = lazyUtilColors().shouldColorize(process.stderr);
        const msg = formatWithOptions({ colors }, ...args);
        const coloredPID = inspect(pid, { colors });
        process.stderr.write(format('%s %s: %s\n', set, coloredPID, msg));
      };
    } else {
      debugImpls[set] = noop;
    }
  }
  return debugImpls[set];
}

// debuglogImpl depends on process.pid and process.env.NODE_DEBUG,
// so it needs to be called lazily in top scopes of internal modules
// that may be loaded before these run time states are allowed to
// be accessed.
function debuglog(set, cb) {
  function init() {
    set = StringPrototypeToUpperCase(set);
    enabled = testEnabled(set);
  }
  let debug = (...args) => {
    init();
    // Only invokes debuglogImpl() when the debug function is
    // called for the first time.
    debug = debuglogImpl(enabled, set);
    if (typeof cb === 'function')
      cb(debug);
    switch (args.length) {
      case 1: return debug(args[0]);
      case 2: return debug(args[0], args[1]);
      default: return debug(...new SafeArrayIterator(args));
    }
  };
  let enabled;
  let test = () => {
    init();
    test = () => enabled;
    return enabled;
  };
  const logger = (...args) => {
    switch (args.length) {
      case 1: return debug(args[0]);
      case 2: return debug(args[0], args[1]);
      default: return debug(...new SafeArrayIterator(args));
    }
  };
  ObjectDefineProperty(logger, 'enabled', {
    __proto__: null,
    get() {
      return test();
    },
    configurable: true,
    enumerable: true,
  });
  return logger;
}

function pad(value) {
  return StringPrototypePadStart(`${value}`, 2, '0');
}

const kNone = 1 << 0;
const kSkipLog = 1 << 1;
const kSkipTrace = 1 << 2;

const kSecond = 1000;
const kMinute = 60 * kSecond;
const kHour = 60 * kMinute;

function formatTime(ms) {
  let hours = 0;
  let minutes = 0;
  let seconds = 0;

  if (ms >= kSecond) {
    if (ms >= kMinute) {
      if (ms >= kHour) {
        hours = MathFloor(ms / kHour);
        ms = ms % kHour;
      }
      minutes = MathFloor(ms / kMinute);
      ms = ms % kMinute;
    }
    seconds = ms / kSecond;
  }

  if (hours !== 0 || minutes !== 0) {
    ({ 0: seconds, 1: ms } = StringPrototypeSplit(
      NumberPrototypeToFixed(seconds, 3),
      '.',
    ));
    const res = hours !== 0 ? `${hours}:${pad(minutes)}` : minutes;
    return `${res}:${pad(seconds)}.${ms} (${hours !== 0 ? 'h:m' : ''}m:ss.mmm)`;
  }

  if (seconds !== 0) {
    return `${NumberPrototypeToFixed(seconds, 3)}s`;
  }

  return `${Number(NumberPrototypeToFixed(ms, 3))}ms`;
}

function safeTraceLabel(label) {
  return label.replace(/\\/g, '\\\\');
}

/**
 * @typedef {(label: string, timeFormatted: string, args?: any[]) => void} LogImpl
 */

/**
 * Returns true if label was found
 * @param {string} timesStore
 * @param {string} implementation
 * @param {LogImpl} logImp
 * @param {string} label
 * @param {any} args
 * @returns {void}
 */
function timeLogImpl(timesStore, implementation, logImp, label, args) {
  const time = timesStore.get(label);
  if (time === undefined) {
    process.emitWarning(`No such label '${label}' for ${implementation}`);
    return;
  }

  const duration = process.hrtime(time);
  const ms = duration[0] * 1000 + duration[1] / 1e6;

  const formatted = formatTime(ms);

  if (args === undefined) {
    logImp(label, formatted);
  } else {
    logImp(label, formatted, args);
  }
}

/**
 * @param {SafeMap} timesStore
 * @param {string} traceCategory
 * @param {string} implementation
 * @param {number} timerFlags
 * @param {string} logLabel
 * @param {string} traceLabel
 * @returns {void}
 */
function time(timesStore, traceCategory, implementation, timerFlags, logLabel = 'default', traceLabel = undefined) {
  // Coerces everything other than Symbol to a string
  logLabel = `${logLabel}`;

  if (traceLabel !== undefined) {
    traceLabel = `${traceLabel}`;
  } else {
    traceLabel = logLabel;
  }

  if (timesStore.has(logLabel)) {
    process.emitWarning(`Label '${logLabel}' already exists for ${implementation}`);
    return;
  }

  if ((timerFlags & kSkipTrace) === 0) {
    traceLabel = safeTraceLabel(traceLabel);
    trace(kTraceBegin, traceCategory, traceLabel, 0);
  }

  timesStore.set(logLabel, process.hrtime());
}

/**
 * @param {SafeMap} timesStore
 * @param {string} traceCategory
 * @param {string} implementation
 * @param {number} timerFlags
 * @param {LogImpl} logImpl
 * @param {string} logLabel
 * @param {string} traceLabel
 * @returns {void}
 */
function timeEnd(
  timesStore,
  traceCategory,
  implementation,
  timerFlags,
  logImpl,
  logLabel = 'default',
  traceLabel = undefined,
) {
  // Coerces everything other than Symbol to a string
  logLabel = `${logLabel}`;

  if (traceLabel !== undefined) {
    traceLabel = `${traceLabel}`;
  } else {
    traceLabel = logLabel;
  }

  if ((timerFlags & kSkipLog) === 0) {
    timeLogImpl(timesStore, implementation, logImpl, logLabel);
  }

  if ((timerFlags & kSkipTrace) === 0) {
    traceLabel = safeTraceLabel(traceLabel);
    trace(kTraceEnd, traceCategory, traceLabel, 0);
  }

  timesStore.delete(logLabel);
}

/**
 * @param {SafeMap} timesStore
 * @param {string} traceCategory
 * @param {string} implementation
 * @param {number} timerFlags
 * @param {LogImpl} logImpl
 * @param {string} logLabel
 * @param {string} traceLabel
 * @param {any[]} args
 * @returns {void}
 */
function timeLog(
  timesStore,
  traceCategory,
  implementation,
  timerFlags,
  logImpl,
  logLabel = 'default',
  traceLabel = undefined,
  args,
) {
  // Coerces everything other than Symbol to a string
  logLabel = `${logLabel}`;

  if (traceLabel !== undefined) {
    traceLabel = `${traceLabel}`;
  } else {
    traceLabel = logLabel;
  }

  if ((timerFlags & kSkipLog) === 0) {
    timeLogImpl(timesStore, implementation, logImpl, logLabel, args);
  }

  if ((timerFlags & kSkipTrace) === 0) {
    traceLabel = safeTraceLabel(traceLabel);
    trace(kTraceInstant, traceCategory, traceLabel, 0);
  }
}

/**
 * @type {Record<string, SafeMap>}
 */
let tracesStores;

/**
 * @typedef {(logLabel: string, traceLabel?: string) => void} TimerStart
 */

/**
 * @typedef {(logLabel: string, traceLabel?: string) => void} TimerEnd
 */

/**
 * @typedef {(logLabel: string, traceLabel?: string, args?: any[]) => void} TimerLog
 */

/**
 * Debuglog with time fns and support for trace
 * @param {string} set
 * @param {(startTimer: TimerStart, endTimer: TimerEnd, logTimer: TimerLog) => void} cb
 * @returns {{startTimer: TimerStart, endTimer: TimerEnd, logTimer: TimerLog}}
 */
function debugWithTimer(set, cb) {
  set = StringPrototypeToUpperCase(set);

  if (tracesStores === undefined) {
    tracesStores = { __proto__: null };
  }

  /**
   * @type {LogImpl}
   */
  function logImpl(label, timeFormatted, args) {
    const pid = process.pid;
    const colors = { colors: lazyUtilColors().shouldColorize(process.stderr) };
    const coloredPID = inspect(pid, colors);

    if (args === undefined)
      process.stderr.write(format('%s %s %s: %s\n', set, coloredPID, label, timeFormatted));
    else
      process.stderr.write(
        format(
          '%s %s %s: %s\n',
          set,
          coloredPID,
          label,
          timeFormatted,
          ...new SafeArrayIterator(args),
        ),
      );
  }

  const traceCategory = `node,node.${StringPrototypeToLowerCase(set)}`;
  let traceCategoryBuffer;
  let debugLogCategoryEnabled = false;
  let timerFlags = kNone;

  const skipAll = kSkipLog | kSkipTrace;

  function ensureTimerFlagsAreUpdated() {
    timerFlags &= ~kSkipTrace;

    if (traceCategoryBuffer[0] === 0) {
      timerFlags |= kSkipTrace;
    }
  }

  /**
   * @type {TimerStart}
   */
  function internalStartTimer(logLabel, traceLabel) {
    ensureTimerFlagsAreUpdated();

    if (timerFlags === skipAll) {
      return;
    }

    time(
      tracesStores[set],
      traceCategory,
      'debuglog.time',
      timerFlags,
      logLabel,
      traceLabel,
    );
  }

  /**
   * @type {TimerEnd}
   */
  function internalEndTimer(logLabel, traceLabel) {
    ensureTimerFlagsAreUpdated();

    if (timerFlags === skipAll) {
      return;
    }

    timeEnd(
      tracesStores[set],
      traceCategory,
      'debuglog.timeEnd',
      timerFlags,
      logImpl,
      logLabel,
      traceLabel,
    );
  }

  /**
   * @type {TimerLog}
   */
  function internalLogTimer(logLabel, traceLabel, args) {
    ensureTimerFlagsAreUpdated();

    if (timerFlags === skipAll) {
      return;
    }

    timeLog(
      tracesStores[set],
      traceCategory,
      'debuglog.timeLog',
      timerFlags,
      logImpl,
      logLabel,
      traceLabel,
      args,
    );
  }

  function init() {
    if (tracesStores[set] === undefined) {
      tracesStores[set] = new SafeMap();
    }
    emitWarningIfNeeded(set);
    debugLogCategoryEnabled = testEnabled(set);
    traceCategoryBuffer = getCategoryEnabledBuffer(traceCategory);

    timerFlags = kNone;

    if (!debugLogCategoryEnabled) {
      timerFlags |= kSkipLog;
    }

    if (traceCategoryBuffer[0] === 0) {
      timerFlags |= kSkipTrace;
    }

    cb(internalStartTimer, internalEndTimer, internalLogTimer);
  }

  /**
   * @type {TimerStart}
   */
  const startTimer = (logLabel, traceLabel) => {
    init();

    if (timerFlags !== skipAll)
      internalStartTimer(logLabel, traceLabel);
  };

  /**
   * @type {TimerEnd}
   */
  const endTimer = (logLabel, traceLabel) => {
    init();

    if (timerFlags !== skipAll)
      internalEndTimer(logLabel, traceLabel);
  };

  /**
   * @type {TimerLog}
   */
  const logTimer = (logLabel, traceLabel, args) => {
    init();

    if (timerFlags !== skipAll)
      internalLogTimer(logLabel, traceLabel, args);
  };

  return {
    startTimer,
    endTimer,
    logTimer,
  };
}

module.exports = {
  kNone,
  kSkipLog,
  kSkipTrace,
  formatTime,
  time,
  timeEnd,
  timeLog,
  debuglog,
  debugWithTimer,
  initializeDebugEnv,
};
