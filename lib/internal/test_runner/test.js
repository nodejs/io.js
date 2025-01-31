'use strict';
const {
  ArrayPrototypePush,
  ArrayPrototypePushApply,
  ArrayPrototypeShift,
  ArrayPrototypeSlice,
  ArrayPrototypeSome,
  ArrayPrototypeSplice,
  ArrayPrototypeUnshift,
  ArrayPrototypeUnshiftApply,
  Error,
  FunctionPrototype,
  MathMax,
  Number,
  NumberPrototypeToFixed,
  ObjectDefineProperty,
  ObjectSeal,
  Promise,
  PromisePrototypeThen,
  PromiseResolve,
  PromiseWithResolvers,
  ReflectApply,
  RegExpPrototypeExec,
  SafeMap,
  SafePromiseAll,
  SafePromiseAllReturnVoid,
  SafePromiseRace,
  SafeSet,
  StringPrototypeStartsWith,
  StringPrototypeTrim,
  Symbol,
  SymbolDispose,
} = primordials;
const { getCallerLocation } = internalBinding('util');
const { exitCodes: { kGenericUserError } } = internalBinding('errors');
const { addAbortListener } = require('internal/events/abort_listener');
const { queueMicrotask } = require('internal/process/task_queues');
const { AsyncResource } = require('async_hooks');
const { AbortController } = require('internal/abort_controller');
const {
  AbortError,
  codes: {
    ERR_INVALID_ARG_TYPE,
    ERR_TEST_FAILURE,
  },
} = require('internal/errors');
const { MockTracker } = require('internal/test_runner/mock/mock');
const { TestsStream } = require('internal/test_runner/tests_stream');
const {
  createDeferredCallback,
  countCompletedTest,
  isTestFailureError,
  reporterScope,
} = require('internal/test_runner/utils');
const {
  kEmptyObject,
  once: runOnce,
} = require('internal/util');
const { isPromise } = require('internal/util/types');
const {
  validateAbortSignal,
  validateFunction,
  validateNumber,
  validateObject,
  validateOneOf,
  validateUint32,
} = require('internal/validators');
const {
  clearTimeout,
  setTimeout,
} = require('timers');
const { TIMEOUT_MAX } = require('internal/timers');
const { fileURLToPath } = require('internal/url');
const { availableParallelism } = require('os');
const { innerOk } = require('internal/assert/utils');
const { bigint: hrtime } = process.hrtime;
const kCallbackAndPromisePresent = 'callbackAndPromisePresent';
const kCancelledByParent = 'cancelledByParent';
const kAborted = 'testAborted';
const kParentAlreadyFinished = 'parentAlreadyFinished';
const kSubtestsFailed = 'subtestsFailed';
const kTestCodeFailure = 'testCodeFailure';
const kTestTimeoutFailure = 'testTimeoutFailure';
const kHookFailure = 'hookFailed';
const kDefaultTimeout = null;
const noop = FunctionPrototype;
const kShouldAbort = Symbol('kShouldAbort');
const kHookNames = ObjectSeal(['before', 'after', 'beforeEach', 'afterEach']);
const kUnwrapErrors = new SafeSet()
  .add(kTestCodeFailure).add(kHookFailure)
  .add('uncaughtException').add('unhandledRejection');
let kResistStopPropagation;
let assertObj;
let findSourceMap;
let noopTestStream;

const kRunOnceOptions = { __proto__: null, preserveReturnValue: true };

function lazyFindSourceMap(file) {
  if (findSourceMap === undefined) {
    ({ findSourceMap } = require('internal/source_map/source_map_cache'));
  }

  return findSourceMap(file);
}

function lazyAssertObject(harness) {
  if (assertObj === undefined) {
    const { getAssertionMap } = require('internal/test_runner/assert');
    const { SnapshotManager } = require('internal/test_runner/snapshot');

    assertObj = getAssertionMap();
    harness.snapshotManager = new SnapshotManager(harness.config.updateSnapshots);

    if (!assertObj.has('snapshot')) {
      assertObj.set('snapshot', harness.snapshotManager.createAssert());
    }

    if (!assertObj.has('fileSnapshot')) {
      assertObj.set('fileSnapshot', harness.snapshotManager.createFileAssert());
    }
  }
  return assertObj;
}

function stopTest(timeout, signal) {
  const deferred = PromiseWithResolvers();
  const abortListener = addAbortListener(signal, deferred.resolve);
  let timer;
  let disposeFunction;

  if (timeout === kDefaultTimeout) {
    disposeFunction = abortListener[SymbolDispose];
  } else {
    timer = setTimeout(() => deferred.resolve(), timeout);
    timer.unref();

    ObjectDefineProperty(deferred, 'promise', {
      __proto__: null,
      configurable: true,
      writable: true,
      value: PromisePrototypeThen(deferred.promise, () => {
        throw new ERR_TEST_FAILURE(
          `test timed out after ${timeout}ms`,
          kTestTimeoutFailure,
        );
      }),
    });

    disposeFunction = () => {
      abortListener[SymbolDispose]();
      timer[SymbolDispose]();
    };
  }

  ObjectDefineProperty(deferred.promise, SymbolDispose, {
    __proto__: null,
    configurable: true,
    writable: true,
    value: disposeFunction,
  });
  return deferred.promise;
}

function testMatchesPattern(test, patterns) {
  const matchesByNameOrParent = ArrayPrototypeSome(patterns, (re) =>
    RegExpPrototypeExec(re, test.name) !== null,
  ) || (test.parent && testMatchesPattern(test.parent, patterns));
  if (matchesByNameOrParent) return true;

  const testNameWithAncestors = StringPrototypeTrim(test.getTestNameWithAncestors());

  return ArrayPrototypeSome(patterns, (re) =>
    RegExpPrototypeExec(re, testNameWithAncestors) !== null,
  );
}

class TestPlan {
  constructor(count) {
    validateUint32(count, 'count');
    this.expected = count;
    this.actual = 0;
  }

  check() {
    if (this.actual !== this.expected) {
      throw new ERR_TEST_FAILURE(
        `plan expected ${this.expected} assertions but received ${this.actual}`,
        kTestCodeFailure,
      );
    }
  }
}

class TestContext {
  #assert;
  #test;

  constructor(test) {
    this.#test = test;
  }

  get signal() {
    return this.#test.signal;
  }

  get name() {
    return this.#test.name;
  }

  get filePath() {
    return this.#test.entryFile;
  }

  get fullName() {
    return getFullName(this.#test);
  }

  get error() {
    return this.#test.error;
  }

  get passed() {
    return this.#test.passed;
  }

  diagnostic(message) {
    this.#test.diagnostic(message);
  }

  plan(count) {
    if (this.#test.plan !== null) {
      throw new ERR_TEST_FAILURE(
        'cannot set plan more than once',
        kTestCodeFailure,
      );
    }

    this.#test.plan = new TestPlan(count);
  }

  get assert() {
    if (this.#assert === undefined) {
      const { plan } = this.#test;
      const map = lazyAssertObject(this.#test.root.harness);
      const assert = { __proto__: null };

      this.#assert = assert;
      map.forEach((method, name) => {
        assert[name] = (...args) => {
          if (plan !== null) {
            plan.actual++;
          }
          return ReflectApply(method, this, args);
        };
      });

      if (!map.has('ok')) {
        // This is a hack. It allows the innerOk function to collect the
        // stacktrace from the correct starting point.
        function ok(...args) {
          if (plan !== null) {
            plan.actual++;
          }
          innerOk(ok, args.length, ...args);
        }

        assert.ok = ok;
      }
    }
    return this.#assert;
  }

  get mock() {
    this.#test.mock ??= new MockTracker();
    return this.#test.mock;
  }

  runOnly(value) {
    this.#test.runOnlySubtests = !!value;
  }

  skip(message) {
    this.#test.skip(message);
  }

  todo(message) {
    this.#test.todo(message);
  }

  test(name, options, fn) {
    const overrides = {
      __proto__: null,
      loc: getCallerLocation(),
    };

    const { plan } = this.#test;
    if (plan !== null) {
      plan.actual++;
    }

    const subtest = this.#test.createSubtest(
      // eslint-disable-next-line no-use-before-define
      Test, name, options, fn, overrides,
    );

    return subtest.start();
  }

  before(fn, options) {
    this.#test.createHook('before', fn, {
      __proto__: null,
      ...options,
      parent: this.#test,
      hookType: 'before',
      loc: getCallerLocation(),
    });
  }

  after(fn, options) {
    this.#test.createHook('after', fn, {
      __proto__: null,
      ...options,
      parent: this.#test,
      hookType: 'after',
      loc: getCallerLocation(),
    });
  }

  beforeEach(fn, options) {
    this.#test.createHook('beforeEach', fn, {
      __proto__: null,
      ...options,
      parent: this.#test,
      hookType: 'beforeEach',
      loc: getCallerLocation(),
    });
  }

  afterEach(fn, options) {
    this.#test.createHook('afterEach', fn, {
      __proto__: null,
      ...options,
      parent: this.#test,
      hookType: 'afterEach',
      loc: getCallerLocation(),
    });
  }

  waitFor(condition, options = kEmptyObject) {
    validateFunction(condition, 'condition');
    validateObject(options, 'options');

    const {
      interval = 50,
      timeout = 1000,
    } = options;

    validateNumber(interval, 'options.interval', 0, TIMEOUT_MAX);
    validateNumber(timeout, 'options.timeout', 0, TIMEOUT_MAX);

    const { promise, resolve, reject } = PromiseWithResolvers();
    const noError = Symbol();
    let cause = noError;
    let pollerId;
    let timeoutId;
    const done = (err, result) => {
      clearTimeout(pollerId);
      clearTimeout(timeoutId);

      if (err === noError) {
        resolve(result);
      } else {
        reject(err);
      }
    };

    timeoutId = setTimeout(() => {
      // eslint-disable-next-line no-restricted-syntax
      const err = new Error('waitFor() timed out');

      if (cause !== noError) {
        err.cause = cause;
      }

      done(err);
    }, timeout);

    const poller = async () => {
      try {
        const result = await condition();

        done(noError, result);
      } catch (err) {
        cause = err;
        pollerId = setTimeout(poller, interval);
      }
    };

    poller();
    return promise;
  }
}

class SuiteContext {
  #suite;

  constructor(suite) {
    this.#suite = suite;
  }

  get signal() {
    return this.#suite.signal;
  }

  get name() {
    return this.#suite.name;
  }

  get filePath() {
    return this.#suite.entryFile;
  }

  get fullName() {
    return getFullName(this.#suite);
  }
}

class Test extends AsyncResource {
  reportedType = 'test';
  abortController;
  outerSignal;
  #reportedSubtest;

  constructor(options) {
    super('Test');

    let { fn, name, parent } = options;
    const { concurrency, entryFile, loc, only, timeout, todo, skip, signal, plan } = options;

    if (typeof fn !== 'function') {
      fn = noop;
    }

    if (typeof name !== 'string' || name === '') {
      name = fn.name || '<anonymous>';
    }

    if (!(parent instanceof Test)) {
      parent = null;
    }

    this.name = name;
    this.parent = parent;
    this.testNumber = 0;
    this.outputSubtestCount = 0;
    this.diagnostics = [];
    this.filtered = false;
    this.filteredByName = false;
    this.hasOnlyTests = false;

    if (parent === null) {
      this.root = this;
      this.harness = options.harness;
      this.config = this.harness.config;
      this.concurrency = 1;
      this.nesting = 0;
      this.only = this.config.only;
      this.reporter = new TestsStream();
      this.runOnlySubtests = this.only;
      this.childNumber = 0;
      this.timeout = kDefaultTimeout;
      this.entryFile = entryFile;
    } else {
      const nesting = parent.parent === null ? parent.nesting :
        parent.nesting + 1;
      const { config, isFilteringByName, isFilteringByOnly } = parent.root.harness;

      this.root = parent.root;
      this.harness = null;
      this.config = config;
      this.concurrency = parent.concurrency;
      this.nesting = nesting;
      this.only = only;
      this.reporter = parent.reporter;
      this.runOnlySubtests = false;
      this.childNumber = parent.subtests.length + 1;
      this.timeout = parent.timeout;
      this.entryFile = parent.entryFile;

      if (isFilteringByName) {
        this.filteredByName = this.willBeFilteredByName();
        if (!this.filteredByName) {
          for (let t = this.parent; t !== null && t.filteredByName; t = t.parent) {
            t.filteredByName = false;
          }
        }
      }

      if (isFilteringByOnly) {
        if (this.only) {
          // If filtering impacts the tests within a suite, then the suite only
          // runs those tests. If filtering does not impact the tests within a
          // suite, then all tests are run.
          this.parent.runOnlySubtests = true;

          if (this.parent === this.root || this.parent.startTime === null) {
            for (let t = this.parent; t !== null && !t.hasOnlyTests; t = t.parent) {
              t.hasOnlyTests = true;
            }
          }
        } else if (this.only === false) {
          fn = noop;
        }
      } else if (only || this.parent.runOnlySubtests) {
        const warning =
          "'only' and 'runOnly' require the --test-only command-line option.";
        this.diagnostic(warning);
      }
    }

    switch (typeof concurrency) {
      case 'number':
        validateUint32(concurrency, 'options.concurrency', true);
        this.concurrency = concurrency;
        break;

      case 'boolean':
        if (concurrency) {
          this.concurrency = parent === null ?
            MathMax(availableParallelism() - 1, 1) : Infinity;
        } else {
          this.concurrency = 1;
        }
        break;

      default:
        if (concurrency != null)
          throw new ERR_INVALID_ARG_TYPE('options.concurrency', ['boolean', 'number'], concurrency);
    }

    if (timeout != null && timeout !== Infinity) {
      validateNumber(timeout, 'options.timeout', 0, TIMEOUT_MAX);
      this.timeout = timeout;
    }

    if (skip) {
      fn = noop;
    }

    this.abortController = new AbortController();
    this.outerSignal = signal;
    this.signal = this.abortController.signal;

    validateAbortSignal(signal, 'options.signal');
    if (signal) {
      kResistStopPropagation ??= require('internal/event_target').kResistStopPropagation;
    }

    this.outerSignal?.addEventListener(
      'abort',
      this.#abortHandler,
      { __proto__: null, [kResistStopPropagation]: true },
    );

    this.fn = fn;
    this.mock = null;
    this.plan = null;
    this.expectedAssertions = plan;
    this.cancelled = false;
    this.skipped = skip !== undefined && skip !== false;
    this.isTodo = todo !== undefined && todo !== false;
    this.startTime = null;
    this.endTime = null;
    this.passed = false;
    this.error = null;
    this.message = typeof skip === 'string' ? skip :
      typeof todo === 'string' ? todo : null;
    this.activeSubtests = 0;
    this.pendingSubtests = [];
    this.readySubtests = new SafeMap();
    this.subtests = [];
    this.waitingOn = 0;
    this.finished = false;
    this.hooks = {
      __proto__: null,
      before: [],
      after: [],
      beforeEach: [],
      afterEach: [],
      ownAfterEachCount: 0,
    };

    if (loc === undefined) {
      this.loc = undefined;
    } else {
      this.loc = {
        __proto__: null,
        line: loc[0],
        column: loc[1],
        file: loc[2],
      };

      if (this.config.sourceMaps === true) {
        const map = lazyFindSourceMap(this.loc.file);
        const entry = map?.findEntry(this.loc.line - 1, this.loc.column - 1);

        if (entry?.originalSource !== undefined) {
          this.loc.line = entry.originalLine + 1;
          this.loc.column = entry.originalColumn + 1;
          this.loc.file = entry.originalSource;
        }
      }

      if (StringPrototypeStartsWith(this.loc.file, 'file://')) {
        this.loc.file = fileURLToPath(this.loc.file);
      }
    }
  }

  applyFilters() {
    if (this.error) {
      // Never filter out errors.
      return;
    }

    if (this.filteredByName) {
      this.filtered = true;
      return;
    }

    if (this.root.harness.isFilteringByOnly && !this.only && !this.hasOnlyTests) {
      if (this.parent.runOnlySubtests ||
          this.parent.hasOnlyTests ||
          this.only === false) {
        this.filtered = true;
      }
    }
  }

  willBeFilteredByName() {
    const { testNamePatterns, testSkipPatterns } = this.config;

    if (testNamePatterns && !testMatchesPattern(this, testNamePatterns)) {
      return true;
    }
    if (testSkipPatterns && testMatchesPattern(this, testSkipPatterns)) {
      return true;
    }
    return false;
  }

  /**
   * Returns a name of the test prefixed by name of all its ancestors in ascending order, separated by a space
   * Ex."grandparent parent test"
   *
   * It's needed to match a single test with non-unique name by pattern
   */
  getTestNameWithAncestors() {
    if (!this.parent) return '';

    return `${this.parent.getTestNameWithAncestors()} ${this.name}`;
  }

  hasConcurrency() {
    return this.concurrency > this.activeSubtests;
  }

  addPendingSubtest(deferred) {
    ArrayPrototypePush(this.pendingSubtests, deferred);
  }

  async processPendingSubtests() {
    while (this.pendingSubtests.length > 0 && this.hasConcurrency()) {
      const deferred = ArrayPrototypeShift(this.pendingSubtests);
      const test = deferred.test;
      test.reporter.dequeue(test.nesting, test.loc, test.name, this.reportedType);
      await test.run();
      deferred.resolve();
    }
  }

  addReadySubtest(subtest) {
    this.readySubtests.set(subtest.childNumber, subtest);
  }

  processReadySubtestRange(canSend) {
    const start = this.waitingOn;
    const end = start + this.readySubtests.size;

    for (let i = start; i < end; i++) {
      const subtest = this.readySubtests.get(i);

      // Check if the specified subtest is in the map. If it is not, return
      // early to avoid trying to process any more tests since they would be
      // out of order.
      if (subtest === undefined) {
        return;
      }

      // Call isClearToSend() in the loop so that it is:
      // - Only called if there are results to report in the correct order.
      // - Guaranteed to only be called a maximum of once per call to
      //   processReadySubtestRange().
      canSend ||= this.isClearToSend();

      if (!canSend) {
        return;
      }

      // Report the subtest's results and remove it from the ready map.
      subtest.finalize();
      this.readySubtests.delete(i);
    }
  }

  createSubtest(Factory, name, options, fn, overrides) {
    if (typeof name === 'function') {
      fn = name;
    } else if (name !== null && typeof name === 'object') {
      fn = options;
      options = name;
    } else if (typeof options === 'function') {
      fn = options;
    }

    if (options === null || typeof options !== 'object') {
      options = kEmptyObject;
    }

    let parent = this;

    // If this test has already ended, attach this test to the root test so
    // that the error can be properly reported.
    const preventAddingSubtests = this.finished || this.buildPhaseFinished;
    if (preventAddingSubtests) {
      while (parent.parent !== null) {
        parent = parent.parent;
      }
    }

    const test = new Factory({ __proto__: null, fn, name, parent, ...options, ...overrides });

    if (parent.waitingOn === 0) {
      parent.waitingOn = test.childNumber;
    }

    if (preventAddingSubtests) {
      test.fail(
        new ERR_TEST_FAILURE(
          'test could not be started because its parent finished',
          kParentAlreadyFinished,
        ),
      );
    }

    ArrayPrototypePush(parent.subtests, test);
    return test;
  }

  #abortHandler = () => {
    const error = this.outerSignal?.reason || new AbortError('The test was aborted');
    error.failureType = kAborted;
    this.#cancel(error);
  };

  #cancel(error) {
    if (this.endTime !== null || this.error !== null) {
      return;
    }

    this.fail(error ||
      new ERR_TEST_FAILURE(
        'test did not finish before its parent and was cancelled',
        kCancelledByParent,
      ),
    );
    this.cancelled = true;
    this.abortController.abort();
  }

  computeInheritedHooks() {
    if (this.parent.hooks.beforeEach.length > 0) {
      ArrayPrototypeUnshiftApply(
        this.hooks.beforeEach,
        ArrayPrototypeSlice(this.parent.hooks.beforeEach),
      );
    }

    if (this.parent.hooks.afterEach.length > 0) {
      ArrayPrototypePushApply(
        this.hooks.afterEach, ArrayPrototypeSlice(this.parent.hooks.afterEach),
      );
    }
  }

  createHook(name, fn, options) {
    validateOneOf(name, 'hook name', kHookNames);
    // eslint-disable-next-line no-use-before-define
    const hook = new TestHook(fn, options);
    if (name === 'before' || name === 'after') {
      hook.run = runOnce(hook.run, kRunOnceOptions);
    }
    if (name === 'before' && this.startTime !== null) {
      // Test has already started, run the hook immediately
      PromisePrototypeThen(hook.run(this.getRunArgs()), () => {
        if (hook.error != null) {
          this.fail(hook.error);
        }
      });
    }
    if (name === 'afterEach') {
      // afterEach hooks for the current test should run in the order that they
      // are created. However, the current test's afterEach hooks should run
      // prior to any ancestor afterEach hooks.
      ArrayPrototypeSplice(this.hooks[name], this.hooks.ownAfterEachCount, 0, hook);
      this.hooks.ownAfterEachCount++;
    } else {
      ArrayPrototypePush(this.hooks[name], hook);
    }
  }

  fail(err) {
    if (this.error !== null) {
      return;
    }

    this.passed = false;
    this.error = err;
  }

  pass() {
    if (this.error !== null) {
      return;
    }

    this.passed = true;
  }

  skip(message) {
    this.skipped = true;
    this.message = message;
  }

  todo(message) {
    this.isTodo = true;
    this.message = message;
  }

  diagnostic(message) {
    ArrayPrototypePush(this.diagnostics, message);
  }

  start() {
    this.applyFilters();

    if (this.filtered) {
      noopTestStream ??= new TestsStream();
      this.reporter = noopTestStream;
      this.run = this.filteredRun;
    } else {
      this.testNumber = ++this.parent.outputSubtestCount;
    }

    // If there is enough available concurrency to run the test now, then do
    // it. Otherwise, return a Promise to the caller and mark the test as
    // pending for later execution.
    this.reporter.enqueue(this.nesting, this.loc, this.name, this.reportedType);
    if (this.root.harness.buildPromise || !this.parent.hasConcurrency()) {
      const deferred = PromiseWithResolvers();

      deferred.test = this;
      this.parent.addPendingSubtest(deferred);
      return deferred.promise;
    }

    this.reporter.dequeue(this.nesting, this.loc, this.name, this.reportedType);
    return this.run();
  }

  [kShouldAbort]() {
    if (this.signal.aborted || this.outerSignal?.aborted) {
      this.#abortHandler();
      return true;
    }
  }

  getRunArgs() {
    const ctx = new TestContext(this);
    return { __proto__: null, ctx, args: [ctx] };
  }

  async runHook(hook, args) {
    validateOneOf(hook, 'hook name', kHookNames);
    try {
      const hooks = this.hooks[hook];
      for (let i = 0; i < hooks.length; ++i) {
        const hook = hooks[i];
        await hook.run(args);
        if (hook.error) {
          throw hook.error;
        }
      }
    } catch (err) {
      const error = new ERR_TEST_FAILURE(`failed running ${hook} hook`, kHookFailure);
      error.cause = isTestFailureError(err) ? err.cause : err;
      throw error;
    }
  }

  async filteredRun() {
    this.pass();
    this.subtests = [];
    this.report = noop;
    queueMicrotask(() => this.postRun());
  }

  async run() {
    if (this.parent !== null) {
      this.parent.activeSubtests++;
      this.computeInheritedHooks();
    }
    this.startTime ??= hrtime();

    if (this[kShouldAbort]()) {
      this.postRun();
      return;
    }

    const hookArgs = this.getRunArgs();
    const { args, ctx } = hookArgs;

    if (this.plan === null && this.expectedAssertions) {
      ctx.plan(this.expectedAssertions);
    }

    const after = async () => {
      if (this.hooks.after.length > 0) {
        await this.runHook('after', hookArgs);
      }
    };
    const afterEach = runOnce(async () => {
      if (this.parent?.hooks.afterEach.length > 0 && !this.skipped) {
        await this.parent.runHook('afterEach', hookArgs);
      }
    }, kRunOnceOptions);

    let stopPromise;

    try {
      if (this.parent?.hooks.before.length > 0) {
        // This hook usually runs immediately, we need to wait for it to finish
        await this.parent.runHook('before', this.parent.getRunArgs());
      }
      if (this.parent?.hooks.beforeEach.length > 0 && !this.skipped) {
        await this.parent.runHook('beforeEach', hookArgs);
      }
      stopPromise = stopTest(this.timeout, this.signal);
      const runArgs = ArrayPrototypeSlice(args);
      ArrayPrototypeUnshift(runArgs, this.fn, ctx);

      if (this.fn.length === runArgs.length - 1) {
        // This test is using legacy Node.js error first callbacks.
        const { promise, cb } = createDeferredCallback();

        ArrayPrototypePush(runArgs, cb);
        const ret = ReflectApply(this.runInAsyncScope, this, runArgs);

        if (isPromise(ret)) {
          this.fail(new ERR_TEST_FAILURE(
            'passed a callback but also returned a Promise',
            kCallbackAndPromisePresent,
          ));
          await SafePromiseRace([ret, stopPromise]);
        } else {
          await SafePromiseRace([PromiseResolve(promise), stopPromise]);
        }
      } else {
        // This test is synchronous or using Promises.
        const promise = ReflectApply(this.runInAsyncScope, this, runArgs);
        await SafePromiseRace([PromiseResolve(promise), stopPromise]);
      }

      this[kShouldAbort]();
      this.plan?.check();
      this.pass();
      await afterEach();
      await after();
    } catch (err) {
      if (isTestFailureError(err)) {
        if (err.failureType === kTestTimeoutFailure) {
          this.#cancel(err);
        } else {
          this.fail(err);
        }
      } else {
        this.fail(new ERR_TEST_FAILURE(err, kTestCodeFailure));
      }
      try { await afterEach(); } catch { /* test is already failing, let's ignore the error */ }
      try { await after(); } catch { /* Ignore error. */ }
    } finally {
      stopPromise?.[SymbolDispose]();

      // Do not abort hooks and the root test as hooks instance are shared between tests suite so aborting them will
      // cause them to not run for further tests.
      if (this.parent !== null) {
        this.abortController.abort();
      }
    }

    if (this.parent !== null || typeof this.hookType === 'string') {
      // Clean up the test. Then, try to report the results and execute any
      // tests that were pending due to available concurrency.
      //
      // The root test is skipped here because it is a special case. Its
      // postRun() method is called when the process is getting ready to exit.
      // This helps catch any asynchronous activity that occurs after the tests
      // have finished executing.
      this.postRun();
    } else if (this.config.forceExit) {
      // This is the root test, and all known tests and hooks have finished
      // executing. If the user wants to force exit the process regardless of
      // any remaining ref'ed handles, then do that now. It is theoretically
      // possible that a ref'ed handle could asynchronously create more tests,
      // but the user opted into this behavior.
      const promises = [];

      for (let i = 0; i < reporterScope.reporters.length; i++) {
        const { destination } = reporterScope.reporters[i];

        ArrayPrototypePush(promises, new Promise((resolve) => {
          destination.on('unpipe', () => {
            if (!destination.closed && typeof destination.close === 'function') {
              destination.close(resolve);
            } else {
              resolve();
            }
          });
        }));
      }

      this.harness.teardown();
      await SafePromiseAllReturnVoid(promises);
      process.exit();
    }
  }

  postRun(pendingSubtestsError) {
    // If the test was cancelled before it started, then the start and end
    // times need to be corrected.
    this.endTime ??= hrtime();
    this.startTime ??= this.endTime;

    // The test has run, so recursively cancel any outstanding subtests and
    // mark this test as failed if any subtests failed.
    this.pendingSubtests = [];
    let failed = 0;
    for (let i = 0; i < this.subtests.length; i++) {
      const subtest = this.subtests[i];

      if (!subtest.finished) {
        subtest.#cancel(pendingSubtestsError);
        subtest.postRun(pendingSubtestsError);
      }
      if (!subtest.passed && !subtest.isTodo) {
        failed++;
      }
    }

    if ((this.passed || this.parent === null) && failed > 0) {
      const subtestString = `subtest${failed > 1 ? 's' : ''}`;
      const msg = `${failed} ${subtestString} failed`;

      this.fail(new ERR_TEST_FAILURE(msg, kSubtestsFailed));
    }

    this.outerSignal?.removeEventListener('abort', this.#abortHandler);
    this.mock?.reset();

    if (this.parent !== null) {
      if (!this.filtered) {
        const report = this.getReportDetails();
        report.details.passed = this.passed;
        this.testNumber ||= ++this.parent.outputSubtestCount;
        this.reporter.complete(this.nesting, this.loc, this.testNumber, this.name, report.details, report.directive);
        this.parent.activeSubtests--;
      }

      this.parent.addReadySubtest(this);
      this.parent.processReadySubtestRange(false);
      this.parent.processPendingSubtests();
    } else if (!this.reported) {
      const {
        diagnostics,
        harness,
        loc,
        nesting,
        reporter,
      } = this;

      this.reported = true;
      reporter.plan(nesting, loc, harness.counters.topLevel);

      // Call this harness.coverage() before collecting diagnostics, since failure to collect coverage is a diagnostic.
      const coverage = harness.coverage();
      harness.snapshotManager?.writeSnapshotFiles();
      for (let i = 0; i < diagnostics.length; i++) {
        reporter.diagnostic(nesting, loc, diagnostics[i]);
      }

      const duration = this.duration();
      reporter.diagnostic(nesting, loc, `tests ${harness.counters.tests}`);
      reporter.diagnostic(nesting, loc, `suites ${harness.counters.suites}`);
      reporter.diagnostic(nesting, loc, `pass ${harness.counters.passed}`);
      reporter.diagnostic(nesting, loc, `fail ${harness.counters.failed}`);
      reporter.diagnostic(nesting, loc, `cancelled ${harness.counters.cancelled}`);
      reporter.diagnostic(nesting, loc, `skipped ${harness.counters.skipped}`);
      reporter.diagnostic(nesting, loc, `todo ${harness.counters.todo}`);
      reporter.diagnostic(nesting, loc, `duration_ms ${duration}`);

      if (coverage) {
        const coverages = [
          { __proto__: null, actual: coverage.totals.coveredLinePercent,
            threshold: this.config.lineCoverage, name: 'line' },

          { __proto__: null, actual: coverage.totals.coveredBranchPercent,
            threshold: this.config.branchCoverage, name: 'branch' },

          { __proto__: null, actual: coverage.totals.coveredFunctionPercent,
            threshold: this.config.functionCoverage, name: 'function' },
        ];

        for (let i = 0; i < coverages.length; i++) {
          const { threshold, actual, name } = coverages[i];
          if (actual < threshold) {
            harness.success = false;
            process.exitCode = kGenericUserError;
            reporter.diagnostic(nesting, loc, `Error: ${NumberPrototypeToFixed(actual, 2)}% ${name} coverage does not meet threshold of ${threshold}%.`);
          }
        }

        reporter.coverage(nesting, loc, coverage);
      }

      reporter.summary(
        nesting, loc?.file, harness.success, harness.counters, duration,
      );

      if (harness.watching) {
        this.reported = false;
        harness.resetCounters();
        assertObj = undefined;
      } else {
        reporter.end();
      }
    }
  }

  isClearToSend() {
    return this.parent === null ||
      (
        this.parent.waitingOn === this.childNumber && this.parent.isClearToSend()
      );
  }

  finalize() {
    // By the time this function is called, the following can be relied on:
    // - The current test has completed or been cancelled.
    // - All of this test's subtests have completed or been cancelled.
    // - It is the current test's turn to report its results.

    // Report any subtests that have not been reported yet. Since all of the
    // subtests have finished, it's safe to pass true to
    // processReadySubtestRange(), which will finalize all remaining subtests.
    this.processReadySubtestRange(true);

    // Output this test's results and update the parent's waiting counter.
    this.report();
    this.parent.waitingOn++;
    this.finished = true;

    if (this.parent === this.root &&
        this.root.waitingOn > this.root.subtests.length) {
      // At this point all of the tests have finished running. However, there
      // might be ref'ed handles keeping the event loop alive. This gives the
      // global after() hook a chance to clean them up. The user may also
      // want to force the test runner to exit despite ref'ed handles.
      this.root.run();
    }
  }

  duration() {
    // Duration is recorded in BigInt nanoseconds. Convert to milliseconds.
    return Number(this.endTime - this.startTime) / 1_000_000;
  }

  getReportDetails() {
    let directive;
    const details = { __proto__: null, duration_ms: this.duration() };

    if (this.skipped) {
      directive = this.reporter.getSkip(this.message);
    } else if (this.isTodo) {
      directive = this.reporter.getTodo(this.message);
    }

    if (this.reportedType) {
      details.type = this.reportedType;
    }
    if (!this.passed) {
      details.error = this.error;
    }
    return { __proto__: null, details, directive };
  }

  report() {
    countCompletedTest(this);
    if (this.outputSubtestCount > 0) {
      this.reporter.plan(this.subtests[0].nesting, this.loc, this.outputSubtestCount);
    } else {
      this.reportStarted();
    }
    const report = this.getReportDetails();

    if (this.passed) {
      this.reporter.ok(this.nesting, this.loc, this.testNumber, this.name, report.details, report.directive);
    } else {
      this.reporter.fail(this.nesting, this.loc, this.testNumber, this.name, report.details, report.directive);
    }

    for (let i = 0; i < this.diagnostics.length; i++) {
      this.reporter.diagnostic(this.nesting, this.loc, this.diagnostics[i]);
    }
  }

  reportStarted() {
    if (this.#reportedSubtest || this.parent === null) {
      return;
    }
    this.#reportedSubtest = true;
    this.parent.reportStarted();
    this.reporter.start(this.nesting, this.loc, this.name);
  }
}

class TestHook extends Test {
  reportedType = 'hook';
  #args;
  constructor(fn, options) {
    const { hookType, loc, parent, timeout, signal } = options;
    super({
      __proto__: null,
      fn,
      loc,
      timeout,
      signal,
      harness: parent.root.harness,
    });
    this.parentTest = parent;
    this.hookType = hookType;
  }
  run(args) {
    if (this.error && !this.outerSignal?.aborted) {
      this.passed = false;
      this.error = null;
      this.abortController.abort();
      this.abortController = new AbortController();
      this.signal = this.abortController.signal;
    }

    this.#args = args;
    return super.run();
  }
  getRunArgs() {
    return this.#args;
  }
  willBeFilteredByName() {
    return false;
  }
  postRun() {
    const { error, loc, parentTest: parent } = this;

    // Report failures in the root test's after() hook.
    if (error && parent === parent.root && this.hookType === 'after') {
      if (isTestFailureError(error)) {
        error.failureType = kHookFailure;
      }

      this.endTime ??= hrtime();
      parent.reporter.fail(0, loc, parent.subtests.length + 1, loc.file, {
        __proto__: null,
        duration_ms: this.duration(),
        error,
      }, undefined);
    }
  }
}

class Suite extends Test {
  reportedType = 'suite';
  constructor(options) {
    super(options);

    if (this.config.testNamePatterns !== null &&
        this.config.testSkipPatterns !== null &&
        !options.skip) {
      this.fn = options.fn || this.fn;
      this.skipped = false;
    }

    this.buildSuite = this.createBuild();
    this.fn = noop;
  }

  async createBuild() {
    try {
      const { ctx, args } = this.getRunArgs();
      const runArgs = [this.fn, ctx];
      ArrayPrototypePushApply(runArgs, args);
      await ReflectApply(this.runInAsyncScope, this, runArgs);
    } catch (err) {
      this.fail(new ERR_TEST_FAILURE(err, kTestCodeFailure));
    }

    this.buildPhaseFinished = true;
  }

  getRunArgs() {
    const ctx = new SuiteContext(this);
    return { __proto__: null, ctx, args: [ctx] };
  }

  async run() {
    this.computeInheritedHooks();
    const hookArgs = this.getRunArgs();

    let stopPromise;
    const after = runOnce(() => this.runHook('after', hookArgs), kRunOnceOptions);
    try {
      this.parent.activeSubtests++;
      await this.buildSuite;
      this.startTime = hrtime();

      if (this[kShouldAbort]()) {
        this.subtests = [];
        this.postRun();
        return;
      }

      if (this.parent.hooks.before.length > 0) {
        await this.parent.runHook('before', this.parent.getRunArgs());
      }

      await this.runHook('before', hookArgs);

      stopPromise = stopTest(this.timeout, this.signal);
      const subtests = this.skipped || this.error ? [] : this.subtests;
      const promise = SafePromiseAll(subtests, (subtests) => subtests.start());

      await SafePromiseRace([promise, stopPromise]);
      await after();

      this.pass();
    } catch (err) {
      try { await after(); } catch { /* suite is already failing */ }
      if (isTestFailureError(err)) {
        this.fail(err);
      } else {
        this.fail(new ERR_TEST_FAILURE(err, kTestCodeFailure));
      }
    } finally {
      stopPromise?.[SymbolDispose]();
    }

    this.postRun();
  }
}

function getFullName(test) {
  let fullName = test.name;

  for (let t = test.parent; t !== t.root; t = t.parent) {
    fullName = `${t.name} > ${fullName}`;
  }

  return fullName;
}

module.exports = {
  kCancelledByParent,
  kSubtestsFailed,
  kTestCodeFailure,
  kTestTimeoutFailure,
  kAborted,
  kUnwrapErrors,
  Suite,
  Test,
};
