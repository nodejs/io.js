'use strict';

const {
  Array,
  ArrayIsArray,
  ArrayPrototypeJoin,
  ArrayPrototypeMap,
  ArrayPrototypePush,
  ArrayPrototypeReduce,
  ArrayPrototypeSlice,
  Boolean,
  Int8Array,
  IteratorPrototype,
  Number,
  ObjectDefineProperties,
  ObjectSetPrototypeOf,
  ReflectGetOwnPropertyDescriptor,
  ReflectOwnKeys,
  SafeMap,
  SafeSet,
  StringPrototypeCharAt,
  StringPrototypeCharCodeAt,
  StringPrototypeCodePointAt,
  StringPrototypeIncludes,
  StringPrototypeIndexOf,
  StringPrototypeSlice,
  StringPrototypeStartsWith,
  StringPrototypeToWellFormed,
  Symbol,
  SymbolIterator,
  SymbolToStringTag,
  decodeURIComponent,
} = primordials;

const { inspect } = require('internal/util/inspect');
const {
  encodeStr,
  hexTable,
  isHexTable,
} = require('internal/querystring');

const {
  getConstructorOf,
  removeColors,
  kEnumerableProperty,
  kEmptyObject,
  SideEffectFreeRegExpPrototypeSymbolReplace,
  isWindows,
} = require('internal/util');

const {
  platform,
} = require('internal/process/per_thread');

const {
  markTransferMode,
} = require('internal/worker/js_transferable');

const {
  codes: {
    ERR_ARG_NOT_ITERABLE,
    ERR_INVALID_ARG_TYPE,
    ERR_INVALID_ARG_VALUE,
    ERR_INVALID_FILE_URL_HOST,
    ERR_INVALID_FILE_URL_PATH,
    ERR_INVALID_THIS,
    ERR_INVALID_TUPLE,
    ERR_INVALID_URL,
    ERR_INVALID_URL_SCHEME,
    ERR_MISSING_ARGS,
    ERR_NO_CRYPTO,
  },
} = require('internal/errors');
const {
  CHAR_AMPERSAND,
  CHAR_BACKWARD_SLASH,
  CHAR_EQUAL,
  CHAR_FORWARD_SLASH,
  CHAR_LOWERCASE_A,
  CHAR_LOWERCASE_Z,
  CHAR_PERCENT,
  CHAR_PLUS,
} = require('internal/constants');
const path = require('path');

const {
  validateFunction,
} = require('internal/validators');

const querystring = require('querystring');

const bindingUrl = internalBinding('url');

const FORWARD_SLASH = /\//g;

const contextForInspect = Symbol('context');

// `unsafeProtocol`, `hostlessProtocol` and `slashedProtocol` is
// deliberately moved to `internal/url` rather than `url`.
// Workers does not bootstrap URL module. Therefore, `SafeSet`
// is not initialized on bootstrap. This case breaks the
// test-require-delete-array-iterator test.

// Protocols that can allow "unsafe" and "unwise" chars.
const unsafeProtocol = new SafeSet([
  'javascript',
  'javascript:',
]);
// Protocols that never have a hostname.
const hostlessProtocol = new SafeSet([
  'javascript',
  'javascript:',
]);
// Protocols that always contain a // bit.
const slashedProtocol = new SafeSet([
  'http',
  'http:',
  'https',
  'https:',
  'ftp',
  'ftp:',
  'gopher',
  'gopher:',
  'file',
  'file:',
  'ws',
  'ws:',
  'wss',
  'wss:',
]);

const updateActions = {
  kProtocol: 0,
  kHost: 1,
  kHostname: 2,
  kPort: 3,
  kUsername: 4,
  kPassword: 5,
  kPathname: 6,
  kSearch: 7,
  kHash: 8,
  kHref: 9,
};
let blob;
let cryptoRandom;

function lazyBlob() {
  blob ??= require('internal/blob');
  return blob;
}

function lazyCryptoRandom() {
  try {
    cryptoRandom ??= require('internal/crypto/random');
  } catch {
    // If Node.js built without crypto support, we'll fall
    // through here and handle it later.
  }
  return cryptoRandom;
}

// This class provides the internal state of a URL object. An instance of this
// class is stored in every URL object and is accessed internally by setters
// and getters. It roughly corresponds to the concept of a URL record in the
// URL Standard, with a few differences. It is also the object transported to
// the C++ binding.
// Refs: https://url.spec.whatwg.org/#concept-url
class URLContext {
  // This is the maximum value uint32_t can get.
  // Ada uses uint32_t(-1) for declaring omitted values.
  static #omitted = 4294967295;

  href = '';
  protocol_end = 0;
  username_end = 0;
  host_start = 0;
  host_end = 0;
  pathname_start = 0;
  search_start = 0;
  hash_start = 0;
  port = 0;
  /**
   * Refers to `ada::scheme::type`
   *
   * enum type : uint8_t {
   *   HTTP = 0,
   *   NOT_SPECIAL = 1,
   *   HTTPS = 2,
   *   WS = 3,
   *   FTP = 4,
   *   WSS = 5,
   *   FILE = 6
   * };
   * @type {number}
   */
  scheme_type = 1;

  get hasPort() {
    return this.port !== URLContext.#omitted;
  }

  get hasSearch() {
    return this.search_start !== URLContext.#omitted;
  }

  get hasHash() {
    return this.hash_start !== URLContext.#omitted;
  }
}

let setURLSearchParamsModified;
let setURLSearchParamsContext;
let getURLSearchParamsList;
let setURLSearchParams;

class URLSearchParamsIterator {
  #target;
  #kind;
  #index;

  // https://heycam.github.io/webidl/#dfn-default-iterator-object
  constructor(target, kind) {
    this.#target = target;
    this.#kind = kind;
    this.#index = 0;
  }

  next() {
    if (typeof this !== 'object' || this === null || !(#target in this))
      throw new ERR_INVALID_THIS('URLSearchParamsIterator');

    const index = this.#index;
    const values = getURLSearchParamsList(this.#target);
    const len = values.length;
    if (index >= len) {
      return {
        value: undefined,
        done: true,
      };
    }

    const name = values[index];
    const value = values[index + 1];
    this.#index = index + 2;

    let result;
    if (this.#kind === 'key') {
      result = name;
    } else if (this.#kind === 'value') {
      result = value;
    } else {
      result = [name, value];
    }

    return {
      value: result,
      done: false,
    };
  }

  [inspect.custom](recurseTimes, ctx) {
    if (!this || typeof this !== 'object' || !(#target in this))
      throw new ERR_INVALID_THIS('URLSearchParamsIterator');

    if (typeof recurseTimes === 'number' && recurseTimes < 0)
      return ctx.stylize('[Object]', 'special');

    const innerOpts = { ...ctx };
    if (recurseTimes !== null) {
      innerOpts.depth = recurseTimes - 1;
    }
    const index = this.#index;
    const values = getURLSearchParamsList(this.#target);
    const output = ArrayPrototypeReduce(
      ArrayPrototypeSlice(values, index),
      (prev, cur, i) => {
        const key = i % 2 === 0;
        if (this.#kind === 'key' && key) {
          ArrayPrototypePush(prev, cur);
        } else if (this.#kind === 'value' && !key) {
          ArrayPrototypePush(prev, cur);
        } else if (this.#kind === 'key+value' && !key) {
          ArrayPrototypePush(prev, [values[index + i - 1], cur]);
        }
        return prev;
      },
      [],
    );
    const breakLn = StringPrototypeIncludes(inspect(output, innerOpts), '\n');
    const outputStrs = ArrayPrototypeMap(output, (p) => inspect(p, innerOpts));
    let outputStr;
    if (breakLn) {
      outputStr = `\n  ${ArrayPrototypeJoin(outputStrs, ',\n  ')}`;
    } else {
      outputStr = ` ${ArrayPrototypeJoin(outputStrs, ', ')}`;
    }
    return `${this[SymbolToStringTag]} {${outputStr} }`;
  }
}

// https://heycam.github.io/webidl/#dfn-iterator-prototype-object
delete URLSearchParamsIterator.prototype.constructor;
ObjectSetPrototypeOf(URLSearchParamsIterator.prototype, IteratorPrototype);

ObjectDefineProperties(URLSearchParamsIterator.prototype, {
  [SymbolToStringTag]: { __proto__: null, configurable: true, value: 'URLSearchParams Iterator' },
  next: kEnumerableProperty,
});


class URLSearchParams {
  #searchParams = [];

  // "associated url object"
  #context;

  static {
    setURLSearchParamsContext = (obj, ctx) => {
      obj.#context = ctx;
    };
    getURLSearchParamsList = (obj) => obj.#searchParams;
    setURLSearchParams = (obj, query) => {
      if (query === undefined) {
        obj.#searchParams = [];
      } else {
        obj.#searchParams = parseParams(query);
      }
    };
  }

  // URL Standard says the default value is '', but as undefined and '' have
  // the same result, undefined is used to prevent unnecessary parsing.
  // Default parameter is necessary to keep URLSearchParams.length === 0 in
  // accordance with Web IDL spec.
  constructor(init = undefined) {
    markTransferMode(this, false, false);

    if (init == null) {
      // Do nothing
    } else if (typeof init === 'object' || typeof init === 'function') {
      const method = init[SymbolIterator];
      if (method === this[SymbolIterator] && #searchParams in init) {
        // While the spec does not have this branch, we can use it as a
        // shortcut to avoid having to go through the costly generic iterator.
        const childParams = init.#searchParams;
        this.#searchParams = childParams.slice();
      } else if (method != null) {
        // Sequence<sequence<USVString>>
        if (typeof method !== 'function') {
          throw new ERR_ARG_NOT_ITERABLE('Query pairs');
        }

        // The following implementation differs from the URL specification:
        // Sequences must first be converted from ECMAScript objects before
        // and operations are done on them, and the operation of converting
        // the sequences would first exhaust the iterators. If the iterator
        // returns something invalid in the middle, whether it would be called
        // after that would be an observable change to the users.
        // Exhausting the iterator and later converting them to USVString comes
        // with a significant cost (~40-80%). In order optimize URLSearchParams
        // creation duration, Node.js merges the iteration and converting
        // iterations into a single iteration.
        for (const pair of init) {
          if (pair == null) {
            throw new ERR_INVALID_TUPLE('Each query pair', '[name, value]');
          } else if (ArrayIsArray(pair)) {
            // If innerSequence's size is not 2, then throw a TypeError.
            if (pair.length !== 2) {
              throw new ERR_INVALID_TUPLE('Each query pair', '[name, value]');
            }
            // Append (innerSequence[0], innerSequence[1]) to query's list.
            ArrayPrototypePush(
              this.#searchParams,
              StringPrototypeToWellFormed(`${pair[0]}`),
              StringPrototypeToWellFormed(`${pair[1]}`),
            );
          } else {
            if (((typeof pair !== 'object' && typeof pair !== 'function') ||
                typeof pair[SymbolIterator] !== 'function')) {
              throw new ERR_INVALID_TUPLE('Each query pair', '[name, value]');
            }

            let length = 0;

            for (const element of pair) {
              length++;
              ArrayPrototypePush(this.#searchParams, StringPrototypeToWellFormed(`${element}`));
            }

            // If innerSequence's size is not 2, then throw a TypeError.
            if (length !== 2) {
              throw new ERR_INVALID_TUPLE('Each query pair', '[name, value]');
            }
          }
        }
      } else {
        // Record<USVString, USVString>
        // Need to use reflection APIs for full spec compliance.
        const visited = new SafeMap();
        const keys = ReflectOwnKeys(init);
        for (let i = 0; i < keys.length; i++) {
          const key = keys[i];
          const desc = ReflectGetOwnPropertyDescriptor(init, key);
          if (desc !== undefined && desc.enumerable) {
            const typedKey = StringPrototypeToWellFormed(key);
            const typedValue = StringPrototypeToWellFormed(`${init[key]}`);

            // Two different keys may become the same USVString after normalization.
            // In that case, we retain the later one. Refer to WPT.
            const keyIdx = visited.get(typedKey);
            if (keyIdx !== undefined) {
              this.#searchParams[keyIdx] = typedValue;
            } else {
              visited.set(typedKey, ArrayPrototypePush(this.#searchParams,
                                                       typedKey,
                                                       typedValue) - 1);
            }
          }
        }
      }
    } else {
      // https://url.spec.whatwg.org/#dom-urlsearchparams-urlsearchparams
      init = StringPrototypeToWellFormed(`${init}`);
      this.#searchParams = init ? parseParams(init) : [];
    }
  }

  [inspect.custom](recurseTimes, ctx) {
    if (typeof this !== 'object' || this === null || !(#searchParams in this))
      throw new ERR_INVALID_THIS('URLSearchParams');

    if (typeof recurseTimes === 'number' && recurseTimes < 0)
      return ctx.stylize('[Object]', 'special');

    const separator = ', ';
    const innerOpts = { ...ctx };
    if (recurseTimes !== null) {
      innerOpts.depth = recurseTimes - 1;
    }
    const innerInspect = (v) => inspect(v, innerOpts);

    const list = this.#searchParams;
    const output = [];
    for (let i = 0; i < list.length; i += 2)
      ArrayPrototypePush(
        output,
        `${innerInspect(list[i])} => ${innerInspect(list[i + 1])}`);

    const length = ArrayPrototypeReduce(
      output,
      (prev, cur) => prev + removeColors(cur).length + separator.length,
      -separator.length,
    );
    if (length > ctx.breakLength) {
      return `${this.constructor.name} {\n` +
      `  ${ArrayPrototypeJoin(output, ',\n  ')} }`;
    } else if (output.length) {
      return `${this.constructor.name} { ` +
      `${ArrayPrototypeJoin(output, separator)} }`;
    }
    return `${this.constructor.name} {}`;
  }

  get size() {
    if (typeof this !== 'object' || this === null || !(#searchParams in this))
      throw new ERR_INVALID_THIS('URLSearchParams');
    return this.#searchParams.length / 2;
  }

  append(name, value) {
    if (typeof this !== 'object' || this === null || !(#searchParams in this))
      throw new ERR_INVALID_THIS('URLSearchParams');

    if (arguments.length < 2) {
      throw new ERR_MISSING_ARGS('name', 'value');
    }

    name = StringPrototypeToWellFormed(`${name}`);
    value = StringPrototypeToWellFormed(`${value}`);
    ArrayPrototypePush(this.#searchParams, name, value);

    if (this.#context) {
      setURLSearchParamsModified(this.#context);
    }
  }

  delete(name, value = undefined) {
    if (typeof this !== 'object' || this === null || !(#searchParams in this))
      throw new ERR_INVALID_THIS('URLSearchParams');

    if (arguments.length < 1) {
      throw new ERR_MISSING_ARGS('name');
    }

    const list = this.#searchParams;
    name = StringPrototypeToWellFormed(`${name}`);

    if (value !== undefined) {
      value = StringPrototypeToWellFormed(`${value}`);
      for (let i = 0; i < list.length;) {
        if (list[i] === name && list[i + 1] === value) {
          list.splice(i, 2);
        } else {
          i += 2;
        }
      }
    } else {
      for (let i = 0; i < list.length;) {
        if (list[i] === name) {
          list.splice(i, 2);
        } else {
          i += 2;
        }
      }
    }

    if (this.#context) {
      setURLSearchParamsModified(this.#context);
    }
  }

  get(name) {
    if (typeof this !== 'object' || this === null || !(#searchParams in this))
      throw new ERR_INVALID_THIS('URLSearchParams');

    if (arguments.length < 1) {
      throw new ERR_MISSING_ARGS('name');
    }

    const list = this.#searchParams;
    name = StringPrototypeToWellFormed(`${name}`);
    for (let i = 0; i < list.length; i += 2) {
      if (list[i] === name) {
        return list[i + 1];
      }
    }
    return null;
  }

  getAll(name) {
    if (typeof this !== 'object' || this === null || !(#searchParams in this))
      throw new ERR_INVALID_THIS('URLSearchParams');

    if (arguments.length < 1) {
      throw new ERR_MISSING_ARGS('name');
    }

    const list = this.#searchParams;
    const values = [];
    name = StringPrototypeToWellFormed(`${name}`);
    for (let i = 0; i < list.length; i += 2) {
      if (list[i] === name) {
        values.push(list[i + 1]);
      }
    }
    return values;
  }

  has(name, value = undefined) {
    if (typeof this !== 'object' || this === null || !(#searchParams in this))
      throw new ERR_INVALID_THIS('URLSearchParams');

    if (arguments.length < 1) {
      throw new ERR_MISSING_ARGS('name');
    }

    const list = this.#searchParams;
    name = StringPrototypeToWellFormed(`${name}`);

    if (value !== undefined) {
      value = StringPrototypeToWellFormed(`${value}`);
    }

    for (let i = 0; i < list.length; i += 2) {
      if (list[i] === name) {
        if (value === undefined || list[i + 1] === value) {
          return true;
        }
      }
    }

    return false;
  }

  set(name, value) {
    if (typeof this !== 'object' || this === null || !(#searchParams in this))
      throw new ERR_INVALID_THIS('URLSearchParams');

    if (arguments.length < 2) {
      throw new ERR_MISSING_ARGS('name', 'value');
    }

    const list = this.#searchParams;
    name = StringPrototypeToWellFormed(`${name}`);
    value = StringPrototypeToWellFormed(`${value}`);

    // If there are any name-value pairs whose name is `name`, in `list`, set
    // the value of the first such name-value pair to `value` and remove the
    // others.
    let found = false;
    for (let i = 0; i < list.length;) {
      const cur = list[i];
      if (cur === name) {
        if (!found) {
          list[i + 1] = value;
          found = true;
          i += 2;
        } else {
          list.splice(i, 2);
        }
      } else {
        i += 2;
      }
    }

    // Otherwise, append a new name-value pair whose name is `name` and value
    // is `value`, to `list`.
    if (!found) {
      ArrayPrototypePush(list, name, value);
    }

    if (this.#context) {
      setURLSearchParamsModified(this.#context);
    }
  }

  sort() {
    if (typeof this !== 'object' || this === null || !(#searchParams in this))
      throw new ERR_INVALID_THIS('URLSearchParams');

    const a = this.#searchParams;
    const len = a.length;

    if (len <= 2) {
      // Nothing needs to be done.
    } else if (len < 100) {
      // 100 is found through testing.
      // Simple stable in-place insertion sort
      // Derived from v8/src/js/array.js
      for (let i = 2; i < len; i += 2) {
        const curKey = a[i];
        const curVal = a[i + 1];
        let j;
        for (j = i - 2; j >= 0; j -= 2) {
          if (a[j] > curKey) {
            a[j + 2] = a[j];
            a[j + 3] = a[j + 1];
          } else {
            break;
          }
        }
        a[j + 2] = curKey;
        a[j + 3] = curVal;
      }
    } else {
      // Bottom-up iterative stable merge sort
      const lBuffer = new Array(len);
      const rBuffer = new Array(len);
      for (let step = 2; step < len; step *= 2) {
        for (let start = 0; start < len - 2; start += 2 * step) {
          const mid = start + step;
          let end = mid + step;
          end = end < len ? end : len;
          if (mid > end)
            continue;
          merge(a, start, mid, end, lBuffer, rBuffer);
        }
      }
    }

    if (this.#context) {
      setURLSearchParamsModified(this.#context);
    }
  }

  // https://heycam.github.io/webidl/#es-iterators
  // Define entries here rather than [Symbol.iterator] as the function name
  // must be set to `entries`.
  entries() {
    if (typeof this !== 'object' || this === null || !(#searchParams in this))
      throw new ERR_INVALID_THIS('URLSearchParams');

    return new URLSearchParamsIterator(this, 'key+value');
  }

  forEach(callback, thisArg = undefined) {
    if (typeof this !== 'object' || this === null || !(#searchParams in this))
      throw new ERR_INVALID_THIS('URLSearchParams');

    validateFunction(callback, 'callback');

    let list = this.#searchParams;

    let i = 0;
    while (i < list.length) {
      const key = list[i];
      const value = list[i + 1];
      callback.call(thisArg, value, key, this);
      // In case the URL object's `search` is updated
      list = this.#searchParams;
      i += 2;
    }
  }

  // https://heycam.github.io/webidl/#es-iterable
  keys() {
    if (typeof this !== 'object' || this === null || !(#searchParams in this))
      throw new ERR_INVALID_THIS('URLSearchParams');

    return new URLSearchParamsIterator(this, 'key');
  }

  values() {
    if (typeof this !== 'object' || this === null || !(#searchParams in this))
      throw new ERR_INVALID_THIS('URLSearchParams');

    return new URLSearchParamsIterator(this, 'value');
  }

  // https://heycam.github.io/webidl/#es-stringifier
  // https://url.spec.whatwg.org/#urlsearchparams-stringification-behavior
  toString() {
    if (typeof this !== 'object' || this === null || !(#searchParams in this))
      throw new ERR_INVALID_THIS('URLSearchParams');

    return serializeParams(this.#searchParams);
  }
}

ObjectDefineProperties(URLSearchParams.prototype, {
  append: kEnumerableProperty,
  delete: kEnumerableProperty,
  get: kEnumerableProperty,
  getAll: kEnumerableProperty,
  has: kEnumerableProperty,
  set: kEnumerableProperty,
  size: kEnumerableProperty,
  sort: kEnumerableProperty,
  entries: kEnumerableProperty,
  forEach: kEnumerableProperty,
  keys: kEnumerableProperty,
  values: kEnumerableProperty,
  toString: kEnumerableProperty,
  [SymbolToStringTag]: { __proto__: null, configurable: true, value: 'URLSearchParams' },

  // https://heycam.github.io/webidl/#es-iterable-entries
  [SymbolIterator]: {
    __proto__: null,
    configurable: true,
    writable: true,
    value: URLSearchParams.prototype.entries,
  },
});

/**
 * Checks if a value has the shape of a WHATWG URL object.
 *
 * Using a symbol or instanceof would not be able to recognize URL objects
 * coming from other implementations (e.g. in Electron), so instead we are
 * checking some well known properties for a lack of a better test.
 *
 * We use `href` and `protocol` as they are the only properties that are
 * easy to retrieve and calculate due to the lazy nature of the getters.
 *
 * We check for `auth` and `path` attribute to distinguish legacy url instance with
 * WHATWG URL instance.
 * @param {*} self
 * @returns {self is URL}
 */
function isURL(self) {
  return Boolean(self?.href && self.protocol && self.auth === undefined && self.path === undefined);
}

/**
 * A unique symbol used as a private identifier to safely invoke the URL constructor
 * with a special parsing behavior. When passed as the third argument to the URL
 * constructor, it signals that the constructor should not throw an exception
 * for invalid URL inputs.
 */
const kParseURLSymbol = Symbol('kParseURL');
const kCreateURLFromPosixPathSymbol = Symbol('kCreateURLFromPosixPath');
const kCreateURLFromWindowsPathSymbol = Symbol('kCreateURLFromWindowsPath');

class URL {
  #context = new URLContext();
  #searchParams;
  #searchParamsModified;

  static {
    setURLSearchParamsModified = (obj) => {
      // When URLSearchParams changes, we lazily update URL on the next read/write for performance.
      obj.#searchParamsModified = true;

      // If URL has an existing search, remove it without cascading back to URLSearchParams.
      // Do this to avoid any internal confusion about whether URLSearchParams or URL is up-to-date.
      if (obj.#context.hasSearch) {
        obj.#updateContext(bindingUrl.update(obj.#context.href, updateActions.kSearch, ''));
      }
    };
  }

  constructor(input, base = undefined, parseSymbol = undefined) {
    markTransferMode(this, false, false);

    if (arguments.length === 0) {
      throw new ERR_MISSING_ARGS('url');
    }

    // StringPrototypeToWellFormed is not needed.
    input = `${input}`;

    if (base !== undefined) {
      base = `${base}`;
    }

    let href;
    if (arguments.length < 3) {
      href = bindingUrl.parse(input, base, true);
    } else {
      const raiseException = parseSymbol !== kParseURLSymbol;
      const interpretAsWindowsPath = parseSymbol === kCreateURLFromWindowsPathSymbol;
      const pathToFileURL = interpretAsWindowsPath || (parseSymbol === kCreateURLFromPosixPathSymbol);
      href = pathToFileURL ?
        bindingUrl.pathToFileURL(input, interpretAsWindowsPath, base) :
        bindingUrl.parse(input, base, raiseException);
    }
    if (href) {
      this.#updateContext(href);
    }
  }

  static parse(input, base = undefined) {
    if (arguments.length === 0) {
      throw new ERR_MISSING_ARGS('url');
    }
    const parsedURLObject = new URL(input, base, kParseURLSymbol);
    return parsedURLObject.href ? parsedURLObject : null;
  }

  [inspect.custom](depth, opts) {
    if (typeof depth === 'number' && depth < 0)
      return this;

    const constructor = getConstructorOf(this) || URL;
    const obj = { __proto__: { constructor } };

    obj.href = this.href;
    obj.origin = this.origin;
    obj.protocol = this.protocol;
    obj.username = this.username;
    obj.password = this.password;
    obj.host = this.host;
    obj.hostname = this.hostname;
    obj.port = this.port;
    obj.pathname = this.pathname;
    obj.search = this.search;
    obj.searchParams = this.searchParams;
    obj.hash = this.hash;

    if (opts.showHidden) {
      obj[contextForInspect] = this.#context;
    }

    return `${constructor.name} ${inspect(obj, opts)}`;
  }

  #getSearchFromContext() {
    if (!this.#context.hasSearch) return '';
    let endsAt = this.#context.href.length;
    if (this.#context.hasHash) endsAt = this.#context.hash_start;
    if (endsAt - this.#context.search_start <= 1) return '';
    return StringPrototypeSlice(this.#context.href, this.#context.search_start, endsAt);
  }

  #getSearchFromParams() {
    if (!this.#searchParams?.size) return '';
    return `?${this.#searchParams}`;
  }

  #ensureSearchParamsUpdated() {
    // URL is updated lazily to greatly improve performance when URLSearchParams is updated repeatedly.
    // If URLSearchParams has been modified, reflect that back into URL, without cascading back.
    if (this.#searchParamsModified) {
      this.#searchParamsModified = false;
      this.#updateContext(bindingUrl.update(this.#context.href, updateActions.kSearch, this.#getSearchFromParams()));
    }
  }

  /**
   * Update the internal context state for URL.
   * @param {string} href New href string from `bindingUrl.update`.
   * @param {boolean} [shouldUpdateSearchParams] If the update has potential to update search params (href/search).
   */
  #updateContext(href, shouldUpdateSearchParams = false) {
    const previousSearch = shouldUpdateSearchParams && this.#searchParams &&
      (this.#searchParamsModified ? this.#getSearchFromParams() : this.#getSearchFromContext());

    this.#context.href = href;

    const {
      0: protocol_end,
      1: username_end,
      2: host_start,
      3: host_end,
      4: port,
      5: pathname_start,
      6: search_start,
      7: hash_start,
      8: scheme_type,
    } = bindingUrl.urlComponents;

    this.#context.protocol_end = protocol_end;
    this.#context.username_end = username_end;
    this.#context.host_start = host_start;
    this.#context.host_end = host_end;
    this.#context.port = port;
    this.#context.pathname_start = pathname_start;
    this.#context.search_start = search_start;
    this.#context.hash_start = hash_start;
    this.#context.scheme_type = scheme_type;

    if (this.#searchParams) {
      // If the search string has updated, URL becomes the source of truth, and we update URLSearchParams.
      // Only do this when we're expecting it to have changed, otherwise a change to hash etc.
      // would incorrectly compare the URLSearchParams state to the empty URL search state.
      if (shouldUpdateSearchParams) {
        const currentSearch = this.#getSearchFromContext();
        if (previousSearch !== currentSearch) {
          setURLSearchParams(this.#searchParams, currentSearch);
          this.#searchParamsModified = false;
        }
      }

      // If we have a URLSearchParams, ensure that URL is up-to-date with any modification to it.
      this.#ensureSearchParamsUpdated();
    }
  }

  toString() {
    // Updates to URLSearchParams are lazily propagated to URL, so we need to check we're in sync.
    this.#ensureSearchParamsUpdated();
    return this.#context.href;
  }

  get href() {
    // Updates to URLSearchParams are lazily propagated to URL, so we need to check we're in sync.
    this.#ensureSearchParamsUpdated();
    return this.#context.href;
  }

  set href(value) {
    value = `${value}`;
    const href = bindingUrl.update(this.#context.href, updateActions.kHref, value);
    if (!href) { throw new ERR_INVALID_URL(value); }
    this.#updateContext(href, true);
  }

  // readonly
  get origin() {
    const protocol = StringPrototypeSlice(this.#context.href, 0, this.#context.protocol_end);

    // Check if scheme_type is not `NOT_SPECIAL`
    if (this.#context.scheme_type !== 1) {
      // Check if scheme_type is `FILE`
      if (this.#context.scheme_type === 6) {
        return 'null';
      }
      return `${protocol}//${this.host}`;
    }

    if (protocol === 'blob:') {
      const path = this.pathname;
      if (path.length > 0) {
        try {
          const out = new URL(path);
          // Only return origin of scheme is `http` or `https`
          // Otherwise return a new opaque origin (null).
          if (out.#context.scheme_type === 0 || out.#context.scheme_type === 2) {
            return `${out.protocol}//${out.host}`;
          }
        } catch {
          // Do nothing.
        }
      }
    }

    return 'null';
  }

  get protocol() {
    return StringPrototypeSlice(this.#context.href, 0, this.#context.protocol_end);
  }

  set protocol(value) {
    const href = bindingUrl.update(this.#context.href, updateActions.kProtocol, `${value}`);
    if (href) {
      this.#updateContext(href);
    }
  }

  get username() {
    if (this.#context.protocol_end + 2 < this.#context.username_end) {
      return StringPrototypeSlice(this.#context.href, this.#context.protocol_end + 2, this.#context.username_end);
    }
    return '';
  }

  set username(value) {
    const href = bindingUrl.update(this.#context.href, updateActions.kUsername, `${value}`);
    if (href) {
      this.#updateContext(href);
    }
  }

  get password() {
    if (this.#context.host_start - this.#context.username_end > 0) {
      return StringPrototypeSlice(this.#context.href, this.#context.username_end + 1, this.#context.host_start);
    }
    return '';
  }

  set password(value) {
    const href = bindingUrl.update(this.#context.href, updateActions.kPassword, `${value}`);
    if (href) {
      this.#updateContext(href);
    }
  }

  get host() {
    let startsAt = this.#context.host_start;
    if (this.#context.href[startsAt] === '@') {
      startsAt++;
    }
    // If we have an empty host, then the space between components.host_end and
    // components.pathname_start may be occupied by /.
    if (startsAt === this.#context.host_end) {
      return '';
    }
    return StringPrototypeSlice(this.#context.href, startsAt, this.#context.pathname_start);
  }

  set host(value) {
    const href = bindingUrl.update(this.#context.href, updateActions.kHost, `${value}`);
    if (href) {
      this.#updateContext(href);
    }
  }

  get hostname() {
    let startsAt = this.#context.host_start;
    // host_start might be "@" if the URL has credentials
    if (this.#context.href[startsAt] === '@') {
      startsAt++;
    }
    return StringPrototypeSlice(this.#context.href, startsAt, this.#context.host_end);
  }

  set hostname(value) {
    const href = bindingUrl.update(this.#context.href, updateActions.kHostname, `${value}`);
    if (href) {
      this.#updateContext(href);
    }
  }

  get port() {
    if (this.#context.hasPort) {
      return `${this.#context.port}`;
    }
    return '';
  }

  set port(value) {
    const href = bindingUrl.update(this.#context.href, updateActions.kPort, `${value}`);
    if (href) {
      this.#updateContext(href);
    }
  }

  get pathname() {
    let endsAt;
    if (this.#context.hasSearch) {
      endsAt = this.#context.search_start;
    } else if (this.#context.hasHash) {
      endsAt = this.#context.hash_start;
    }
    return StringPrototypeSlice(this.#context.href, this.#context.pathname_start, endsAt);
  }

  set pathname(value) {
    const href = bindingUrl.update(this.#context.href, updateActions.kPathname, `${value}`);
    if (href) {
      this.#updateContext(href);
    }
  }

  get search() {
    // Updates to URLSearchParams are lazily propagated to URL, so we need to check we're in sync.
    this.#ensureSearchParamsUpdated();
    return this.#getSearchFromContext();
  }

  set search(value) {
    const href = bindingUrl.update(this.#context.href, updateActions.kSearch, StringPrototypeToWellFormed(`${value}`));
    if (href) {
      this.#updateContext(href, true);
    }
  }

  // readonly
  get searchParams() {
    // Create URLSearchParams on demand to greatly improve the URL performance.
    if (this.#searchParams == null) {
      this.#searchParams = new URLSearchParams(this.#getSearchFromContext());
      setURLSearchParamsContext(this.#searchParams, this);
      this.#searchParamsModified = false;
    }
    return this.#searchParams;
  }

  get hash() {
    if (!this.#context.hasHash || (this.#context.href.length - this.#context.hash_start <= 1)) {
      return '';
    }
    return StringPrototypeSlice(this.#context.href, this.#context.hash_start);
  }

  set hash(value) {
    const href = bindingUrl.update(this.#context.href, updateActions.kHash, `${value}`);
    if (href) {
      this.#updateContext(href);
    }
  }

  toJSON() {
    // Updates to URLSearchParams are lazily propagated to URL, so we need to check we're in sync.
    this.#ensureSearchParamsUpdated();
    return this.#context.href;
  }

  static canParse(url, base = undefined) {
    if (arguments.length === 0) {
      throw new ERR_MISSING_ARGS('url');
    }

    url = `${url}`;

    if (base !== undefined) {
      return bindingUrl.canParse(url, `${base}`);
    }

    // It is important to differentiate the canParse call statements
    // since they resolve into different v8 fast api overloads.
    return bindingUrl.canParse(url);
  }
}

ObjectDefineProperties(URL.prototype, {
  [SymbolToStringTag]: { __proto__: null, configurable: true, value: 'URL' },
  toString: kEnumerableProperty,
  href: kEnumerableProperty,
  origin: kEnumerableProperty,
  protocol: kEnumerableProperty,
  username: kEnumerableProperty,
  password: kEnumerableProperty,
  host: kEnumerableProperty,
  hostname: kEnumerableProperty,
  port: kEnumerableProperty,
  pathname: kEnumerableProperty,
  search: kEnumerableProperty,
  searchParams: kEnumerableProperty,
  hash: kEnumerableProperty,
  toJSON: kEnumerableProperty,
});

ObjectDefineProperties(URL, {
  canParse: {
    __proto__: null,
    configurable: true,
    writable: true,
    enumerable: true,
  },
  parse: {
    __proto__: null,
    configurable: true,
    writable: true,
    enumerable: true,
  },
});

function installObjectURLMethods() {
  const bindingBlob = internalBinding('blob');

  function createObjectURL(obj) {
    const cryptoRandom = lazyCryptoRandom();
    if (cryptoRandom === undefined)
      throw new ERR_NO_CRYPTO();

    const blob = lazyBlob();
    if (!blob.isBlob(obj))
      throw new ERR_INVALID_ARG_TYPE('obj', 'Blob', obj);

    const id = cryptoRandom.randomUUID();

    bindingBlob.storeDataObject(id, obj[blob.kHandle], obj.size, obj.type);

    return `blob:nodedata:${id}`;
  }

  function revokeObjectURL(url) {
    if (arguments.length === 0) {
      throw new ERR_MISSING_ARGS('url');
    }

    bindingBlob.revokeObjectURL(`${url}`);
  }

  ObjectDefineProperties(URL, {
    createObjectURL: {
      __proto__: null,
      configurable: true,
      writable: true,
      enumerable: true,
      value: createObjectURL,
    },
    revokeObjectURL: {
      __proto__: null,
      configurable: true,
      writable: true,
      enumerable: true,
      value: revokeObjectURL,
    },
  });
}

// application/x-www-form-urlencoded parser
// Ref: https://url.spec.whatwg.org/#concept-urlencoded-parser
function parseParams(qs) {
  const out = [];
  let seenSep = false;
  let buf = '';
  let encoded = false;
  let encodeCheck = 0;
  let i = qs[0] === '?' ? 1 : 0;
  let pairStart = i;
  let lastPos = i;
  for (; i < qs.length; ++i) {
    const code = StringPrototypeCharCodeAt(qs, i);

    // Try matching key/value pair separator
    if (code === CHAR_AMPERSAND) {
      if (pairStart === i) {
        // We saw an empty substring between pair separators
        lastPos = pairStart = i + 1;
        continue;
      }

      if (lastPos < i)
        buf += qs.slice(lastPos, i);
      if (encoded)
        buf = querystring.unescape(buf);
      out.push(buf);

      // If `buf` is the key, add an empty value.
      if (!seenSep)
        out.push('');

      seenSep = false;
      buf = '';
      encoded = false;
      encodeCheck = 0;
      lastPos = pairStart = i + 1;
      continue;
    }

    // Try matching key/value separator (e.g. '=') if we haven't already
    if (!seenSep && code === CHAR_EQUAL) {
      // Key/value separator match!
      if (lastPos < i)
        buf += qs.slice(lastPos, i);
      if (encoded)
        buf = querystring.unescape(buf);
      out.push(buf);

      seenSep = true;
      buf = '';
      encoded = false;
      encodeCheck = 0;
      lastPos = i + 1;
      continue;
    }

    // Handle + and percent decoding.
    if (code === CHAR_PLUS) {
      if (lastPos < i)
        buf += StringPrototypeSlice(qs, lastPos, i);
      buf += ' ';
      lastPos = i + 1;
    } else if (!encoded) {
      // Try to match an (valid) encoded byte (once) to minimize unnecessary
      // calls to string decoding functions
      if (code === CHAR_PERCENT) {
        encodeCheck = 1;
      } else if (encodeCheck > 0) {
        if (isHexTable[code] === 1) {
          if (++encodeCheck === 3) {
            encoded = true;
          }
        } else {
          encodeCheck = 0;
        }
      }
    }
  }

  // Deal with any leftover key or value data

  // There is a trailing &. No more processing is needed.
  if (pairStart === i)
    return out;

  if (lastPos < i)
    buf += StringPrototypeSlice(qs, lastPos, i);
  if (encoded)
    buf = querystring.unescape(buf);
  ArrayPrototypePush(out, buf);

  // If `buf` is the key, add an empty value.
  if (!seenSep)
    ArrayPrototypePush(out, '');

  return out;
}

// Adapted from querystring's implementation.
// Ref: https://url.spec.whatwg.org/#concept-urlencoded-byte-serializer
const noEscape = new Int8Array([
/*
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, A, B, C, D, E, F
*/
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 0x00 - 0x0F
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 0x10 - 0x1F
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0, // 0x20 - 0x2F
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, // 0x30 - 0x3F
  0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 0x40 - 0x4F
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, // 0x50 - 0x5F
  0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 0x60 - 0x6F
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,  // 0x70 - 0x7F
]);

// Special version of hexTable that uses `+` for U+0020 SPACE.
const paramHexTable = hexTable.slice();
paramHexTable[0x20] = '+';

// application/x-www-form-urlencoded serializer
// Ref: https://url.spec.whatwg.org/#concept-urlencoded-serializer
function serializeParams(array) {
  const len = array.length;
  if (len === 0)
    return '';

  const firstEncodedParam = encodeStr(array[0], noEscape, paramHexTable);
  const firstEncodedValue = encodeStr(array[1], noEscape, paramHexTable);
  let output = `${firstEncodedParam}=${firstEncodedValue}`;

  for (let i = 2; i < len; i += 2) {
    const encodedParam = encodeStr(array[i], noEscape, paramHexTable);
    const encodedValue = encodeStr(array[i + 1], noEscape, paramHexTable);
    output += `&${encodedParam}=${encodedValue}`;
  }

  return output;
}

// for merge sort
function merge(out, start, mid, end, lBuffer, rBuffer) {
  const sizeLeft = mid - start;
  const sizeRight = end - mid;
  let l, r, o;

  for (l = 0; l < sizeLeft; l++)
    lBuffer[l] = out[start + l];
  for (r = 0; r < sizeRight; r++)
    rBuffer[r] = out[mid + r];

  l = 0;
  r = 0;
  o = start;
  while (l < sizeLeft && r < sizeRight) {
    if (lBuffer[l] <= rBuffer[r]) {
      out[o++] = lBuffer[l++];
      out[o++] = lBuffer[l++];
    } else {
      out[o++] = rBuffer[r++];
      out[o++] = rBuffer[r++];
    }
  }
  while (l < sizeLeft)
    out[o++] = lBuffer[l++];
  while (r < sizeRight)
    out[o++] = rBuffer[r++];
}

function domainToASCII(domain) {
  if (arguments.length < 1)
    throw new ERR_MISSING_ARGS('domain');

  // StringPrototypeToWellFormed is not needed.
  return bindingUrl.domainToASCII(`${domain}`);
}

function domainToUnicode(domain) {
  if (arguments.length < 1)
    throw new ERR_MISSING_ARGS('domain');

  // StringPrototypeToWellFormed is not needed.
  return bindingUrl.domainToUnicode(`${domain}`);
}

/**
 * Utility function that converts a URL object into an ordinary options object
 * as expected by the `http.request` and `https.request` APIs.
 * @param {URL} url
 * @returns {Record<string, unknown>}
 */
function urlToHttpOptions(url) {
  const { hostname, pathname, port, username, password, search } = url;
  const options = {
    __proto__: null,
    ...url, // In case the url object was extended by the user.
    protocol: url.protocol,
    hostname: hostname && hostname[0] === '[' ?
      StringPrototypeSlice(hostname, 1, -1) :
      hostname,
    hash: url.hash,
    search: search,
    pathname: pathname,
    path: `${pathname || ''}${search || ''}`,
    href: url.href,
  };
  if (port !== '') {
    options.port = Number(port);
  }
  if (username || password) {
    options.auth = `${decodeURIComponent(username)}:${decodeURIComponent(password)}`;
  }
  return options;
}

function getPathFromURLWin32(url) {
  const hostname = url.hostname;
  let pathname = url.pathname;
  for (let n = 0; n < pathname.length; n++) {
    if (pathname[n] === '%') {
      const third = StringPrototypeCodePointAt(pathname, n + 2) | 0x20;
      if ((pathname[n + 1] === '2' && third === 102) || // 2f 2F /
          (pathname[n + 1] === '5' && third === 99)) {  // 5c 5C \
        throw new ERR_INVALID_FILE_URL_PATH(
          'must not include encoded \\ or / characters',
        );
      }
    }
  }
  pathname = SideEffectFreeRegExpPrototypeSymbolReplace(FORWARD_SLASH, pathname, '\\');
  pathname = decodeURIComponent(pathname);
  if (hostname !== '') {
    // If hostname is set, then we have a UNC path
    // Pass the hostname through domainToUnicode just in case
    // it is an IDN using punycode encoding. We do not need to worry
    // about percent encoding because the URL parser will have
    // already taken care of that for us. Note that this only
    // causes IDNs with an appropriate `xn--` prefix to be decoded.
    return `\\\\${domainToUnicode(hostname)}${pathname}`;
  }
  // Otherwise, it's a local path that requires a drive letter
  const letter = StringPrototypeCodePointAt(pathname, 1) | 0x20;
  const sep = StringPrototypeCharAt(pathname, 2);
  if (letter < CHAR_LOWERCASE_A || letter > CHAR_LOWERCASE_Z ||   // a..z A..Z
      (sep !== ':')) {
    throw new ERR_INVALID_FILE_URL_PATH('must be absolute');
  }
  return StringPrototypeSlice(pathname, 1);
}

function getPathFromURLPosix(url) {
  if (url.hostname !== '') {
    throw new ERR_INVALID_FILE_URL_HOST(platform);
  }
  const pathname = url.pathname;
  for (let n = 0; n < pathname.length; n++) {
    if (pathname[n] === '%') {
      const third = StringPrototypeCodePointAt(pathname, n + 2) | 0x20;
      if (pathname[n + 1] === '2' && third === 102) {
        throw new ERR_INVALID_FILE_URL_PATH(
          'must not include encoded / characters',
        );
      }
    }
  }
  return decodeURIComponent(pathname);
}

function fileURLToPath(path, options = kEmptyObject) {
  const windows = options?.windows;
  if (typeof path === 'string')
    path = new URL(path);
  else if (!isURL(path))
    throw new ERR_INVALID_ARG_TYPE('path', ['string', 'URL'], path);
  if (path.protocol !== 'file:')
    throw new ERR_INVALID_URL_SCHEME('file');
  return (windows ?? isWindows) ? getPathFromURLWin32(path) : getPathFromURLPosix(path);
}

function pathToFileURL(filepath, options = kEmptyObject) {
  const windows = options?.windows ?? isWindows;
  const isUNC = windows && StringPrototypeStartsWith(filepath, '\\\\');
  let resolved = isUNC ?
    filepath :
    (windows ? path.win32.resolve(filepath) : path.posix.resolve(filepath));
  if (isUNC || (windows && StringPrototypeStartsWith(resolved, '\\\\'))) {
    // UNC path format: \\server\share\resource
    // Handle extended UNC path and standard UNC path
    // "\\?\UNC\" path prefix should be ignored.
    // Ref: https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation
    const isExtendedUNC = StringPrototypeStartsWith(resolved, '\\\\?\\UNC\\');
    const prefixLength = isExtendedUNC ? 8 : 2;
    const hostnameEndIndex = StringPrototypeIndexOf(resolved, '\\', prefixLength);
    if (hostnameEndIndex === -1) {
      throw new ERR_INVALID_ARG_VALUE(
        'path',
        resolved,
        'Missing UNC resource path',
      );
    }
    if (hostnameEndIndex === 2) {
      throw new ERR_INVALID_ARG_VALUE(
        'path',
        resolved,
        'Empty UNC servername',
      );
    }
    const hostname = StringPrototypeSlice(resolved, prefixLength, hostnameEndIndex);
    return new URL(StringPrototypeSlice(resolved, hostnameEndIndex), hostname, kCreateURLFromWindowsPathSymbol);
  }
  // path.resolve strips trailing slashes so we must add them back
  const filePathLast = StringPrototypeCharCodeAt(filepath,
                                                 filepath.length - 1);
  if ((filePathLast === CHAR_FORWARD_SLASH ||
       ((windows ?? isWindows) && filePathLast === CHAR_BACKWARD_SLASH)) &&
      resolved[resolved.length - 1] !== path.sep)
    resolved += '/';

  return new URL(resolved, undefined, windows ? kCreateURLFromWindowsPathSymbol : kCreateURLFromPosixPathSymbol);
}

function toPathIfFileURL(fileURLOrPath) {
  if (!isURL(fileURLOrPath))
    return fileURLOrPath;
  return fileURLToPath(fileURLOrPath);
}

/**
 * This util takes a string containing a URL and return the URL origin,
 * its meant to avoid calls to `new URL` constructor.
 * @param {string} url
 * @returns {URL['origin']}
 */
function getURLOrigin(url) {
  return bindingUrl.getOrigin(url);
}

module.exports = {
  fileURLToPath,
  pathToFileURL,
  toPathIfFileURL,
  installObjectURLMethods,
  URL,
  URLSearchParams,
  URLParse: URL.parse,
  domainToASCII,
  domainToUnicode,
  urlToHttpOptions,
  encodeStr,
  isURL,

  urlUpdateActions: updateActions,
  getURLOrigin,
  unsafeProtocol,
  hostlessProtocol,
  slashedProtocol,
};
