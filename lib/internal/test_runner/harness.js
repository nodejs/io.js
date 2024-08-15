'use strict';
const {
  ArrayPrototypeForEach,
  FunctionPrototypeBind,
  PromiseResolve,
  SafeMap,
} = primordials;
const { getCallerLocation } = internalBinding('util');
const {
  createHook,
  executionAsyncId,
} = require('async_hooks');
const { relative } = require('path');
const {
  codes: {
    ERR_TEST_FAILURE,
  },
} = require('internal/errors');
const { exitCodes: { kGenericUserError } } = internalBinding('errors');

const { kEmptyObject } = require('internal/util');
const { kCancelledByParent, Test, Suite } = require('internal/test_runner/test');
const {
  parseCommandLine,
  reporterScope,
  setupTestReporters,
  shouldColorizeTestFiles,
} = require('internal/test_runner/utils');
const { queueMicrotask } = require('internal/process/task_queues');
const { bigint: hrtime } = process.hrtime;
const resolvedPromise = PromiseResolve();
const testResources = new SafeMap();
let globalRoot;

testResources.set(reporterScope.asyncId(), reporterScope);

function createTestTree(options = kEmptyObject) {
  globalRoot = setup(new Test({ __proto__: null, ...options, name: '<root>' }));
  return globalRoot;
}

function createProcessEventHandler(eventName, rootTest) {
  return (err) => {
    if (rootTest.harness.bootstrapPromise) {
      // Something went wrong during the asynchronous portion of bootstrapping
      // the test runner. Since the test runner is not setup properly, we can't
      // do anything but throw the error.
      throw err;
    }

    const test = testResources.get(executionAsyncId());

    // Check if this error is coming from a reporter. If it is, throw it.
    if (test === reporterScope) {
      throw err;
    }

    // Check if this error is coming from a test or test hook. If it is, fail the test.
    if (!test || test.finished || test.hookType) {
      // If the test is already finished or the resource that created the error
      // is not mapped to a Test, report this as a top level diagnostic.
      let msg;

      if (test) {
        const name = test.hookType ? `Test hook "${test.hookType}"` : `Test "${test.name}"`;
        let locInfo = '';
        if (test.loc) {
          const relPath = relative(process.cwd(), test.loc.file);
          locInfo = ` at ${relPath}:${test.loc.line}:${test.loc.column}`;
        }

        msg = `Error: ${name}${locInfo} generated asynchronous ` +
          'activity after the test ended. This activity created the error ' +
          `"${err}" and would have caused the test to fail, but instead ` +
          `triggered an ${eventName} event.`;
      } else {
        msg = 'Error: A resource generated asynchronous activity after ' +
          `the test ended. This activity created the error "${err}" which ` +
          `triggered an ${eventName} event, caught by the test runner.`;
      }

      rootTest.diagnostic(msg);
      process.exitCode = kGenericUserError;
      return;
    }

    test.fail(new ERR_TEST_FAILURE(err, eventName));
    test.abortController.abort();
  };
}

function configureCoverage(rootTest, globalOptions) {
  if (!globalOptions.coverage) {
    return null;
  }

  const { setupCoverage } = require('internal/test_runner/coverage');

  try {
    return setupCoverage(globalOptions);
  } catch (err) {
    const msg = `Warning: Code coverage could not be enabled. ${err}`;

    rootTest.diagnostic(msg);
    process.exitCode = kGenericUserError;
  }
}

function collectCoverage(rootTest, coverage) {
  if (!coverage) {
    return null;
  }

  let summary = null;

  try {
    summary = coverage.summary();
    coverage.cleanup();
  } catch (err) {
    const op = summary ? 'clean up' : 'report';
    const msg = `Warning: Could not ${op} code coverage. ${err}`;

    rootTest.diagnostic(msg);
    process.exitCode = kGenericUserError;
  }

  return summary;
}

function setup(root) {
  if (root.startTime !== null) {
    return root;
  }

  // Parse the command line options before the hook is enabled. We don't want
  // global input validation errors to end up in the uncaughtException handler.
  const globalOptions = parseCommandLine();

  const hook = createHook({
    __proto__: null,
    init(asyncId, type, triggerAsyncId, resource) {
      if (resource instanceof Test) {
        testResources.set(asyncId, resource);
        return;
      }

      const parent = testResources.get(triggerAsyncId);

      if (parent !== undefined) {
        testResources.set(asyncId, parent);
      }
    },
    destroy(asyncId) {
      testResources.delete(asyncId);
    },
  });

  hook.enable();

  const exceptionHandler =
    createProcessEventHandler('uncaughtException', root);
  const rejectionHandler =
    createProcessEventHandler('unhandledRejection', root);
  const coverage = configureCoverage(root, globalOptions);
  const exitHandler = async () => {
    if (root.subtests.length === 0 && (root.hooks.before.length > 0 || root.hooks.after.length > 0)) {
      // Run global before/after hooks in case there are no tests
      await root.run();
    }
    root.postRun(new ERR_TEST_FAILURE(
      'Promise resolution is still pending but the event loop has already resolved',
      kCancelledByParent));

    hook.disable();
    process.removeListener('uncaughtException', exceptionHandler);
    process.removeListener('unhandledRejection', rejectionHandler);
    process.removeListener('beforeExit', exitHandler);
    if (globalOptions.isTestRunner) {
      process.removeListener('SIGINT', terminationHandler);
      process.removeListener('SIGTERM', terminationHandler);
    }
  };

  const terminationHandler = () => {
    exitHandler();
    process.exit();
  };

  process.on('uncaughtException', exceptionHandler);
  process.on('unhandledRejection', rejectionHandler);
  process.on('beforeExit', exitHandler);
  // TODO(MoLow): Make it configurable to hook when isTestRunner === false.
  if (globalOptions.isTestRunner) {
    process.on('SIGINT', terminationHandler);
    process.on('SIGTERM', terminationHandler);
  }

  root.harness = {
    __proto__: null,
    allowTestsToRun: false,
    bootstrapPromise: resolvedPromise,
    watching: false,
    coverage: FunctionPrototypeBind(collectCoverage, null, root, coverage),
    resetCounters() {
      root.harness.counters = {
        __proto__: null,
        all: 0,
        failed: 0,
        passed: 0,
        cancelled: 0,
        skipped: 0,
        todo: 0,
        topLevel: 0,
        suites: 0,
      };
    },
    counters: null,
    shouldColorizeTestFiles: shouldColorizeTestFiles(globalOptions.destinations),
    teardown: exitHandler,
    snapshotManager: null,
  };
  root.harness.resetCounters();
  root.startTime = hrtime();
  return root;
}

function lazyBootstrapRoot() {
  if (!globalRoot) {
    // This is where the test runner is bootstrapped when node:test is used
    // without the --test flag or the run() API.
    createTestTree({ __proto__: null, entryFile: process.argv?.[1] });
    globalRoot.reporter.on('test:fail', (data) => {
      if (data.todo === undefined || data.todo === false) {
        process.exitCode = kGenericUserError;
      }
    });
    globalRoot.harness.bootstrapPromise = setupTestReporters(globalRoot.reporter);
  }
  return globalRoot;
}

async function startSubtestAfterBootstrap(subtest) {
  if (subtest.root.harness.bootstrapPromise) {
    // Only incur the overhead of awaiting the Promise once.
    await subtest.root.harness.bootstrapPromise;
    subtest.root.harness.bootstrapPromise = null;
    queueMicrotask(() => {
      subtest.root.harness.allowTestsToRun = true;
      subtest.root.processPendingSubtests();
    });
  }

  await subtest.start();
}

function runInParentContext(Factory) {
  function run(name, options, fn, overrides) {
    const parent = testResources.get(executionAsyncId()) || lazyBootstrapRoot();
    const subtest = parent.createSubtest(Factory, name, options, fn, overrides);
    if (parent instanceof Suite) {
      return PromiseResolve();
    }

    return startSubtestAfterBootstrap(subtest);
  }

  const test = (name, options, fn) => {
    const overrides = {
      __proto__: null,
      loc: getCallerLocation(),
    };

    return run(name, options, fn, overrides);
  };
  ArrayPrototypeForEach(['skip', 'todo', 'only'], (keyword) => {
    test[keyword] = (name, options, fn) => {
      const overrides = {
        __proto__: null,
        [keyword]: true,
        loc: getCallerLocation(),
      };

      return run(name, options, fn, overrides);
    };
  });
  return test;
}

function hook(hook) {
  return (fn, options) => {
    const parent = testResources.get(executionAsyncId()) || lazyBootstrapRoot();
    parent.createHook(hook, fn, {
      __proto__: null,
      ...options,
      parent,
      hookType: hook,
      loc: getCallerLocation(),
    });
  };
}

module.exports = {
  createTestTree,
  test: runInParentContext(Test),
  suite: runInParentContext(Suite),
  before: hook('before'),
  after: hook('after'),
  beforeEach: hook('beforeEach'),
  afterEach: hook('afterEach'),
};
