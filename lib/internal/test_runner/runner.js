'use strict';

const {
  ArrayIsArray,
  ArrayPrototypeEvery,
  ArrayPrototypeFilter,
  ArrayPrototypeFind,
  ArrayPrototypeForEach,
  ArrayPrototypeIncludes,
  ArrayPrototypeJoin,
  ArrayPrototypeMap,
  ArrayPrototypePush,
  ArrayPrototypePushApply,
  ArrayPrototypeShift,
  ArrayPrototypeSlice,
  ArrayPrototypeSome,
  ArrayPrototypeSort,
  ObjectAssign,
  PromisePrototypeThen,
  PromiseWithResolvers,
  SafeMap,
  SafePromiseAll,
  SafePromiseAllReturnVoid,
  SafePromiseAllSettledReturnVoid,
  SafeSet,
  StringPrototypeIndexOf,
  StringPrototypeSlice,
  StringPrototypeStartsWith,
  Symbol,
  TypedArrayPrototypeGetLength,
  TypedArrayPrototypeSubarray,
} = primordials;

const { spawn } = require('child_process');
const { finished } = require('internal/streams/end-of-stream');
const { resolve, sep, isAbsolute } = require('path');
const { DefaultDeserializer, DefaultSerializer } = require('v8');
const { getOptionValue } = require('internal/options');
const { Interface } = require('internal/readline/interface');
const { deserializeError } = require('internal/error_serdes');
const { Buffer } = require('buffer');
const { FilesWatcher } = require('internal/watch_mode/files_watcher');
const console = require('internal/console/global');
const {
  codes: {
    ERR_INVALID_ARG_TYPE,
    ERR_INVALID_ARG_VALUE,
    ERR_TEST_FAILURE,
  },
} = require('internal/errors');
const esmLoader = require('internal/modules/esm/loader');
const {
  validateArray,
  validateBoolean,
  validateFunction,
  validateObject,
  validateOneOf,
  validateInteger,
  validateString,
  validateStringArray,
} = require('internal/validators');
const { getInspectPort, isUsingInspector, isInspectorMessage } = require('internal/util/inspector');
const { isRegExp } = require('internal/util/types');
const { pathToFileURL } = require('internal/url');
const {
  kEmptyObject,
} = require('internal/util');
const { kEmitMessage } = require('internal/test_runner/tests_stream');
const {
  createTestTree,
  startSubtestAfterBootstrap,
} = require('internal/test_runner/harness');
const {
  kAborted,
  kCancelledByParent,
  kSubtestsFailed,
  kTestCodeFailure,
  kTestTimeoutFailure,
  Test,
} = require('internal/test_runner/test');

const {
  convertStringToRegExp,
  countCompletedTest,
  kDefaultPattern,
  parseCommandLine,
} = require('internal/test_runner/utils');
const { Glob } = require('internal/fs/glob');
const { once } = require('events');
const {
  triggerUncaughtException,
  exitCodes: { kGenericUserError },
} = internalBinding('errors');
let debug = require('internal/util/debuglog').debuglog('test_runner', (fn) => {
  debug = fn;
});

const kIsolatedProcessName = Symbol('kIsolatedProcessName');
const kFilterArgs = ['--test', '--experimental-test-coverage', '--watch'];
const kFilterArgValues = ['--test-reporter', '--test-reporter-destination'];
const kDiagnosticsFilterArgs = ['tests', 'suites', 'pass', 'fail', 'cancelled', 'skipped', 'todo', 'duration_ms'];

const kCanceledTests = new SafeSet()
  .add(kCancelledByParent).add(kAborted).add(kTestTimeoutFailure);

let kResistStopPropagation;

function createTestFileList(patterns, cwd) {
  const hasUserSuppliedPattern = patterns != null;
  if (!patterns || patterns.length === 0) {
    patterns = [kDefaultPattern];
  }
  const glob = new Glob(patterns, {
    __proto__: null,
    cwd,
    exclude: (name) => name === 'node_modules',
  });
  const results = glob.globSync();

  if (hasUserSuppliedPattern && results.length === 0 && ArrayPrototypeEvery(glob.matchers, (m) => !m.hasMagic())) {
    console.error(`Could not find '${ArrayPrototypeJoin(patterns, ', ')}'`);
    process.exit(kGenericUserError);
  }

  return ArrayPrototypeSort(results);
}

function filterExecArgv(arg, i, arr) {
  return !ArrayPrototypeIncludes(kFilterArgs, arg) &&
  !ArrayPrototypeSome(kFilterArgValues, (p) => arg === p || (i > 0 && arr[i - 1] === p) || StringPrototypeStartsWith(arg, `${p}=`));
}

function getRunArgs(path, { forceExit,
                            inspectPort,
                            testNamePatterns,
                            testSkipPatterns,
                            only,
                            argv: suppliedArgs,
                            execArgv,
                            cwd }) {
  const argv = ArrayPrototypeFilter(process.execArgv, filterExecArgv);
  if (forceExit === true) {
    ArrayPrototypePush(argv, '--test-force-exit');
  }
  if (isUsingInspector()) {
    ArrayPrototypePush(argv, `--inspect-port=${getInspectPort(inspectPort)}`);
  }
  if (testNamePatterns != null) {
    ArrayPrototypeForEach(testNamePatterns, (pattern) => ArrayPrototypePush(argv, `--test-name-pattern=${pattern}`));
  }
  if (testSkipPatterns != null) {
    ArrayPrototypeForEach(testSkipPatterns, (pattern) => ArrayPrototypePush(argv, `--test-skip-pattern=${pattern}`));
  }
  if (only === true) {
    ArrayPrototypePush(argv, '--test-only');
  }

  ArrayPrototypePushApply(argv, execArgv);

  if (path === kIsolatedProcessName) {
    ArrayPrototypePush(argv, '--test');
    ArrayPrototypePushApply(argv, ArrayPrototypeSlice(process.argv, 1));
  } else {
    ArrayPrototypePush(argv, path);
  }

  ArrayPrototypePushApply(argv, suppliedArgs);

  return argv;
}

const serializer = new DefaultSerializer();
serializer.writeHeader();
const v8Header = serializer.releaseBuffer();
const kV8HeaderLength = TypedArrayPrototypeGetLength(v8Header);
const kSerializedSizeHeader = 4 + kV8HeaderLength;

class FileTest extends Test {
  // This class maintains two buffers:
  #reportBuffer = []; // Parsed items waiting for this.isClearToSend()
  #rawBuffer = []; // Raw data waiting to be parsed
  #rawBufferSize = 0;
  #reportedChildren = 0;
  failedSubtests = false;

  constructor(options) {
    super(options);
    this.loc ??= {
      __proto__: null,
      line: 1,
      column: 1,
      file: resolve(this.name),
    };
  }

  #skipReporting() {
    return this.#reportedChildren > 0 && (!this.error || this.error.failureType === kSubtestsFailed);
  }
  #checkNestedComment(comment) {
    const firstSpaceIndex = StringPrototypeIndexOf(comment, ' ');
    if (firstSpaceIndex === -1) return false;
    const secondSpaceIndex = StringPrototypeIndexOf(comment, ' ', firstSpaceIndex + 1);
    return secondSpaceIndex === -1 &&
          ArrayPrototypeIncludes(kDiagnosticsFilterArgs, StringPrototypeSlice(comment, 0, firstSpaceIndex));
  }
  #handleReportItem(item) {
    const isTopLevel = item.data.nesting === 0;
    if (isTopLevel) {
      if (item.type === 'test:plan' && this.#skipReporting()) {
        return;
      }
      if (item.type === 'test:diagnostic' && this.#checkNestedComment(item.data.message)) {
        return;
      }
    }
    if (item.data.details?.error) {
      item.data.details.error = deserializeError(item.data.details.error);
    }
    if (item.type === 'test:pass' || item.type === 'test:fail') {
      item.data.testNumber = isTopLevel ? (this.root.harness.counters.topLevel + 1) : item.data.testNumber;
      countCompletedTest({
        __proto__: null,
        name: item.data.name,
        finished: true,
        skipped: item.data.skip !== undefined,
        isTodo: item.data.todo !== undefined,
        passed: item.type === 'test:pass',
        cancelled: kCanceledTests.has(item.data.details?.error?.failureType),
        nesting: item.data.nesting,
        reportedType: item.data.details?.type,
      }, this.root.harness);
    }
    this.reporter[kEmitMessage](item.type, item.data);
  }
  #accumulateReportItem(item) {
    if (item.type !== 'test:pass' && item.type !== 'test:fail') {
      return;
    }
    this.#reportedChildren++;
    if (item.data.nesting === 0 && item.type === 'test:fail') {
      this.failedSubtests = true;
    }
  }
  #drainReportBuffer() {
    if (this.#reportBuffer.length > 0) {
      ArrayPrototypeForEach(this.#reportBuffer, (ast) => this.#handleReportItem(ast));
      this.#reportBuffer = [];
    }
  }
  addToReport(item) {
    this.#accumulateReportItem(item);
    if (!this.isClearToSend()) {
      ArrayPrototypePush(this.#reportBuffer, item);
      return;
    }
    this.#drainReportBuffer();
    this.#handleReportItem(item);
  }
  reportStarted() {}
  drain() {
    this.#drainRawBuffer();
    this.#drainReportBuffer();
  }
  report() {
    this.drain();
    const skipReporting = this.#skipReporting();
    if (!skipReporting) {
      super.reportStarted();
      super.report();
    }
  }
  parseMessage(readData) {
    let dataLength = TypedArrayPrototypeGetLength(readData);
    if (dataLength === 0) return;
    const partialV8Header = readData[dataLength - 1] === v8Header[0];

    if (partialV8Header) {
      // This will break if v8Header length (2 bytes) is changed.
      // However it is covered by tests.
      readData = TypedArrayPrototypeSubarray(readData, 0, dataLength - 1);
      dataLength--;
    }

    if (this.#rawBuffer[0] && TypedArrayPrototypeGetLength(this.#rawBuffer[0]) < kSerializedSizeHeader) {
      this.#rawBuffer[0] = Buffer.concat([this.#rawBuffer[0], readData]);
    } else {
      ArrayPrototypePush(this.#rawBuffer, readData);
    }
    this.#rawBufferSize += dataLength;
    this.#processRawBuffer();

    if (partialV8Header) {
      ArrayPrototypePush(this.#rawBuffer, TypedArrayPrototypeSubarray(v8Header, 0, 1));
      this.#rawBufferSize++;
    }
  }
  #drainRawBuffer() {
    while (this.#rawBuffer.length > 0) {
      this.#processRawBuffer();
    }
  }
  #processRawBuffer() {
    // This method is called when it is known that there is at least one message
    let bufferHead = this.#rawBuffer[0];
    let headerIndex = bufferHead.indexOf(v8Header);
    let nonSerialized = Buffer.alloc(0);

    while (bufferHead && headerIndex !== 0) {
      const nonSerializedData = headerIndex === -1 ?
        bufferHead :
        bufferHead.slice(0, headerIndex);
      nonSerialized = Buffer.concat([nonSerialized, nonSerializedData]);
      this.#rawBufferSize -= TypedArrayPrototypeGetLength(nonSerializedData);
      if (headerIndex === -1) {
        ArrayPrototypeShift(this.#rawBuffer);
      } else {
        this.#rawBuffer[0] = TypedArrayPrototypeSubarray(bufferHead, headerIndex);
      }
      bufferHead = this.#rawBuffer[0];
      headerIndex = bufferHead?.indexOf(v8Header);
    }

    if (TypedArrayPrototypeGetLength(nonSerialized) > 0) {
      this.addToReport({
        __proto__: null,
        type: 'test:stdout',
        data: { __proto__: null, file: this.name, message: nonSerialized.toString('utf-8') },
      });
    }

    while (bufferHead?.length >= kSerializedSizeHeader) {
      // We call `readUInt32BE` manually here, because this is faster than first converting
      // it to a buffer and using `readUInt32BE` on that.
      const fullMessageSize = (
        bufferHead[kV8HeaderLength] << 24 |
        bufferHead[kV8HeaderLength + 1] << 16 |
        bufferHead[kV8HeaderLength + 2] << 8 |
        bufferHead[kV8HeaderLength + 3]
      ) + kSerializedSizeHeader;

      if (this.#rawBufferSize < fullMessageSize) break;

      const concatenatedBuffer = this.#rawBuffer.length === 1 ?
        this.#rawBuffer[0] : Buffer.concat(this.#rawBuffer, this.#rawBufferSize);

      const deserializer = new DefaultDeserializer(
        TypedArrayPrototypeSubarray(concatenatedBuffer, kSerializedSizeHeader, fullMessageSize),
      );

      bufferHead = TypedArrayPrototypeSubarray(concatenatedBuffer, fullMessageSize);
      this.#rawBufferSize = TypedArrayPrototypeGetLength(bufferHead);
      this.#rawBuffer = this.#rawBufferSize !== 0 ? [bufferHead] : [];

      deserializer.readHeader();
      const item = deserializer.readValue();
      this.addToReport(item);
    }
  }
}

function runTestFile(path, filesWatcher, opts) {
  const watchMode = filesWatcher != null;
  const testPath = path === kIsolatedProcessName ? '' : path;
  const testOpts = { __proto__: null, signal: opts.signal };
  const subtest = opts.root.createSubtest(FileTest, testPath, testOpts, async (t) => {
    const args = getRunArgs(path, opts);
    const stdio = ['pipe', 'pipe', 'pipe'];
    const env = { __proto__: null, ...process.env, NODE_TEST_CONTEXT: 'child-v8' };
    if (watchMode) {
      stdio.push('ipc');
      env.WATCH_REPORT_DEPENDENCIES = '1';
    }
    if (opts.root.harness.shouldColorizeTestFiles) {
      env.FORCE_COLOR = '1';
    }

    const child = spawn(
      process.execPath, args,
      {
        __proto__: null,
        signal: t.signal,
        encoding: 'utf8',
        env,
        stdio,
        cwd: opts.cwd,
      },
    );
    if (watchMode) {
      filesWatcher.runningProcesses.set(path, child);
      filesWatcher.watcher.watchChildProcessModules(child, path);
    }

    let err;

    child.on('error', (error) => {
      err = error;
    });

    child.stdout.on('data', (data) => {
      subtest.parseMessage(data);
    });

    const rl = new Interface({ __proto__: null, input: child.stderr });
    rl.on('line', (line) => {
      if (isInspectorMessage(line)) {
        process.stderr.write(line + '\n');
        return;
      }

      // stderr cannot be treated as TAP, per the spec. However, we want to
      // surface stderr lines to improve the DX. Inject each line into the
      // test output as an unknown token as if it came from the TAP parser.
      subtest.addToReport({
        __proto__: null,
        type: 'test:stderr',
        data: { __proto__: null, file: path, message: line + '\n' },
      });
    });

    const { 0: { 0: code, 1: signal } } = await SafePromiseAll([
      once(child, 'exit', { __proto__: null, signal: t.signal }),
      finished(child.stdout, { __proto__: null, signal: t.signal }),
    ]);

    if (watchMode) {
      filesWatcher.runningProcesses.delete(path);
      filesWatcher.runningSubtests.delete(path);
      (async () => {
        try {
          await subTestEnded;
        } finally {
          if (filesWatcher.runningSubtests.size === 0) {
            opts.root.reporter[kEmitMessage]('test:watch:drained');
            opts.root.postRun();
          }
        }
      })();
    }

    if (code !== 0 || signal !== null) {
      if (!err) {
        const failureType = subtest.failedSubtests ? kSubtestsFailed : kTestCodeFailure;
        err = ObjectAssign(new ERR_TEST_FAILURE('test failed', failureType), {
          __proto__: null,
          exitCode: code,
          signal: signal,
          // The stack will not be useful since the failures came from tests
          // in a child process.
          stack: undefined,
        });
      }

      throw err;
    }
  });
  const subTestEnded = subtest.start();
  return subTestEnded;
}

function watchFiles(testFiles, opts) {
  const runningProcesses = new SafeMap();
  const runningSubtests = new SafeMap();
  const watcherMode = opts.hasFiles ? 'filter' : 'all';
  const watcher = new FilesWatcher({ __proto__: null, debounce: 200, mode: watcherMode, signal: opts.signal });
  if (!opts.hasFiles) {
    watcher.watchPath(opts.cwd);
  }
  const filesWatcher = { __proto__: null, watcher, runningProcesses, runningSubtests };
  opts.root.harness.watching = true;

  async function restartTestFile(file) {
    const runningProcess = runningProcesses.get(file);
    if (runningProcess) {
      runningProcess.kill();
      await once(runningProcess, 'exit');
    }
    if (!runningSubtests.size) {
      // Reset the topLevel counter
      opts.root.harness.counters.topLevel = 0;
    }
    await runningSubtests.get(file);
    runningSubtests.set(file, runTestFile(file, filesWatcher, opts));
  }

  // Watch for changes in current filtered files
  watcher.on('changed', ({ owners, eventType }) => {
    if (!opts.hasFiles && (eventType === 'rename' || eventType === 'change')) {
      const updatedTestFiles = createTestFileList(opts.globPatterns, opts.cwd);
      const newFileName = ArrayPrototypeFind(updatedTestFiles, (x) => !ArrayPrototypeIncludes(testFiles, x));
      const previousFileName = ArrayPrototypeFind(testFiles, (x) => !ArrayPrototypeIncludes(updatedTestFiles, x));

      testFiles = updatedTestFiles;

      // When file renamed (created / deleted) we need to update the watcher
      if (newFileName) {
        owners = new SafeSet().add(newFileName);
        const resolveFileName = isAbsolute(newFileName) ? newFileName : resolve(opts.cwd, newFileName);
        watcher.filterFile(resolveFileName, owners);
      }

      if (!newFileName && previousFileName) {
        return; // Avoid rerunning files when file deleted
      }
    }

    if (opts.isolation === 'none') {
      PromisePrototypeThen(restartTestFile(kIsolatedProcessName), undefined, (error) => {
        triggerUncaughtException(error, true /* fromPromise */);
      });
    } else {
      watcher.unfilterFilesOwnedBy(owners);
      PromisePrototypeThen(SafePromiseAllReturnVoid(testFiles, async (file) => {
        if (!owners.has(file)) {
          return;
        }

        await restartTestFile(file);
      }, undefined, (error) => {
        triggerUncaughtException(error, true /* fromPromise */);
      }));
    }
  });
  if (opts.signal) {
    kResistStopPropagation ??= require('internal/event_target').kResistStopPropagation;
    opts.signal.addEventListener(
      'abort',
      () => {
        opts.root.harness.watching = false;
        opts.root.postRun();
      },
      { __proto__: null, once: true, [kResistStopPropagation]: true },
    );
  }

  return filesWatcher;
}

function run(options = kEmptyObject) {
  validateObject(options, 'options');

  let {
    testNamePatterns,
    testSkipPatterns,
    shard,
    coverageExcludeGlobs,
    coverageIncludeGlobs,
  } = options;
  const {
    concurrency,
    timeout,
    signal,
    files,
    forceExit,
    inspectPort,
    isolation = 'process',
    watch,
    setup,
    only,
    globPatterns,
    coverage = false,
    lineCoverage = 0,
    branchCoverage = 0,
    functionCoverage = 0,
    execArgv = [],
    argv = [],
    cwd = process.cwd(),
  } = options;

  if (files != null) {
    validateArray(files, 'options.files');
  }
  if (watch != null) {
    validateBoolean(watch, 'options.watch');
  }
  if (forceExit != null) {
    validateBoolean(forceExit, 'options.forceExit');

    if (forceExit && watch) {
      throw new ERR_INVALID_ARG_VALUE(
        'options.forceExit', watch, 'is not supported with watch mode',
      );
    }
  }
  if (only != null) {
    validateBoolean(only, 'options.only');
  }
  if (globPatterns != null) {
    validateArray(globPatterns, 'options.globPatterns');
  }

  validateString(cwd, 'options.cwd');

  if (globPatterns?.length > 0 && files?.length > 0) {
    throw new ERR_INVALID_ARG_VALUE(
      'options.globPatterns', globPatterns, 'is not supported when specifying \'options.files\'',
    );
  }

  if (shard != null) {
    validateObject(shard, 'options.shard');
    // Avoid re-evaluating the shard object in case it's a getter
    shard = { __proto__: null, index: shard.index, total: shard.total };

    validateInteger(shard.total, 'options.shard.total', 1);
    validateInteger(shard.index, 'options.shard.index', 1, shard.total);

    if (watch) {
      throw new ERR_INVALID_ARG_VALUE('options.shard', watch, 'shards not supported with watch mode');
    }
  }
  if (setup != null) {
    validateFunction(setup, 'options.setup');
  }
  if (testNamePatterns != null) {
    if (!ArrayIsArray(testNamePatterns)) {
      testNamePatterns = [testNamePatterns];
    }

    testNamePatterns = ArrayPrototypeMap(testNamePatterns, (value, i) => {
      if (isRegExp(value)) {
        return value;
      }
      const name = `options.testNamePatterns[${i}]`;
      if (typeof value === 'string') {
        return convertStringToRegExp(value, name);
      }
      throw new ERR_INVALID_ARG_TYPE(name, ['string', 'RegExp'], value);
    });
  }
  if (testSkipPatterns != null) {
    if (!ArrayIsArray(testSkipPatterns)) {
      testSkipPatterns = [testSkipPatterns];
    }

    testSkipPatterns = ArrayPrototypeMap(testSkipPatterns, (value, i) => {
      if (isRegExp(value)) {
        return value;
      }
      const name = `options.testSkipPatterns[${i}]`;
      if (typeof value === 'string') {
        return convertStringToRegExp(value, name);
      }
      throw new ERR_INVALID_ARG_TYPE(name, ['string', 'RegExp'], value);
    });
  }
  validateOneOf(isolation, 'options.isolation', ['process', 'none']);
  validateBoolean(coverage, 'options.coverage');
  if (coverageExcludeGlobs != null) {
    if (!ArrayIsArray(coverageExcludeGlobs)) {
      coverageExcludeGlobs = [coverageExcludeGlobs];
    }
    validateStringArray(coverageExcludeGlobs, 'options.coverageExcludeGlobs');
  }
  if (coverageIncludeGlobs != null) {
    if (!ArrayIsArray(coverageIncludeGlobs)) {
      coverageIncludeGlobs = [coverageIncludeGlobs];
    }
    validateStringArray(coverageIncludeGlobs, 'options.coverageIncludeGlobs');
  }
  validateInteger(lineCoverage, 'options.lineCoverage', 0, 100);
  validateInteger(branchCoverage, 'options.branchCoverage', 0, 100);
  validateInteger(functionCoverage, 'options.functionCoverage', 0, 100);

  validateStringArray(argv, 'options.argv');
  validateStringArray(execArgv, 'options.execArgv');

  const rootTestOptions = { __proto__: null, concurrency, timeout, signal };
  const globalOptions = {
    __proto__: null,
    // parseCommandLine() should not be used here. However, The existing run()
    // behavior has relied on it, so removing it must be done in a semver major.
    ...parseCommandLine(),
    setup,  // This line can be removed when parseCommandLine() is removed here.
    coverage,
    coverageExcludeGlobs,
    coverageIncludeGlobs,
    lineCoverage: lineCoverage,
    branchCoverage: branchCoverage,
    functionCoverage: functionCoverage,
    cwd,
  };
  const root = createTestTree(rootTestOptions, globalOptions);
  let testFiles = files ?? createTestFileList(globPatterns, cwd);

  if (shard) {
    testFiles = ArrayPrototypeFilter(testFiles, (_, index) => index % shard.total === shard.index - 1);
  }

  let teardown;
  let postRun;
  let filesWatcher;
  let runFiles;
  const opts = {
    __proto__: null,
    root,
    signal,
    inspectPort,
    testNamePatterns,
    testSkipPatterns,
    hasFiles: files != null,
    globPatterns,
    only,
    forceExit,
    cwd,
    isolation,
    argv,
    execArgv,
  };

  if (isolation === 'process') {
    if (process.env.NODE_TEST_CONTEXT !== undefined) {
      process.emitWarning('node:test run() is being called recursively within a test file. skipping running files.');
      root.postRun();
      return root.reporter;
    }

    if (watch) {
      filesWatcher = watchFiles(testFiles, opts);
    } else {
      postRun = () => root.postRun();
      teardown = () => root.harness.teardown();
    }

    runFiles = () => {
      root.harness.bootstrapPromise = null;
      root.harness.buildPromise = null;
      return SafePromiseAllSettledReturnVoid(testFiles, (path) => {
        const subtest = runTestFile(path, filesWatcher, opts);
        filesWatcher?.runningSubtests.set(path, subtest);
        return subtest;
      });
    };
  } else if (isolation === 'none') {
    if (watch) {
      const absoluteTestFiles = ArrayPrototypeMap(testFiles, (file) => (isAbsolute(file) ? file : resolve(cwd, file)));
      filesWatcher = watchFiles(absoluteTestFiles, opts);
      runFiles = async () => {
        root.harness.bootstrapPromise = null;
        root.harness.buildPromise = null;
        const subtest = runTestFile(kIsolatedProcessName, filesWatcher, opts);
        filesWatcher?.runningSubtests.set(kIsolatedProcessName, subtest);
        return subtest;
      };
    } else {
      runFiles = async () => {
        const { promise, resolve: finishBootstrap } = PromiseWithResolvers();

        await root.runInAsyncScope(async () => {
          const parentURL = pathToFileURL(cwd + sep).href;
          const cascadedLoader = esmLoader.getOrInitializeCascadedLoader();
          let topLevelTestCount = 0;

          root.harness.bootstrapPromise = promise;

          const userImports = getOptionValue('--import');
          for (let i = 0; i < userImports.length; i++) {
            await cascadedLoader.import(userImports[i], parentURL, kEmptyObject);
          }

          for (let i = 0; i < testFiles.length; ++i) {
            const testFile = testFiles[i];
            const fileURL = pathToFileURL(resolve(cwd, testFile));
            const parent = i === 0 ? undefined : parentURL;
            let threw = false;
            let importError;

            root.entryFile = resolve(testFile);
            debug('loading test file:', fileURL.href);
            try {
              await cascadedLoader.import(fileURL, parent, { __proto__: null });
            } catch (err) {
              threw = true;
              importError = err;
            }

            debug(
              'loaded "%s": top level test count before = %d and after = %d',
              testFile,
              topLevelTestCount,
              root.subtests.length,
            );
            if (topLevelTestCount === root.subtests.length) {
              // This file had no tests in it. Add the placeholder test.
              const subtest = root.createSubtest(Test, testFile);
              if (threw) {
                subtest.fail(importError);
              }
              startSubtestAfterBootstrap(subtest);
            }

            topLevelTestCount = root.subtests.length;
          }
        });

        debug('beginning test execution');
        root.entryFile = null;
        finishBootstrap();
        root.processPendingSubtests();
      };
    }
  }

  const runChain = async () => {
    if (typeof setup === 'function') {
      await setup(root.reporter);
    }

    await runFiles();
    postRun?.();
    teardown?.();
  };

  runChain();
  return root.reporter;
}

module.exports = {
  FileTest, // Exported for tests only
  run,
};
