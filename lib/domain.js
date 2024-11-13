// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

'use strict';

// WARNING: THIS MODULE IS PENDING DEPRECATION.
//
// No new pull requests targeting this module will be accepted
// unless they address existing, critical bugs.

const {
  ArrayPrototypeEvery,
  ArrayPrototypeIndexOf,
  ArrayPrototypeLastIndexOf,
  ArrayPrototypePush,
  ArrayPrototypeSlice,
  ArrayPrototypeSplice,
  Error,
  FunctionPrototypeCall,
  ObjectDefineProperty,
  Promise,
  ReflectApply,
  SafeMap,
  SafeWeakMap,
  StringPrototypeRepeat,
  Symbol,
} = primordials;

const EventEmitter = require('events');
const {
  ERR_DOMAIN_CALLBACK_NOT_AVAILABLE,
  ERR_DOMAIN_CANNOT_SET_UNCAUGHT_EXCEPTION_CAPTURE,
  ERR_UNHANDLED_ERROR,
} = require('internal/errors').codes;
const { createHook } = require('async_hooks');
const { useDomainTrampoline } = require('internal/async_hooks');

const kWeak = Symbol('kWeak');
const { WeakReference } = require('internal/util');

// Overwrite process.domain with a getter/setter that will allow for more
// effective optimizations
const _domain = [null];
ObjectDefineProperty(process, 'domain', {
  __proto__: null,
  enumerable: true,
  get: function() {
    return _domain[0];
  },
  set: function(arg) {
    return _domain[0] = arg;
  },
});

const vmPromises = new SafeWeakMap();
const pairing = new SafeMap();
const asyncHook = createHook({
  init(asyncId, type, triggerAsyncId, resource) {
    if (process.domain !== null && process.domain !== undefined) {
      // If this operation is created while in a domain, let's mark it
      pairing.set(asyncId, process.domain[kWeak]);
      // Promises from other contexts, such as with the VM module, should not
      // have a domain property as it can be used to escape the sandbox.
      if (type !== 'PROMISE' || resource instanceof Promise) {
        ObjectDefineProperty(resource, 'domain', {
          __proto__: null,
          configurable: true,
          enumerable: false,
          value: process.domain,
          writable: true,
        });
      // Because promises from other contexts don't get a domain field,
      // the domain needs to be held alive another way. Stuffing it in a
      // weakmap connected to the promise lifetime can fix that.
      } else {
        vmPromises.set(resource, process.domain);
      }
    }
  },
  before(asyncId) {
    const current = pairing.get(asyncId);
    if (current !== undefined) { // Enter domain for this cb
      // We will get the domain through current.get(), because the resource
      // object's .domain property makes sure it is not garbage collected.
      // However, we do need to make the reference to the domain non-weak,
      // so that it cannot be garbage collected before the after() hook.
      current.incRef();
      current.get().enter();
    }
  },
  after(asyncId) {
    const current = pairing.get(asyncId);
    if (current !== undefined) { // Exit domain for this cb
      const domain = current.get();
      current.decRef();
      domain.exit();
    }
  },
  destroy(asyncId) {
    pairing.delete(asyncId); // cleaning up
  },
});

// When domains are in use, they claim full ownership of the
// uncaught exception capture callback.
if (process.hasUncaughtExceptionCaptureCallback()) {
  throw new ERR_DOMAIN_CALLBACK_NOT_AVAILABLE();
}

// Get the stack trace at the point where `domain` was required.
// eslint-disable-next-line no-restricted-syntax
const domainRequireStack = new Error('require(`domain`) at this point').stack;

const { setUncaughtExceptionCaptureCallback } = process;
process.setUncaughtExceptionCaptureCallback = function(fn) {
  const err = new ERR_DOMAIN_CANNOT_SET_UNCAUGHT_EXCEPTION_CAPTURE();
  err.stack += `\n${StringPrototypeRepeat('-', 40)}\n${domainRequireStack}`;
  throw err;
};


let sendMakeCallbackDeprecation = false;
function emitMakeCallbackDeprecation({ target, method }) {
  if (!sendMakeCallbackDeprecation) {
    process.emitWarning(
      'Using a domain property in MakeCallback is deprecated. Use the ' +
      'async_context variant of MakeCallback or the AsyncResource class ' +
      'instead. ' +
      `(Triggered by calling ${method?.name || '<anonymous>'} ` +
      `on ${target?.constructor?.name}.)`,
      'DeprecationWarning', 'DEP0097');
    sendMakeCallbackDeprecation = true;
  }
}

function topLevelDomainCallback(cb, ...args) {
  const domain = this.domain;
  if (exports.active && domain)
    emitMakeCallbackDeprecation({ target: this, method: cb });

  if (domain)
    domain.enter();
  const ret = ReflectApply(cb, this, args);
  if (domain)
    domain.exit();

  return ret;
}

// It's possible to enter one domain while already inside
// another one. The stack is each entered domain.
let stack = [];
exports._stack = stack;
useDomainTrampoline(topLevelDomainCallback);

function updateExceptionCapture() {
  if (ArrayPrototypeEvery(stack,
                          (domain) => domain.listenerCount('error') === 0)) {
    setUncaughtExceptionCaptureCallback(null);
  } else {
    setUncaughtExceptionCaptureCallback(null);
    setUncaughtExceptionCaptureCallback((er) => {
      return process.domain._errorHandler(er);
    });
  }
}


process.on('newListener', (name, listener) => {
  if (name === 'uncaughtException' &&
      listener !== domainUncaughtExceptionClear) {
    // Make sure the first listener for `uncaughtException` always clears
    // the domain stack.
    process.removeListener(name, domainUncaughtExceptionClear);
    process.prependListener(name, domainUncaughtExceptionClear);
  }
});

process.on('removeListener', (name, listener) => {
  if (name === 'uncaughtException' &&
      listener !== domainUncaughtExceptionClear) {
    // If the domain listener would be the only remaining one, remove it.
    const listeners = process.listeners('uncaughtException');
    if (listeners.length === 1 && listeners[0] === domainUncaughtExceptionClear)
      process.removeListener(name, domainUncaughtExceptionClear);
  }
});

function domainUncaughtExceptionClear() {
  stack.length = 0;
  exports.active = process.domain = null;
  updateExceptionCapture();
}


class Domain extends EventEmitter {
  constructor() {
    super();

    this.members = [];
    this[kWeak] = new WeakReference(this);
    asyncHook.enable();

    this.on('removeListener', updateExceptionCapture);
    this.on('newListener', updateExceptionCapture);
  }
}

exports.Domain = Domain;

exports.create = exports.createDomain = function createDomain() {
  return new Domain();
};

// The active domain is always the one that we're currently in.
exports.active = null;
Domain.prototype.members = undefined;

// Called by process._fatalException in case an error was thrown.
Domain.prototype._errorHandler = function(er) {
  let caught = false;

  if ((typeof er === 'object' && er !== null) || typeof er === 'function') {
    ObjectDefineProperty(er, 'domain', {
      __proto__: null,
      configurable: true,
      enumerable: false,
      value: this,
      writable: true,
    });
    er.domainThrown = true;
  }
  // Pop all adjacent duplicates of the currently active domain from the stack.
  // This is done to prevent a domain's error handler to run within the context
  // of itself, and re-entering itself recursively handler as a result of an
  // exception thrown in its context.
  while (exports.active === this) {
    this.exit();
  }

  // The top-level domain-handler is handled separately.
  //
  // The reason is that if V8 was passed a command line option
  // asking it to abort on an uncaught exception (currently
  // "--abort-on-uncaught-exception"), we want an uncaught exception
  // in the top-level domain error handler to make the
  // process abort. Using try/catch here would always make V8 think
  // that these exceptions are caught, and thus would prevent it from
  // aborting in these cases.
  if (stack.length === 0) {
    // If there's no error handler, do not emit an 'error' event
    // as this would throw an error, make the process exit, and thus
    // prevent the process 'uncaughtException' event from being emitted
    // if a listener is set.
    if (EventEmitter.listenerCount(this, 'error') > 0) {
      // Clear the uncaughtExceptionCaptureCallback so that we know that, since
      // the top-level domain is not active anymore, it would be ok to abort on
      // an uncaught exception at this point
      setUncaughtExceptionCaptureCallback(null);
      try {
        caught = this.emit('error', er);
      } finally {
        updateExceptionCapture();
      }
    }
  } else {
    // Wrap this in a try/catch so we don't get infinite throwing
    try {
      // One of three things will happen here.
      //
      // 1. There is a handler, caught = true
      // 2. There is no handler, caught = false
      // 3. It throws, caught = false
      //
      // If caught is false after this, then there's no need to exit()
      // the domain, because we're going to crash the process anyway.
      caught = this.emit('error', er);
    } catch (er2) {
      // The domain error handler threw!  oh no!
      // See if another domain can catch THIS error,
      // or else crash on the original one.
      updateExceptionCapture();
      if (stack.length) {
        exports.active = process.domain = stack[stack.length - 1];
        caught = process.domain._errorHandler(er2);
      } else {
        // Pass on to the next exception handler.
        throw er2;
      }
    }
  }

  // Exit all domains on the stack.  Uncaught exceptions end the
  // current tick and no domains should be left on the stack
  // between ticks.
  domainUncaughtExceptionClear();

  return caught;
};


Domain.prototype.enter = function() {
  // Note that this might be a no-op, but we still need
  // to push it onto the stack so that we can pop it later.
  exports.active = process.domain = this;
  ArrayPrototypePush(stack, this);
  updateExceptionCapture();
};


Domain.prototype.exit = function() {
  // Don't do anything if this domain is not on the stack.
  const index = ArrayPrototypeLastIndexOf(stack, this);
  if (index === -1) return;

  // Exit all domains until this one.
  ArrayPrototypeSplice(stack, index);

  exports.active = stack.length === 0 ? undefined : stack[stack.length - 1];
  process.domain = exports.active;
  updateExceptionCapture();
};


// note: this works for timers as well.
Domain.prototype.add = function(ee) {
  // If the domain is already added, then nothing left to do.
  if (ee.domain === this)
    return;

  // Has a domain already - remove it first.
  if (ee.domain)
    ee.domain.remove(ee);

  // Check for circular Domain->Domain links.
  // They cause big issues.
  //
  // For example:
  // var d = domain.create();
  // var e = domain.create();
  // d.add(e);
  // e.add(d);
  // e.emit('error', er); // RangeError, stack overflow!
  if (this.domain && (ee instanceof Domain)) {
    for (let d = this.domain; d; d = d.domain) {
      if (ee === d) return;
    }
  }

  ObjectDefineProperty(ee, 'domain', {
    __proto__: null,
    configurable: true,
    enumerable: false,
    value: this,
    writable: true,
  });
  ArrayPrototypePush(this.members, ee);
};


Domain.prototype.remove = function(ee) {
  ee.domain = null;
  const index = ArrayPrototypeIndexOf(this.members, ee);
  if (index !== -1)
    ArrayPrototypeSplice(this.members, index, 1);
};


Domain.prototype.run = function(fn) {
  this.enter();
  const ret = ReflectApply(fn, this, ArrayPrototypeSlice(arguments, 1));
  this.exit();

  return ret;
};


function intercepted(_this, self, cb, fnargs) {
  if (fnargs[0] && fnargs[0] instanceof Error) {
    const er = fnargs[0];
    er.domainBound = cb;
    er.domainThrown = false;
    ObjectDefineProperty(er, 'domain', {
      __proto__: null,
      configurable: true,
      enumerable: false,
      value: self,
      writable: true,
    });
    self.emit('error', er);
    return;
  }

  self.enter();
  const ret = ReflectApply(cb, _this, ArrayPrototypeSlice(fnargs, 1));
  self.exit();

  return ret;
}


Domain.prototype.intercept = function(cb) {
  const self = this;

  function runIntercepted() {
    return intercepted(this, self, cb, arguments);
  }

  return runIntercepted;
};


function bound(_this, self, cb, fnargs) {
  self.enter();
  const ret = ReflectApply(cb, _this, fnargs);
  self.exit();

  return ret;
}


Domain.prototype.bind = function(cb) {
  const self = this;

  function runBound() {
    return bound(this, self, cb, arguments);
  }

  ObjectDefineProperty(runBound, 'domain', {
    __proto__: null,
    configurable: true,
    enumerable: false,
    value: this,
    writable: true,
  });

  return runBound;
};

// Override EventEmitter methods to make it domain-aware.
EventEmitter.usingDomains = true;

const eventInit = EventEmitter.init;
EventEmitter.init = function(opts) {
  ObjectDefineProperty(this, 'domain', {
    __proto__: null,
    configurable: true,
    enumerable: false,
    value: null,
    writable: true,
  });
  if (exports.active && !(this instanceof exports.Domain)) {
    this.domain = exports.active;
  }

  return FunctionPrototypeCall(eventInit, this, opts);
};

const eventEmit = EventEmitter.prototype.emit;
EventEmitter.prototype.emit = function emit(...args) {
  const domain = this.domain;

  const type = args[0];
  const shouldEmitError = type === 'error' &&
                          this.listenerCount(type) > 0;

  // Just call original `emit` if current EE instance has `error`
  // handler, there's no active domain or this is process
  if (shouldEmitError || domain === null || domain === undefined ||
      this === process) {
    return ReflectApply(eventEmit, this, args);
  }

  if (type === 'error') {
    const er = args.length > 1 && args[1] ?
      args[1] : new ERR_UNHANDLED_ERROR();

    if (typeof er === 'object') {
      er.domainEmitter = this;
      ObjectDefineProperty(er, 'domain', {
        __proto__: null,
        configurable: true,
        enumerable: false,
        value: domain,
        writable: true,
      });
      er.domainThrown = false;
    }

    // Remove the current domain (and its duplicates) from the domains stack and
    // set the active domain to its parent (if any) so that the domain's error
    // handler doesn't run in its own context. This prevents any event emitter
    // created or any exception thrown in that error handler from recursively
    // executing that error handler.
    const origDomainsStack = ArrayPrototypeSlice(stack);
    const origActiveDomain = process.domain;

    // Travel the domains stack from top to bottom to find the first domain
    // instance that is not a duplicate of the current active domain.
    let idx = stack.length - 1;
    while (idx > -1 && process.domain === stack[idx]) {
      --idx;
    }

    // Change the stack to not contain the current active domain, and only the
    // domains above it on the stack.
    if (idx < 0) {
      stack.length = 0;
    } else {
      ArrayPrototypeSplice(stack, idx + 1);
    }

    // Change the current active domain
    if (stack.length > 0) {
      exports.active = process.domain = stack[stack.length - 1];
    } else {
      exports.active = process.domain = null;
    }

    updateExceptionCapture();

    domain.emit('error', er);

    // Now that the domain's error handler has completed, restore the domains
    // stack and the active domain to their original values.
    exports._stack = stack = origDomainsStack;
    exports.active = process.domain = origActiveDomain;
    updateExceptionCapture();

    return false;
  }

  domain.enter();
  const ret = ReflectApply(eventEmit, this, args);
  domain.exit();

  return ret;
};
