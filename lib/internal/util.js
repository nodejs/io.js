'use strict';

const {
  ArrayFrom,
  ArrayPrototypePush,
  ArrayPrototypeSlice,
  ArrayPrototypeSort,
  Error,
  ErrorCaptureStackTrace,
  FunctionPrototypeCall,
  ObjectDefineProperties,
  ObjectDefineProperty,
  ObjectFreeze,
  ObjectGetOwnPropertyDescriptor,
  ObjectGetOwnPropertyDescriptors,
  ObjectGetPrototypeOf,
  ObjectPrototypeHasOwnProperty,
  ObjectSetPrototypeOf,
  ObjectValues,
  Promise,
  ReflectApply,
  ReflectConstruct,
  RegExpPrototypeExec,
  RegExpPrototypeGetDotAll,
  RegExpPrototypeGetGlobal,
  RegExpPrototypeGetHasIndices,
  RegExpPrototypeGetIgnoreCase,
  RegExpPrototypeGetMultiline,
  RegExpPrototypeGetSource,
  RegExpPrototypeGetSticky,
  RegExpPrototypeGetUnicode,
  SafeMap,
  SafeSet,
  SafeWeakMap,
  SafeWeakRef,
  StringPrototypeReplace,
  StringPrototypeToLowerCase,
  StringPrototypeToUpperCase,
  Symbol,
  SymbolFor,
  SymbolReplace,
  SymbolSplit,
} = primordials;

const {
  codes: {
    ERR_NO_CRYPTO,
    ERR_NO_TYPESCRIPT,
    ERR_UNKNOWN_SIGNAL,
  },
  isErrorStackTraceLimitWritable,
  overrideStackTrace,
  uvErrmapGet,
} = require('internal/errors');
const { signals } = internalBinding('constants').os;
const {
  guessHandleType: _guessHandleType,
  privateSymbols: {
    arrow_message_private_symbol,
    decorated_private_symbol,
  },
  sleep: _sleep,
} = internalBinding('util');
const { isNativeError, isPromise } = internalBinding('types');
const { getOptionValue } = require('internal/options');
const assert = require('internal/assert');
const { encodings } = internalBinding('string_decoder');

const noCrypto = !process.versions.openssl;
const noTypeScript = !process.versions.amaro;

const isWindows = process.platform === 'win32';
const isMacOS = process.platform === 'darwin';

const experimentalWarnings = new SafeSet();

const colorRegExp = /\u001b\[\d\d?m/g; // eslint-disable-line no-control-regex

let uvBinding;

function lazyUv() {
  uvBinding ??= internalBinding('uv');
  return uvBinding;
}

function removeColors(str) {
  return StringPrototypeReplace(str, colorRegExp, '');
}

function isError(e) {
  // An error could be an instance of Error while not being a native error
  // or could be from a different realm and not be instance of Error but still
  // be a native error.
  return isNativeError(e) || e instanceof Error;
}

// Keep a list of deprecation codes that have been warned on so we only warn on
// each one once.
const codesWarned = new SafeSet();

let validateString;

function getDeprecationWarningEmitter(
  code, msg, deprecated, useEmitSync,
  shouldEmitWarning = () => true,
) {
  let warned = false;
  return function() {
    if (!warned && shouldEmitWarning()) {
      warned = true;
      if (code === 'ExperimentalWarning') {
        process.emitWarning(msg, code, deprecated);
      } else if (code !== undefined) {
        if (!codesWarned.has(code)) {
          const emitWarning = useEmitSync ?
            require('internal/process/warning').emitWarningSync :
            process.emitWarning;
          emitWarning(msg, 'DeprecationWarning', code, deprecated);
          codesWarned.add(code);
        }
      } else {
        process.emitWarning(msg, 'DeprecationWarning', deprecated);
      }
    }
  };
}

function isPendingDeprecation() {
  return getOptionValue('--pending-deprecation') &&
    !getOptionValue('--no-deprecation');
}

// Internal deprecator for pending --pending-deprecation. This can be invoked
// at snapshot building time as the warning permission is only queried at
// run time.
function pendingDeprecate(fn, msg, code) {
  const emitDeprecationWarning = getDeprecationWarningEmitter(
    code, msg, deprecated, false, isPendingDeprecation,
  );
  function deprecated(...args) {
    emitDeprecationWarning();
    return ReflectApply(fn, this, args);
  }
  return deprecated;
}

// Mark that a method should not be used.
// Returns a modified function which warns once by default.
// If --no-deprecation is set, then it is a no-op.
function deprecate(fn, msg, code, useEmitSync) {
  // Lazy-load to avoid a circular dependency.
  if (validateString === undefined)
    ({ validateString } = require('internal/validators'));

  if (code !== undefined)
    validateString(code, 'code');

  const emitDeprecationWarning = getDeprecationWarningEmitter(
    code, msg, deprecated, useEmitSync,
  );

  function deprecated(...args) {
    // TODO(joyeecheung): use getOptionValue('--no-deprecation') instead.
    if (!process.noDeprecation) {
      emitDeprecationWarning();
    }
    if (new.target) {
      return ReflectConstruct(fn, args, new.target);
    }
    return ReflectApply(fn, this, args);
  }

  // The wrapper will keep the same prototype as fn to maintain prototype chain
  ObjectSetPrototypeOf(deprecated, fn);
  if (fn.prototype) {
    // Setting this (rather than using Object.setPrototype, as above) ensures
    // that calling the unwrapped constructor gives an instanceof the wrapped
    // constructor.
    deprecated.prototype = fn.prototype;
  }

  return deprecated;
}

function deprecateInstantiation(target, code, ...args) {
  assert(typeof code === 'string');

  getDeprecationWarningEmitter(code, `Instantiating ${target.name} without the 'new' keyword has been deprecated.`, target)();

  return ReflectConstruct(target, args);
}

function decorateErrorStack(err) {
  if (!(isError(err) && err.stack) || err[decorated_private_symbol])
    return;

  const arrow = err[arrow_message_private_symbol];

  if (arrow) {
    err.stack = arrow + err.stack;
    err[decorated_private_symbol] = true;
  }
}

function assertCrypto() {
  if (noCrypto)
    throw new ERR_NO_CRYPTO();
}

function assertTypeScript() {
  if (noTypeScript)
    throw new ERR_NO_TYPESCRIPT();
}

/**
 * Move the "slow cases" to a separate function to make sure this function gets
 * inlined properly. That prioritizes the common case.
 * @param {unknown} enc
 * @returns {string | undefined} Returns undefined if there is no match.
 */
function normalizeEncoding(enc) {
  if (enc == null || enc === 'utf8' || enc === 'utf-8') return 'utf8';
  return slowCases(enc);
}

function slowCases(enc) {
  switch (enc.length) {
    case 4:
      if (enc === 'UTF8') return 'utf8';
      if (enc === 'ucs2' || enc === 'UCS2') return 'utf16le';
      enc = StringPrototypeToLowerCase(enc);
      if (enc === 'utf8') return 'utf8';
      if (enc === 'ucs2') return 'utf16le';
      break;
    case 3:
      if (enc === 'hex' || enc === 'HEX' ||
      StringPrototypeToLowerCase(enc) === 'hex')
        return 'hex';
      break;
    case 5:
      if (enc === 'ascii') return 'ascii';
      if (enc === 'ucs-2') return 'utf16le';
      if (enc === 'UTF-8') return 'utf8';
      if (enc === 'ASCII') return 'ascii';
      if (enc === 'UCS-2') return 'utf16le';
      enc = StringPrototypeToLowerCase(enc);
      if (enc === 'utf-8') return 'utf8';
      if (enc === 'ascii') return 'ascii';
      if (enc === 'ucs-2') return 'utf16le';
      break;
    case 6:
      if (enc === 'base64') return 'base64';
      if (enc === 'latin1' || enc === 'binary') return 'latin1';
      if (enc === 'BASE64') return 'base64';
      if (enc === 'LATIN1' || enc === 'BINARY') return 'latin1';
      enc = StringPrototypeToLowerCase(enc);
      if (enc === 'base64') return 'base64';
      if (enc === 'latin1' || enc === 'binary') return 'latin1';
      break;
    case 7:
      if (enc === 'utf16le' || enc === 'UTF16LE' ||
      StringPrototypeToLowerCase(enc) === 'utf16le')
        return 'utf16le';
      break;
    case 8:
      if (enc === 'utf-16le' || enc === 'UTF-16LE' ||
      StringPrototypeToLowerCase(enc) === 'utf-16le')
        return 'utf16le';
      break;
    case 9:
      if (enc === 'base64url' || enc === 'BASE64URL' ||
      StringPrototypeToLowerCase(enc) === 'base64url')
        return 'base64url';
      break;
    default:
      if (enc === '') return 'utf8';
  }
}

/**
 * @param {string} feature Feature name used in the warning message
 * @param {string} messagePrefix Prefix of the warning message
 * @param {string} code See documentation of process.emitWarning
 * @param {string} ctor See documentation of process.emitWarning
 */
function emitExperimentalWarning(feature, messagePrefix, code, ctor) {
  if (experimentalWarnings.has(feature)) return;
  experimentalWarnings.add(feature);
  let msg = `${feature} is an experimental feature and might change at any time`;
  if (messagePrefix) {
    msg = messagePrefix + msg;
  }
  process.emitWarning(msg, 'ExperimentalWarning', code, ctor);
}

function filterDuplicateStrings(items, low) {
  const map = new SafeMap();
  for (let i = 0; i < items.length; i++) {
    const item = items[i];
    const key = StringPrototypeToLowerCase(item);
    if (low) {
      map.set(key, key);
    } else {
      map.set(key, item);
    }
  }
  return ArrayPrototypeSort(ArrayFrom(map.values()));
}

function cachedResult(fn) {
  let result;
  return () => {
    if (result === undefined)
      result = fn();
    return ArrayPrototypeSlice(result);
  };
}

// Useful for Wrapping an ES6 Class with a constructor Function that
// does not require the new keyword. For instance:
//   class A { constructor(x) {this.x = x;}}
//   const B = createClassWrapper(A);
//   B() instanceof A // true
//   B() instanceof B // true
function createClassWrapper(type) {
  function fn(...args) {
    return ReflectConstruct(type, args, new.target || type);
  }
  // Mask the wrapper function name and length values
  ObjectDefineProperties(fn, {
    name: { __proto__: null, value: type.name },
    length: { __proto__: null, value: type.length },
  });
  ObjectSetPrototypeOf(fn, type);
  fn.prototype = type.prototype;
  return fn;
}

let signalsToNamesMapping;
function getSignalsToNamesMapping() {
  if (signalsToNamesMapping !== undefined)
    return signalsToNamesMapping;

  signalsToNamesMapping = { __proto__: null };
  for (const key in signals) {
    signalsToNamesMapping[signals[key]] = key;
  }

  return signalsToNamesMapping;
}

function convertToValidSignal(signal) {
  if (typeof signal === 'number' && getSignalsToNamesMapping()[signal])
    return signal;

  if (typeof signal === 'string') {
    const signalName = signals[StringPrototypeToUpperCase(signal)];
    if (signalName) return signalName;
  }

  throw new ERR_UNKNOWN_SIGNAL(signal);
}

function getConstructorOf(obj) {
  while (obj) {
    const descriptor = ObjectGetOwnPropertyDescriptor(obj, 'constructor');
    if (descriptor !== undefined &&
        typeof descriptor.value === 'function' &&
        descriptor.value.name !== '') {
      return descriptor.value;
    }

    obj = ObjectGetPrototypeOf(obj);
  }

  return null;
}

let cachedURL;
let cachedCWD;

/**
 * Get the current working directory while accounting for the possibility that it has been deleted.
 * `process.cwd()` can fail if the parent directory is deleted while the process runs.
 * @returns {URL} The current working directory or the volume root if it cannot be determined.
 */
function getCWDURL() {
  const { sep } = require('path');
  const { pathToFileURL } = require('internal/url');

  let cwd;

  try {
    // The implementation of `process.cwd()` already uses proper cache when it can.
    // It's a relatively cheap call performance-wise for the most common use case.
    cwd = process.cwd();
  } catch {
    cachedURL ??= pathToFileURL(sep);
  }

  if (cwd != null && cwd !== cachedCWD) {
    cachedURL = pathToFileURL(cwd + sep);
    cachedCWD = cwd;
  }

  return cachedURL;
}

function getSystemErrorMessage(err) {
  return lazyUv().getErrorMessage(err);
}

function getSystemErrorName(err) {
  const entry = uvErrmapGet(err);
  return entry ? entry[0] : `Unknown system error ${err}`;
}

function getSystemErrorMap() {
  return lazyUv().getErrorMap();
}

const kCustomPromisifiedSymbol = SymbolFor('nodejs.util.promisify.custom');
const kCustomPromisifyArgsSymbol = Symbol('customPromisifyArgs');

let validateFunction;

function promisify(original) {
  // Lazy-load to avoid a circular dependency.
  if (validateFunction === undefined)
    ({ validateFunction } = require('internal/validators'));

  validateFunction(original, 'original');

  if (original[kCustomPromisifiedSymbol]) {
    const fn = original[kCustomPromisifiedSymbol];

    validateFunction(fn, 'util.promisify.custom');

    return ObjectDefineProperty(fn, kCustomPromisifiedSymbol, {
      __proto__: null,
      value: fn, enumerable: false, writable: false, configurable: true,
    });
  }

  // Names to create an object from in case the callback receives multiple
  // arguments, e.g. ['bytesRead', 'buffer'] for fs.read.
  const argumentNames = original[kCustomPromisifyArgsSymbol];

  function fn(...args) {
    return new Promise((resolve, reject) => {
      ArrayPrototypePush(args, (err, ...values) => {
        if (err) {
          return reject(err);
        }
        if (argumentNames !== undefined && values.length > 1) {
          const obj = {};
          for (let i = 0; i < argumentNames.length; i++)
            obj[argumentNames[i]] = values[i];
          resolve(obj);
        } else {
          resolve(values[0]);
        }
      });
      if (isPromise(ReflectApply(original, this, args))) {
        process.emitWarning('Calling promisify on a function that returns a Promise is likely a mistake.',
                            'DeprecationWarning', 'DEP0174');
      }
    });
  }

  ObjectSetPrototypeOf(fn, ObjectGetPrototypeOf(original));

  ObjectDefineProperty(fn, kCustomPromisifiedSymbol, {
    __proto__: null,
    value: fn, enumerable: false, writable: false, configurable: true,
  });

  const descriptors = ObjectGetOwnPropertyDescriptors(original);
  const propertiesValues = ObjectValues(descriptors);
  for (let i = 0; i < propertiesValues.length; i++) {
    // We want to use null-prototype objects to not rely on globally mutable
    // %Object.prototype%.
    ObjectSetPrototypeOf(propertiesValues[i], null);
  }
  return ObjectDefineProperties(fn, descriptors);
}

promisify.custom = kCustomPromisifiedSymbol;

// The built-in Array#join is slower in v8 6.0
function join(output, separator) {
  let str = '';
  if (output.length !== 0) {
    const lastIndex = output.length - 1;
    for (let i = 0; i < lastIndex; i++) {
      // It is faster not to use a template string here
      str += output[i];
      str += separator;
    }
    str += output[lastIndex];
  }
  return str;
}

// As of V8 6.6, depending on the size of the array, this is anywhere
// between 1.5-10x faster than the two-arg version of Array#splice()
function spliceOne(list, index) {
  for (; index + 1 < list.length; index++)
    list[index] = list[index + 1];
  list.pop();
}

const kNodeModulesRE = /^(?:.*)[\\/]node_modules[\\/]/;

function isUnderNodeModules(filename) {
  return filename && (RegExpPrototypeExec(kNodeModulesRE, filename) !== null);
}

let getStructuredStackImpl;

function lazyGetStructuredStack() {
  if (getStructuredStackImpl === undefined) {
    // Lazy-load to avoid a circular dependency.
    const { runInNewContext } = require('vm');
    // Use `runInNewContext()` to get something tamper-proof and
    // side-effect-free. Since this is currently only used for a deprecated API
    // and module mocking, the perf implications should be okay.
    getStructuredStackImpl = runInNewContext(`(function() {
      try { Error.stackTraceLimit = Infinity; } catch {}
      return function structuredStack() {
        const e = new Error();
        overrideStackTrace.set(e, (err, trace) => trace);
        return e.stack;
      };
    })()`, { overrideStackTrace }, { filename: 'structured-stack' });
  }

  return getStructuredStackImpl;
}

function getStructuredStack() {
  const getStructuredStackImpl = lazyGetStructuredStack();

  return getStructuredStackImpl();
}

function once(callback, { preserveReturnValue = false } = kEmptyObject) {
  let called = false;
  let returnValue;
  return function(...args) {
    if (called) return returnValue;
    called = true;
    const result = ReflectApply(callback, this, args);
    returnValue = preserveReturnValue ? result : undefined;
    return result;
  };
}

let validateUint32;

function sleep(msec) {
  // Lazy-load to avoid a circular dependency.
  if (validateUint32 === undefined)
    ({ validateUint32 } = require('internal/validators'));

  validateUint32(msec, 'msec');
  _sleep(msec);
}

// https://heycam.github.io/webidl/#define-the-operations
function defineOperation(target, name, method) {
  ObjectDefineProperty(target, name, {
    __proto__: null,
    writable: true,
    enumerable: true,
    configurable: true,
    value: method,
  });
}

// https://heycam.github.io/webidl/#es-interfaces
function exposeInterface(target, name, interfaceObject) {
  ObjectDefineProperty(target, name, {
    __proto__: null,
    writable: true,
    enumerable: false,
    configurable: true,
    value: interfaceObject,
  });
}

// https://heycam.github.io/webidl/#es-namespaces
function exposeNamespace(target, name, namespaceObject) {
  ObjectDefineProperty(target, name, {
    __proto__: null,
    writable: true,
    enumerable: false,
    configurable: true,
    value: namespaceObject,
  });
}

function exposeGetterAndSetter(target, name, getter, setter = undefined) {
  ObjectDefineProperty(target, name, {
    __proto__: null,
    enumerable: false,
    configurable: true,
    get: getter,
    set: setter,
  });
}

function defineLazyProperties(target, id, keys, enumerable = true) {
  const descriptors = { __proto__: null };
  let mod;
  for (let i = 0; i < keys.length; i++) {
    const key = keys[i];
    let lazyLoadedValue;
    function set(value) {
      ObjectDefineProperty(target, key, {
        __proto__: null,
        writable: true,
        value,
      });
    }
    ObjectDefineProperty(set, 'name', {
      __proto__: null,
      value: `set ${key}`,
    });
    function get() {
      mod ??= require(id);
      if (lazyLoadedValue === undefined) {
        lazyLoadedValue = mod[key];
        set(lazyLoadedValue);
      }
      return lazyLoadedValue;
    }
    ObjectDefineProperty(get, 'name', {
      __proto__: null,
      value: `get ${key}`,
    });
    descriptors[key] = {
      __proto__: null,
      configurable: true,
      enumerable,
      get,
      set,
    };
  }
  ObjectDefineProperties(target, descriptors);
}

function defineReplaceableLazyAttribute(target, id, keys, writable = true, check) {
  let mod;
  for (let i = 0; i < keys.length; i++) {
    const key = keys[i];
    let value;
    let setterCalled = false;

    function get() {
      if (check !== undefined) {
        FunctionPrototypeCall(check, this);
      }
      if (setterCalled) {
        return value;
      }
      mod ??= require(id);
      value ??= mod[key];
      return value;
    }

    ObjectDefineProperty(get, 'name', {
      __proto__: null,
      value: `get ${key}`,
    });

    function set(val) {
      setterCalled = true;
      value = val;
    }
    ObjectDefineProperty(set, 'name', {
      __proto__: null,
      value: `set ${key}`,
    });

    ObjectDefineProperty(target, key, {
      __proto__: null,
      enumerable: true,
      configurable: true,
      get,
      set: writable ? set : undefined,
    });
  }
}

function exposeLazyInterfaces(target, id, keys) {
  defineLazyProperties(target, id, keys, false);
}

let _DOMException;
const lazyDOMExceptionClass = () => {
  _DOMException ??= internalBinding('messaging').DOMException;
  return _DOMException;
};

const lazyDOMException = (message, name) => {
  _DOMException ??= internalBinding('messaging').DOMException;
  if (isErrorStackTraceLimitWritable()) {
    const limit = Error.stackTraceLimit;
    Error.stackTraceLimit = 0;
    const ex = new _DOMException(message, name);
    Error.stackTraceLimit = limit;
    ErrorCaptureStackTrace(ex, lazyDOMException);
    return ex;
  }
  return new _DOMException(message, name);

};

const kEnumerableProperty = { __proto__: null };
kEnumerableProperty.enumerable = true;
ObjectFreeze(kEnumerableProperty);

const kEmptyObject = ObjectFreeze({ __proto__: null });

function filterOwnProperties(source, keys) {
  const filtered = { __proto__: null };
  for (let i = 0; i < keys.length; i++) {
    const key = keys[i];
    if (ObjectPrototypeHasOwnProperty(source, key)) {
      filtered[key] = source[key];
    }
  }

  return filtered;
}

/**
 * Mimics `obj[key] = value` but ignoring potential prototype inheritance.
 * @param {any} obj
 * @param {string} key
 * @param {any} value
 * @returns {any}
 */
function setOwnProperty(obj, key, value) {
  return ObjectDefineProperty(obj, key, {
    __proto__: null,
    configurable: true,
    enumerable: true,
    value,
    writable: true,
  });
}

let internalGlobal;
function getInternalGlobal() {
  if (internalGlobal == null) {
    // Lazy-load to avoid a circular dependency.
    const { runInNewContext } = require('vm');
    internalGlobal = runInNewContext('this', undefined, { contextName: 'internal' });
  }
  return internalGlobal;
}

function SideEffectFreeRegExpPrototypeExec(regex, string) {
  const { RegExp: RegExpFromAnotherRealm } = getInternalGlobal();
  return FunctionPrototypeCall(RegExpFromAnotherRealm.prototype.exec, regex, string);
}

const crossRealmRegexes = new SafeWeakMap();
function getCrossRealmRegex(regex) {
  const cached = crossRealmRegexes.get(regex);
  if (cached) return cached;

  let flagString = '';
  if (RegExpPrototypeGetHasIndices(regex)) flagString += 'd';
  if (RegExpPrototypeGetGlobal(regex)) flagString += 'g';
  if (RegExpPrototypeGetIgnoreCase(regex)) flagString += 'i';
  if (RegExpPrototypeGetMultiline(regex)) flagString += 'm';
  if (RegExpPrototypeGetDotAll(regex)) flagString += 's';
  if (RegExpPrototypeGetUnicode(regex)) flagString += 'u';
  if (RegExpPrototypeGetSticky(regex)) flagString += 'y';

  const { RegExp: RegExpFromAnotherRealm } = getInternalGlobal();
  const crossRealmRegex = new RegExpFromAnotherRealm(RegExpPrototypeGetSource(regex), flagString);
  crossRealmRegexes.set(regex, crossRealmRegex);
  return crossRealmRegex;
}

function SideEffectFreeRegExpPrototypeSymbolReplace(regex, string, replacement) {
  return getCrossRealmRegex(regex)[SymbolReplace](string, replacement);
}

function SideEffectFreeRegExpPrototypeSymbolSplit(regex, string, limit = undefined) {
  return getCrossRealmRegex(regex)[SymbolSplit](string, limit);
}

/**
 * Helper function to lazy-load an initialize-once value.
 * @template T Return value of initializer
 * @param {()=>T} initializer Initializer of the lazily loaded value.
 * @returns {()=>T}
 */
function getLazy(initializer) {
  let value;
  let initialized = false;
  return function() {
    if (initialized === false) {
      value = initializer();
      initialized = true;
    }
    return value;
  };
}

// Setup user-facing NODE_V8_COVERAGE environment variable that writes
// ScriptCoverage objects to a specified directory.
function setupCoverageHooks(dir) {
  const cwd = require('internal/process/execution').tryGetCwd();
  const { resolve } = require('path');
  const coverageDirectory = resolve(cwd, dir);
  const { sourceMapCacheToObject } =
    require('internal/source_map/source_map_cache');

  if (process.features.inspector) {
    internalBinding('profiler').setCoverageDirectory(coverageDirectory);
    internalBinding('profiler').setSourceMapCacheGetter(sourceMapCacheToObject);
  } else {
    process.emitWarning('The inspector is disabled, ' +
                        'coverage could not be collected',
                        'Warning');
    return '';
  }
  return coverageDirectory;
}


const handleTypes = ['TCP', 'TTY', 'UDP', 'FILE', 'PIPE', 'UNKNOWN'];
function guessHandleType(fd) {
  const type = _guessHandleType(fd);
  return handleTypes[type];
}

class WeakReference {
  #weak = null;
  // eslint-disable-next-line no-unused-private-class-members
  #strong = null;
  #refCount = 0;
  constructor(object) {
    this.#weak = new SafeWeakRef(object);
  }

  incRef() {
    this.#refCount++;
    if (this.#refCount === 1) {
      const derefed = this.#weak.deref();
      if (derefed !== undefined) {
        this.#strong = derefed;
      }
    }
    return this.#refCount;
  }

  decRef() {
    this.#refCount--;
    if (this.#refCount === 0) {
      this.#strong = null;
    }
    return this.#refCount;
  }

  get() {
    return this.#weak.deref();
  }
}

const encodingsMap = { __proto__: null };
for (let i = 0; i < encodings.length; ++i)
  encodingsMap[encodings[i]] = i;

module.exports = {
  getLazy,
  assertCrypto,
  assertTypeScript,
  cachedResult,
  convertToValidSignal,
  createClassWrapper,
  decorateErrorStack,
  defineOperation,
  defineLazyProperties,
  defineReplaceableLazyAttribute,
  deprecate,
  deprecateInstantiation,
  emitExperimentalWarning,
  encodingsMap,
  exposeInterface,
  exposeLazyInterfaces,
  exposeNamespace,
  exposeGetterAndSetter,
  filterDuplicateStrings,
  filterOwnProperties,
  getConstructorOf,
  getCWDURL,
  getInternalGlobal,
  getStructuredStack,
  getSystemErrorMap,
  getSystemErrorName,
  getSystemErrorMessage,
  guessHandleType,
  isError,
  isUnderNodeModules,
  isMacOS,
  isWindows,
  join,
  lazyDOMException,
  lazyDOMExceptionClass,
  normalizeEncoding,
  once,
  promisify,
  SideEffectFreeRegExpPrototypeExec,
  SideEffectFreeRegExpPrototypeSymbolReplace,
  SideEffectFreeRegExpPrototypeSymbolSplit,
  sleep,
  spliceOne,
  setupCoverageHooks,
  removeColors,

  // Symbol used to customize promisify conversion
  customPromisifyArgs: kCustomPromisifyArgsSymbol,

  // Symbol used to provide a custom inspect function for an object as an
  // alternative to using 'inspect'
  customInspectSymbol: SymbolFor('nodejs.util.inspect.custom'),

  // Used by the buffer module to capture an internal reference to the
  // default isEncoding implementation, just in case userland overrides it.
  kIsEncodingSymbol: Symbol('kIsEncodingSymbol'),
  kVmBreakFirstLineSymbol: Symbol('kVmBreakFirstLineSymbol'),

  kEmptyObject,
  kEnumerableProperty,
  setOwnProperty,
  pendingDeprecate,
  WeakReference,
};
