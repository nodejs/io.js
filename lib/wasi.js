'use strict';
const {
  ArrayPrototypeForEach,
  ArrayPrototypeMap,
  ArrayPrototypePush,
  FunctionPrototypeBind,
  ObjectEntries,
  String,
  Symbol,
} = primordials;
const {
  WebAssembly,
} = globalThis;

const {
  ERR_INVALID_ARG_VALUE,
  ERR_WASI_ALREADY_STARTED,
  ERR_INVALID_ARG_TYPE,
} = require('internal/errors').codes;
const {
  emitExperimentalWarning,
  kEmptyObject,
} = require('internal/util');
const {
  validateArray,
  validateBoolean,
  validateFunction,
  validateInt32,
  validateObject,
  validateString,
  validateUndefined,
} = require('internal/validators');
const kExitCode = Symbol('kExitCode');
const kSetMemory = Symbol('kSetMemory');
const kStarted = Symbol('kStarted');
const kInstance = Symbol('kInstance');
const kBindingName = Symbol('kBindingName');

emitExperimentalWarning('WASI');

class WASI {
  constructor(options = kEmptyObject) {
    validateObject(options, 'options');

    let _WASI;
    validateString(options.version, 'options.version');
    switch (options.version) {
      case 'unstable':
        ({ WASI: _WASI } = internalBinding('wasi'));
        this[kBindingName] = 'wasi_unstable';
        break;
      case 'preview1':
        ({ WASI: _WASI } = internalBinding('wasi'));
        this[kBindingName] = 'wasi_snapshot_preview1';
        break;
      // When adding support for additional wasi versions add case here
      default:
        throw new ERR_INVALID_ARG_VALUE('options.version',
                                        options.version,
                                        'unsupported WASI version');
    }

    if (options.args !== undefined)
      validateArray(options.args, 'options.args');
    const args = ArrayPrototypeMap(options.args || [], String);

    const env = [];
    if (options.env !== undefined) {
      validateObject(options.env, 'options.env');
      ArrayPrototypeForEach(
        ObjectEntries(options.env),
        ({ 0: key, 1: value }) => {
          if (value !== undefined)
            ArrayPrototypePush(env, `${key}=${value}`);
        });
    }

    const preopens = [];
    if (options.preopens !== undefined) {
      validateObject(options.preopens, 'options.preopens');
      ArrayPrototypeForEach(
        ObjectEntries(options.preopens),
        ({ 0: key, 1: value }) =>
          ArrayPrototypePush(preopens, String(key), String(value)),
      );
    }

    const { stdin = 0, stdout = 1, stderr = 2 } = options;
    validateInt32(stdin, 'options.stdin', 0);
    validateInt32(stdout, 'options.stdout', 0);
    validateInt32(stderr, 'options.stderr', 0);
    const stdio = [stdin, stdout, stderr];

    const wrap = new _WASI(args, env, preopens, stdio);

    for (const prop in wrap) {
      wrap[prop] = FunctionPrototypeBind(wrap[prop], wrap);
    }

    let returnOnExit = true;
    if (options.returnOnExit !== undefined) {
      validateBoolean(options.returnOnExit, 'options.returnOnExit');
      returnOnExit = options.returnOnExit;
    }
    if (returnOnExit)
      wrap.proc_exit = FunctionPrototypeBind(wasiReturnOnProcExit, this);

    this[kSetMemory] = wrap._setMemory;
    delete wrap._setMemory;
    this.wasiImport = wrap;
    this[kStarted] = false;
    this[kExitCode] = 0;
    this[kInstance] = undefined;
  }

  setupInstance(instance, memory) {
    validateObject(instance, 'instance');
    validateObject(instance.exports, 'instance.exports');

    if (memory != null) {
      if (!(memory instanceof WebAssembly.Memory)) {
        throw new ERR_INVALID_ARG_TYPE('memory', 'WebAssembly.Memory', memory);
      }
      this[kSetMemory](memory);
    } else {
      this[kSetMemory](instance.exports.memory);
    }

    this[kInstance] = instance;
    this[kStarted] = true;
  }

  // Must not export _initialize, must export _start
  start(instance) {
    if (this[kStarted]) {
      throw new ERR_WASI_ALREADY_STARTED();
    }

    this.setupInstance(instance);

    const { _start, _initialize } = this[kInstance].exports;

    validateFunction(_start, 'instance.exports._start');
    validateUndefined(_initialize, 'instance.exports._initialize');

    try {
      _start();
    } catch (err) {
      if (err !== kExitCode) {
        throw err;
      }
    }

    return this[kExitCode];
  }

  // Must not export _start, may optionally export _initialize
  initialize(instance) {
    if (this[kStarted]) {
      throw new ERR_WASI_ALREADY_STARTED();
    }

    this.setupInstance(instance);

    const { _start, _initialize } = this[kInstance].exports;

    validateUndefined(_start, 'instance.exports._start');
    if (_initialize !== undefined) {
      validateFunction(_initialize, 'instance.exports._initialize');
      _initialize();
    }
  }

  getImportObject() {
    return { [this[kBindingName]]: this.wasiImport };
  }
}

module.exports = { WASI };


function wasiReturnOnProcExit(rval) {
  // If __wasi_proc_exit() does not terminate the process, an assertion is
  // triggered in the wasm runtime. Node can sidestep the assertion and return
  // an exit code by recording the exit code, and throwing a JavaScript
  // exception that WebAssembly cannot catch.
  this[kExitCode] = rval;
  throw kExitCode;
}
