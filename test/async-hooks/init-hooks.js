'use strict';
// Flags: --expose-gc

require('../common');
const assert = require('assert');
const async_hooks = require('async_hooks');
const { isMainThread } = require('worker_threads');
const util = require('util');
const print = process._rawDebug;

if (typeof global.gc === 'function') {
  (function exity(cntr) {
    process.once('beforeExit', () => {
      global.gc();
      if (cntr < 4) setImmediate(() => exity(cntr + 1));
    });
  })(0);
}

function noop() {}

class ActivityCollector {
  constructor(start, {
    allowNoInit = false,
    oninit,
    onbefore,
    onafter,
    ondestroy,
    onpromiseResolve,
    logid = null,
    logtype = null,
  } = {}) {
    this._start = start;
    this._allowNoInit = allowNoInit;
    this._activities = new Map();
    this._logid = logid;
    this._logtype = logtype;

    // Register event handlers if provided
    this.oninit = typeof oninit === 'function' ? oninit : noop;
    this.onbefore = typeof onbefore === 'function' ? onbefore : noop;
    this.onafter = typeof onafter === 'function' ? onafter : noop;
    this.ondestroy = typeof ondestroy === 'function' ? ondestroy : noop;
    this.onpromiseResolve = typeof onpromiseResolve === 'function' ?
      onpromiseResolve : noop;

    // Create the hook with which we'll collect activity data
    this._asyncHook = async_hooks.createHook({
      init: this._init.bind(this),
      before: this._before.bind(this),
      after: this._after.bind(this),
      destroy: this._destroy.bind(this),
      promiseResolve: this._promiseResolve.bind(this),
    });
  }

  enable() {
    this._asyncHook.enable();
  }

  disable() {
    this._asyncHook.disable();
  }

  sanityCheck(types) {
    if (types != null && !Array.isArray(types)) types = [ types ];

    function activityString(a) {
      return util.inspect(a, false, 5, true);
    }

    const violations = [];
    let tempActivityString;

    function v(msg) { violations.push(msg); }
    for (const a of this._activities.values()) {
      tempActivityString = activityString(a);
      if (types != null && !types.includes(a.type)) continue;

      if (a.init && a.init.length > 1) {
        v(`Activity inited twice\n${tempActivityString}` +
          '\nExpected "init" to be called at most once');
      }
      if (a.destroy && a.destroy.length > 1) {
        v(`Activity destroyed twice\n${tempActivityString}` +
          '\nExpected "destroy" to be called at most once');
      }
      if (a.before && a.after) {
        if (a.before.length < a.after.length) {
          v('Activity called "after" without calling "before"\n' +
            `${tempActivityString}` +
            '\nExpected no "after" call without a "before"');
        }
        if (a.before.some((x, idx) => x > a.after[idx])) {
          v('Activity had an instance where "after" ' +
            'was invoked before "before"\n' +
            `${tempActivityString}` +
            '\nExpected "after" to be called after "before"');
        }
      }
      if (a.before && a.destroy) {
        if (a.before.some((x, idx) => x > a.destroy[idx])) {
          v('Activity had an instance where "destroy" ' +
            'was invoked before "before"\n' +
            `${tempActivityString}` +
            '\nExpected "destroy" to be called after "before"');
        }
      }
      if (a.after && a.destroy) {
        if (a.after.some((x, idx) => x > a.destroy[idx])) {
          v('Activity had an instance where "destroy" ' +
            'was invoked before "after"\n' +
            `${tempActivityString}` +
            '\nExpected "destroy" to be called after "after"');
        }
      }
      if (!a.handleIsObject) {
        v(`No resource object\n${tempActivityString}` +
          '\nExpected "init" to be called with a resource object');
      }
    }
    if (violations.length) {
      console.error(violations.join('\n\n') + '\n');
      assert.fail(`${violations.length} failed sanity checks`);
    }
  }

  inspect(opts = {}) {
    if (typeof opts === 'string') opts = { types: opts };
    const { types = null, depth = 5, stage = null } = opts;
    const activities = types == null ?
      Array.from(this._activities.values()) :
      this.activitiesOfTypes(types);

    if (stage != null) console.log(`\n${stage}`);
    console.log(util.inspect(activities, false, depth, true));
  }

  activitiesOfTypes(types) {
    if (!Array.isArray(types)) types = [ types ];
    return this.activities.filter((x) => types.includes(x.type));
  }

  get activities() {
    return Array.from(this._activities.values());
  }

  _stamp(h, hook) {
    if (h == null) return;
    h[hook] ??= [];
    const time = process.hrtime(this._start);
    h[hook].push((time[0] * 1e9) + time[1]);
  }

  _getActivity(uid, hook) {
    const h = this._activities.get(uid);
    if (!h) {
      // If we allowed handles without init we ignore any further life time
      // events this makes sense for a few tests in which we enable some hooks
      // later
      if (this._allowNoInit) {
        const stub = { uid, type: 'Unknown', handleIsObject: true, handle: {} };
        this._activities.set(uid, stub);
        return stub;
      } else if (!isMainThread) {
        // Worker threads start main script execution inside of an AsyncWrap
        // callback, so we don't yield errors for these.
        return null;
      }
      const err = new Error(`Found a handle whose ${hook}` +
                            ' hook was invoked but not its init hook');
      throw err;
    }
    return h;
  }

  _init(uid, type, triggerAsyncId, handle) {
    const activity = {
      uid,
      type,
      triggerAsyncId,
      // In some cases (e.g. Timeout) the handle is a function, thus the usual
      // `typeof handle === 'object' && handle !== null` check can't be used.
      handleIsObject: handle instanceof Object,
      handle,
    };
    this._stamp(activity, 'init');
    this._activities.set(uid, activity);
    this._maybeLog(uid, type, 'init');
    this.oninit(uid, type, triggerAsyncId, handle);
  }

  _before(uid) {
    const h = this._getActivity(uid, 'before');
    this._stamp(h, 'before');
    this._maybeLog(uid, h?.type, 'before');
    this.onbefore(uid);
  }

  _after(uid) {
    const h = this._getActivity(uid, 'after');
    this._stamp(h, 'after');
    this._maybeLog(uid, h?.type, 'after');
    this.onafter(uid);
  }

  _destroy(uid) {
    const h = this._getActivity(uid, 'destroy');
    this._stamp(h, 'destroy');
    this._maybeLog(uid, h?.type, 'destroy');
    this.ondestroy(uid);
  }

  _promiseResolve(uid) {
    const h = this._getActivity(uid, 'promiseResolve');
    this._stamp(h, 'promiseResolve');
    this._maybeLog(uid, h?.type, 'promiseResolve');
    this.onpromiseResolve(uid);
  }

  _maybeLog(uid, type, name) {
    if (this._logid &&
      (type == null || this._logtype == null || this._logtype === type)) {
      print(`${this._logid}.${name}.uid-${uid}`);
    }
  }
}

exports = module.exports = function initHooks({
  oninit,
  onbefore,
  onafter,
  ondestroy,
  onpromiseResolve,
  allowNoInit,
  logid,
  logtype,
} = {}) {
  return new ActivityCollector(process.hrtime(), {
    oninit,
    onbefore,
    onafter,
    ondestroy,
    onpromiseResolve,
    allowNoInit,
    logid,
    logtype,
  });
};
