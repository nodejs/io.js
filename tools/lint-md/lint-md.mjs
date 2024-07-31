import fs from 'fs';
import path$2 from 'path';
import { pathToFileURL } from 'url';
import path$1 from 'node:path';
import process$1 from 'node:process';
import { fileURLToPath } from 'node:url';
import fs$1 from 'node:fs';
import os from 'node:os';
import tty from 'node:tty';

function bail(error) {
  if (error) {
    throw error
  }
}

var commonjsGlobal = typeof globalThis !== 'undefined' ? globalThis : typeof window !== 'undefined' ? window : typeof global !== 'undefined' ? global : typeof self !== 'undefined' ? self : {};

function getDefaultExportFromCjs (x) {
	return x && x.__esModule && Object.prototype.hasOwnProperty.call(x, 'default') ? x['default'] : x;
}

var hasOwn = Object.prototype.hasOwnProperty;
var toStr = Object.prototype.toString;
var defineProperty = Object.defineProperty;
var gOPD = Object.getOwnPropertyDescriptor;
var isArray = function isArray(arr) {
	if (typeof Array.isArray === 'function') {
		return Array.isArray(arr);
	}
	return toStr.call(arr) === '[object Array]';
};
var isPlainObject$1 = function isPlainObject(obj) {
	if (!obj || toStr.call(obj) !== '[object Object]') {
		return false;
	}
	var hasOwnConstructor = hasOwn.call(obj, 'constructor');
	var hasIsPrototypeOf = obj.constructor && obj.constructor.prototype && hasOwn.call(obj.constructor.prototype, 'isPrototypeOf');
	if (obj.constructor && !hasOwnConstructor && !hasIsPrototypeOf) {
		return false;
	}
	var key;
	for (key in obj) {  }
	return typeof key === 'undefined' || hasOwn.call(obj, key);
};
var setProperty = function setProperty(target, options) {
	if (defineProperty && options.name === '__proto__') {
		defineProperty(target, options.name, {
			enumerable: true,
			configurable: true,
			value: options.newValue,
			writable: true
		});
	} else {
		target[options.name] = options.newValue;
	}
};
var getProperty = function getProperty(obj, name) {
	if (name === '__proto__') {
		if (!hasOwn.call(obj, name)) {
			return void 0;
		} else if (gOPD) {
			return gOPD(obj, name).value;
		}
	}
	return obj[name];
};
var extend$1 = function extend() {
	var options, name, src, copy, copyIsArray, clone;
	var target = arguments[0];
	var i = 1;
	var length = arguments.length;
	var deep = false;
	if (typeof target === 'boolean') {
		deep = target;
		target = arguments[1] || {};
		i = 2;
	}
	if (target == null || (typeof target !== 'object' && typeof target !== 'function')) {
		target = {};
	}
	for (; i < length; ++i) {
		options = arguments[i];
		if (options != null) {
			for (name in options) {
				src = getProperty(target, name);
				copy = getProperty(options, name);
				if (target !== copy) {
					if (deep && copy && (isPlainObject$1(copy) || (copyIsArray = isArray(copy)))) {
						if (copyIsArray) {
							copyIsArray = false;
							clone = src && isArray(src) ? src : [];
						} else {
							clone = src && isPlainObject$1(src) ? src : {};
						}
						setProperty(target, { name: name, newValue: extend(deep, clone, copy) });
					} else if (typeof copy !== 'undefined') {
						setProperty(target, { name: name, newValue: copy });
					}
				}
			}
		}
	}
	return target;
};
var extend$2 = getDefaultExportFromCjs(extend$1);

function ok$1() {}

function isPlainObject(value) {
	if (typeof value !== 'object' || value === null) {
		return false;
	}
	const prototype = Object.getPrototypeOf(value);
	return (prototype === null || prototype === Object.prototype || Object.getPrototypeOf(prototype) === null) && !(Symbol.toStringTag in value) && !(Symbol.iterator in value);
}

function trough() {
  const fns = [];
  const pipeline = {run, use};
  return pipeline
  function run(...values) {
    let middlewareIndex = -1;
    const callback = values.pop();
    if (typeof callback !== 'function') {
      throw new TypeError('Expected function as last argument, not ' + callback)
    }
    next(null, ...values);
    function next(error, ...output) {
      const fn = fns[++middlewareIndex];
      let index = -1;
      if (error) {
        callback(error);
        return
      }
      while (++index < values.length) {
        if (output[index] === null || output[index] === undefined) {
          output[index] = values[index];
        }
      }
      values = output;
      if (fn) {
        wrap(fn, next)(...output);
      } else {
        callback(null, ...output);
      }
    }
  }
  function use(middelware) {
    if (typeof middelware !== 'function') {
      throw new TypeError(
        'Expected `middelware` to be a function, not ' + middelware
      )
    }
    fns.push(middelware);
    return pipeline
  }
}
function wrap(middleware, callback) {
  let called;
  return wrapped
  function wrapped(...parameters) {
    const fnExpectsCallback = middleware.length > parameters.length;
    let result;
    if (fnExpectsCallback) {
      parameters.push(done);
    }
    try {
      result = middleware.apply(this, parameters);
    } catch (error) {
      const exception =  (error);
      if (fnExpectsCallback && called) {
        throw exception
      }
      return done(exception)
    }
    if (!fnExpectsCallback) {
      if (result && result.then && typeof result.then === 'function') {
        result.then(then, done);
      } else if (result instanceof Error) {
        done(result);
      } else {
        then(result);
      }
    }
  }
  function done(error, ...output) {
    if (!called) {
      called = true;
      callback(error, ...output);
    }
  }
  function then(value) {
    done(null, value);
  }
}

function stringifyPosition(value) {
  if (!value || typeof value !== 'object') {
    return ''
  }
  if ('position' in value || 'type' in value) {
    return position$1(value.position)
  }
  if ('start' in value || 'end' in value) {
    return position$1(value)
  }
  if ('line' in value || 'column' in value) {
    return point$2(value)
  }
  return ''
}
function point$2(point) {
  return index(point && point.line) + ':' + index(point && point.column)
}
function position$1(pos) {
  return point$2(pos && pos.start) + '-' + point$2(pos && pos.end)
}
function index(value) {
  return value && typeof value === 'number' ? value : 1
}

class VFileMessage extends Error {
  constructor(causeOrReason, optionsOrParentOrPlace, origin) {
    super();
    if (typeof optionsOrParentOrPlace === 'string') {
      origin = optionsOrParentOrPlace;
      optionsOrParentOrPlace = undefined;
    }
    let reason = '';
    let options = {};
    let legacyCause = false;
    if (optionsOrParentOrPlace) {
      if (
        'line' in optionsOrParentOrPlace &&
        'column' in optionsOrParentOrPlace
      ) {
        options = {place: optionsOrParentOrPlace};
      }
      else if (
        'start' in optionsOrParentOrPlace &&
        'end' in optionsOrParentOrPlace
      ) {
        options = {place: optionsOrParentOrPlace};
      }
      else if ('type' in optionsOrParentOrPlace) {
        options = {
          ancestors: [optionsOrParentOrPlace],
          place: optionsOrParentOrPlace.position
        };
      }
      else {
        options = {...optionsOrParentOrPlace};
      }
    }
    if (typeof causeOrReason === 'string') {
      reason = causeOrReason;
    }
    else if (!options.cause && causeOrReason) {
      legacyCause = true;
      reason = causeOrReason.message;
      options.cause = causeOrReason;
    }
    if (!options.ruleId && !options.source && typeof origin === 'string') {
      const index = origin.indexOf(':');
      if (index === -1) {
        options.ruleId = origin;
      } else {
        options.source = origin.slice(0, index);
        options.ruleId = origin.slice(index + 1);
      }
    }
    if (!options.place && options.ancestors && options.ancestors) {
      const parent = options.ancestors[options.ancestors.length - 1];
      if (parent) {
        options.place = parent.position;
      }
    }
    const start =
      options.place && 'start' in options.place
        ? options.place.start
        : options.place;
    this.ancestors = options.ancestors || undefined;
    this.cause = options.cause || undefined;
    this.column = start ? start.column : undefined;
    this.fatal = undefined;
    this.file;
    this.message = reason;
    this.line = start ? start.line : undefined;
    this.name = stringifyPosition(options.place) || '1:1';
    this.place = options.place || undefined;
    this.reason = this.message;
    this.ruleId = options.ruleId || undefined;
    this.source = options.source || undefined;
    this.stack =
      legacyCause && options.cause && typeof options.cause.stack === 'string'
        ? options.cause.stack
        : '';
    this.actual;
    this.expected;
    this.note;
    this.url;
  }
}
VFileMessage.prototype.file = '';
VFileMessage.prototype.name = '';
VFileMessage.prototype.reason = '';
VFileMessage.prototype.message = '';
VFileMessage.prototype.stack = '';
VFileMessage.prototype.column = undefined;
VFileMessage.prototype.line = undefined;
VFileMessage.prototype.ancestors = undefined;
VFileMessage.prototype.cause = undefined;
VFileMessage.prototype.fatal = undefined;
VFileMessage.prototype.place = undefined;
VFileMessage.prototype.ruleId = undefined;
VFileMessage.prototype.source = undefined;

function isUrl(fileUrlOrPath) {
  return Boolean(
    fileUrlOrPath !== null &&
      typeof fileUrlOrPath === 'object' &&
      'href' in fileUrlOrPath &&
      fileUrlOrPath.href &&
      'protocol' in fileUrlOrPath &&
      fileUrlOrPath.protocol &&
      fileUrlOrPath.auth === undefined
  )
}

const order =  ([
  'history',
  'path',
  'basename',
  'stem',
  'extname',
  'dirname'
]);
class VFile {
  constructor(value) {
    let options;
    if (!value) {
      options = {};
    } else if (isUrl(value)) {
      options = {path: value};
    } else if (typeof value === 'string' || isUint8Array$2(value)) {
      options = {value};
    } else {
      options = value;
    }
    this.cwd = process$1.cwd();
    this.data = {};
    this.history = [];
    this.messages = [];
    this.value;
    this.map;
    this.result;
    this.stored;
    let index = -1;
    while (++index < order.length) {
      const prop = order[index];
      if (
        prop in options &&
        options[prop] !== undefined &&
        options[prop] !== null
      ) {
        this[prop] = prop === 'history' ? [...options[prop]] : options[prop];
      }
    }
    let prop;
    for (prop in options) {
      if (!order.includes(prop)) {
        this[prop] = options[prop];
      }
    }
  }
  get basename() {
    return typeof this.path === 'string' ? path$1.basename(this.path) : undefined
  }
  set basename(basename) {
    assertNonEmpty(basename, 'basename');
    assertPart(basename, 'basename');
    this.path = path$1.join(this.dirname || '', basename);
  }
  get dirname() {
    return typeof this.path === 'string' ? path$1.dirname(this.path) : undefined
  }
  set dirname(dirname) {
    assertPath(this.basename, 'dirname');
    this.path = path$1.join(dirname || '', this.basename);
  }
  get extname() {
    return typeof this.path === 'string' ? path$1.extname(this.path) : undefined
  }
  set extname(extname) {
    assertPart(extname, 'extname');
    assertPath(this.dirname, 'extname');
    if (extname) {
      if (extname.codePointAt(0) !== 46 ) {
        throw new Error('`extname` must start with `.`')
      }
      if (extname.includes('.', 1)) {
        throw new Error('`extname` cannot contain multiple dots')
      }
    }
    this.path = path$1.join(this.dirname, this.stem + (extname || ''));
  }
  get path() {
    return this.history[this.history.length - 1]
  }
  set path(path) {
    if (isUrl(path)) {
      path = fileURLToPath(path);
    }
    assertNonEmpty(path, 'path');
    if (this.path !== path) {
      this.history.push(path);
    }
  }
  get stem() {
    return typeof this.path === 'string'
      ? path$1.basename(this.path, this.extname)
      : undefined
  }
  set stem(stem) {
    assertNonEmpty(stem, 'stem');
    assertPart(stem, 'stem');
    this.path = path$1.join(this.dirname || '', stem + (this.extname || ''));
  }
  fail(causeOrReason, optionsOrParentOrPlace, origin) {
    const message = this.message(causeOrReason, optionsOrParentOrPlace, origin);
    message.fatal = true;
    throw message
  }
  info(causeOrReason, optionsOrParentOrPlace, origin) {
    const message = this.message(causeOrReason, optionsOrParentOrPlace, origin);
    message.fatal = undefined;
    return message
  }
  message(causeOrReason, optionsOrParentOrPlace, origin) {
    const message = new VFileMessage(
      causeOrReason,
      optionsOrParentOrPlace,
      origin
    );
    if (this.path) {
      message.name = this.path + ':' + message.name;
      message.file = this.path;
    }
    message.fatal = false;
    this.messages.push(message);
    return message
  }
  toString(encoding) {
    if (this.value === undefined) {
      return ''
    }
    if (typeof this.value === 'string') {
      return this.value
    }
    const decoder = new TextDecoder(encoding || undefined);
    return decoder.decode(this.value)
  }
}
function assertPart(part, name) {
  if (part && part.includes(path$1.sep)) {
    throw new Error(
      '`' + name + '` cannot be a path: did not expect `' + path$1.sep + '`'
    )
  }
}
function assertNonEmpty(part, name) {
  if (!part) {
    throw new Error('`' + name + '` cannot be empty')
  }
}
function assertPath(path, name) {
  if (!path) {
    throw new Error('Setting `' + name + '` requires `path` to be set too')
  }
}
function isUint8Array$2(value) {
  return Boolean(
    value &&
      typeof value === 'object' &&
      'byteLength' in value &&
      'byteOffset' in value
  )
}

const CallableInstance =
  (
    (
      function (property) {
        const self = this;
        const constr = self.constructor;
        const proto =  (
          constr.prototype
        );
        const value = proto[property];
        const apply = function () {
          return value.apply(apply, arguments)
        };
        Object.setPrototypeOf(apply, proto);
        return apply
      }
    )
  );

const own$5 = {}.hasOwnProperty;
class Processor extends CallableInstance {
  constructor() {
    super('copy');
    this.Compiler = undefined;
    this.Parser = undefined;
    this.attachers = [];
    this.compiler = undefined;
    this.freezeIndex = -1;
    this.frozen = undefined;
    this.namespace = {};
    this.parser = undefined;
    this.transformers = trough();
  }
  copy() {
    const destination =
       (
        new Processor()
      );
    let index = -1;
    while (++index < this.attachers.length) {
      const attacher = this.attachers[index];
      destination.use(...attacher);
    }
    destination.data(extend$2(true, {}, this.namespace));
    return destination
  }
  data(key, value) {
    if (typeof key === 'string') {
      if (arguments.length === 2) {
        assertUnfrozen('data', this.frozen);
        this.namespace[key] = value;
        return this
      }
      return (own$5.call(this.namespace, key) && this.namespace[key]) || undefined
    }
    if (key) {
      assertUnfrozen('data', this.frozen);
      this.namespace = key;
      return this
    }
    return this.namespace
  }
  freeze() {
    if (this.frozen) {
      return this
    }
    const self =  ( (this));
    while (++this.freezeIndex < this.attachers.length) {
      const [attacher, ...options] = this.attachers[this.freezeIndex];
      if (options[0] === false) {
        continue
      }
      if (options[0] === true) {
        options[0] = undefined;
      }
      const transformer = attacher.call(self, ...options);
      if (typeof transformer === 'function') {
        this.transformers.use(transformer);
      }
    }
    this.frozen = true;
    this.freezeIndex = Number.POSITIVE_INFINITY;
    return this
  }
  parse(file) {
    this.freeze();
    const realFile = vfile(file);
    const parser = this.parser || this.Parser;
    assertParser('parse', parser);
    return parser(String(realFile), realFile)
  }
  process(file, done) {
    const self = this;
    this.freeze();
    assertParser('process', this.parser || this.Parser);
    assertCompiler('process', this.compiler || this.Compiler);
    return done ? executor(undefined, done) : new Promise(executor)
    function executor(resolve, reject) {
      const realFile = vfile(file);
      const parseTree =
         (
           (self.parse(realFile))
        );
      self.run(parseTree, realFile, function (error, tree, file) {
        if (error || !tree || !file) {
          return realDone(error)
        }
        const compileTree =
           (
             (tree)
          );
        const compileResult = self.stringify(compileTree, file);
        if (looksLikeAValue(compileResult)) {
          file.value = compileResult;
        } else {
          file.result = compileResult;
        }
        realDone(error,  (file));
      });
      function realDone(error, file) {
        if (error || !file) {
          reject(error);
        } else if (resolve) {
          resolve(file);
        } else {
          done(undefined, file);
        }
      }
    }
  }
  processSync(file) {
    let complete = false;
    let result;
    this.freeze();
    assertParser('processSync', this.parser || this.Parser);
    assertCompiler('processSync', this.compiler || this.Compiler);
    this.process(file, realDone);
    assertDone('processSync', 'process', complete);
    return result
    function realDone(error, file) {
      complete = true;
      bail(error);
      result = file;
    }
  }
  run(tree, file, done) {
    assertNode(tree);
    this.freeze();
    const transformers = this.transformers;
    if (!done && typeof file === 'function') {
      done = file;
      file = undefined;
    }
    return done ? executor(undefined, done) : new Promise(executor)
    function executor(resolve, reject) {
      const realFile = vfile(file);
      transformers.run(tree, realFile, realDone);
      function realDone(error, outputTree, file) {
        const resultingTree =
           (
            outputTree || tree
          );
        if (error) {
          reject(error);
        } else if (resolve) {
          resolve(resultingTree);
        } else {
          done(undefined, resultingTree, file);
        }
      }
    }
  }
  runSync(tree, file) {
    let complete = false;
    let result;
    this.run(tree, file, realDone);
    assertDone('runSync', 'run', complete);
    return result
    function realDone(error, tree) {
      bail(error);
      result = tree;
      complete = true;
    }
  }
  stringify(tree, file) {
    this.freeze();
    const realFile = vfile(file);
    const compiler = this.compiler || this.Compiler;
    assertCompiler('stringify', compiler);
    assertNode(tree);
    return compiler(tree, realFile)
  }
  use(value, ...parameters) {
    const attachers = this.attachers;
    const namespace = this.namespace;
    assertUnfrozen('use', this.frozen);
    if (value === null || value === undefined) ; else if (typeof value === 'function') {
      addPlugin(value, parameters);
    } else if (typeof value === 'object') {
      if (Array.isArray(value)) {
        addList(value);
      } else {
        addPreset(value);
      }
    } else {
      throw new TypeError('Expected usable value, not `' + value + '`')
    }
    return this
    function add(value) {
      if (typeof value === 'function') {
        addPlugin(value, []);
      } else if (typeof value === 'object') {
        if (Array.isArray(value)) {
          const [plugin, ...parameters] =
             (value);
          addPlugin(plugin, parameters);
        } else {
          addPreset(value);
        }
      } else {
        throw new TypeError('Expected usable value, not `' + value + '`')
      }
    }
    function addPreset(result) {
      if (!('plugins' in result) && !('settings' in result)) {
        throw new Error(
          'Expected usable value but received an empty preset, which is probably a mistake: presets typically come with `plugins` and sometimes with `settings`, but this has neither'
        )
      }
      addList(result.plugins);
      if (result.settings) {
        namespace.settings = extend$2(true, namespace.settings, result.settings);
      }
    }
    function addList(plugins) {
      let index = -1;
      if (plugins === null || plugins === undefined) ; else if (Array.isArray(plugins)) {
        while (++index < plugins.length) {
          const thing = plugins[index];
          add(thing);
        }
      } else {
        throw new TypeError('Expected a list of plugins, not `' + plugins + '`')
      }
    }
    function addPlugin(plugin, parameters) {
      let index = -1;
      let entryIndex = -1;
      while (++index < attachers.length) {
        if (attachers[index][0] === plugin) {
          entryIndex = index;
          break
        }
      }
      if (entryIndex === -1) {
        attachers.push([plugin, ...parameters]);
      }
      else if (parameters.length > 0) {
        let [primary, ...rest] = parameters;
        const currentPrimary = attachers[entryIndex][1];
        if (isPlainObject(currentPrimary) && isPlainObject(primary)) {
          primary = extend$2(true, currentPrimary, primary);
        }
        attachers[entryIndex] = [plugin, primary, ...rest];
      }
    }
  }
}
const unified = new Processor().freeze();
function assertParser(name, value) {
  if (typeof value !== 'function') {
    throw new TypeError('Cannot `' + name + '` without `parser`')
  }
}
function assertCompiler(name, value) {
  if (typeof value !== 'function') {
    throw new TypeError('Cannot `' + name + '` without `compiler`')
  }
}
function assertUnfrozen(name, frozen) {
  if (frozen) {
    throw new Error(
      'Cannot call `' +
        name +
        '` on a frozen processor.\nCreate a new processor first, by calling it: use `processor()` instead of `processor`.'
    )
  }
}
function assertNode(node) {
  if (!isPlainObject(node) || typeof node.type !== 'string') {
    throw new TypeError('Expected node, got `' + node + '`')
  }
}
function assertDone(name, asyncName, complete) {
  if (!complete) {
    throw new Error(
      '`' + name + '` finished async. Use `' + asyncName + '` instead'
    )
  }
}
function vfile(value) {
  return looksLikeAVFile$1(value) ? value : new VFile(value)
}
function looksLikeAVFile$1(value) {
  return Boolean(
    value &&
      typeof value === 'object' &&
      'message' in value &&
      'messages' in value
  )
}
function looksLikeAValue(value) {
  return typeof value === 'string' || isUint8Array$1(value)
}
function isUint8Array$1(value) {
  return Boolean(
    value &&
      typeof value === 'object' &&
      'byteLength' in value &&
      'byteOffset' in value
  )
}

const emptyOptions$2 = {};
function toString(value, options) {
  const settings = emptyOptions$2;
  const includeImageAlt =
    typeof settings.includeImageAlt === 'boolean'
      ? settings.includeImageAlt
      : true;
  const includeHtml =
    typeof settings.includeHtml === 'boolean' ? settings.includeHtml : true;
  return one(value, includeImageAlt, includeHtml)
}
function one(value, includeImageAlt, includeHtml) {
  if (node(value)) {
    if ('value' in value) {
      return value.type === 'html' && !includeHtml ? '' : value.value
    }
    if (includeImageAlt && 'alt' in value && value.alt) {
      return value.alt
    }
    if ('children' in value) {
      return all(value.children, includeImageAlt, includeHtml)
    }
  }
  if (Array.isArray(value)) {
    return all(value, includeImageAlt, includeHtml)
  }
  return ''
}
function all(values, includeImageAlt, includeHtml) {
  const result = [];
  let index = -1;
  while (++index < values.length) {
    result[index] = one(values[index], includeImageAlt, includeHtml);
  }
  return result.join('')
}
function node(value) {
  return Boolean(value && typeof value === 'object')
}

const characterEntities = {
  AElig: 'Æ',
  AMP: '&',
  Aacute: 'Á',
  Abreve: 'Ă',
  Acirc: 'Â',
  Acy: 'А',
  Afr: '𝔄',
  Agrave: 'À',
  Alpha: 'Α',
  Amacr: 'Ā',
  And: '⩓',
  Aogon: 'Ą',
  Aopf: '𝔸',
  ApplyFunction: '⁡',
  Aring: 'Å',
  Ascr: '𝒜',
  Assign: '≔',
  Atilde: 'Ã',
  Auml: 'Ä',
  Backslash: '∖',
  Barv: '⫧',
  Barwed: '⌆',
  Bcy: 'Б',
  Because: '∵',
  Bernoullis: 'ℬ',
  Beta: 'Β',
  Bfr: '𝔅',
  Bopf: '𝔹',
  Breve: '˘',
  Bscr: 'ℬ',
  Bumpeq: '≎',
  CHcy: 'Ч',
  COPY: '©',
  Cacute: 'Ć',
  Cap: '⋒',
  CapitalDifferentialD: 'ⅅ',
  Cayleys: 'ℭ',
  Ccaron: 'Č',
  Ccedil: 'Ç',
  Ccirc: 'Ĉ',
  Cconint: '∰',
  Cdot: 'Ċ',
  Cedilla: '¸',
  CenterDot: '·',
  Cfr: 'ℭ',
  Chi: 'Χ',
  CircleDot: '⊙',
  CircleMinus: '⊖',
  CirclePlus: '⊕',
  CircleTimes: '⊗',
  ClockwiseContourIntegral: '∲',
  CloseCurlyDoubleQuote: '”',
  CloseCurlyQuote: '’',
  Colon: '∷',
  Colone: '⩴',
  Congruent: '≡',
  Conint: '∯',
  ContourIntegral: '∮',
  Copf: 'ℂ',
  Coproduct: '∐',
  CounterClockwiseContourIntegral: '∳',
  Cross: '⨯',
  Cscr: '𝒞',
  Cup: '⋓',
  CupCap: '≍',
  DD: 'ⅅ',
  DDotrahd: '⤑',
  DJcy: 'Ђ',
  DScy: 'Ѕ',
  DZcy: 'Џ',
  Dagger: '‡',
  Darr: '↡',
  Dashv: '⫤',
  Dcaron: 'Ď',
  Dcy: 'Д',
  Del: '∇',
  Delta: 'Δ',
  Dfr: '𝔇',
  DiacriticalAcute: '´',
  DiacriticalDot: '˙',
  DiacriticalDoubleAcute: '˝',
  DiacriticalGrave: '`',
  DiacriticalTilde: '˜',
  Diamond: '⋄',
  DifferentialD: 'ⅆ',
  Dopf: '𝔻',
  Dot: '¨',
  DotDot: '⃜',
  DotEqual: '≐',
  DoubleContourIntegral: '∯',
  DoubleDot: '¨',
  DoubleDownArrow: '⇓',
  DoubleLeftArrow: '⇐',
  DoubleLeftRightArrow: '⇔',
  DoubleLeftTee: '⫤',
  DoubleLongLeftArrow: '⟸',
  DoubleLongLeftRightArrow: '⟺',
  DoubleLongRightArrow: '⟹',
  DoubleRightArrow: '⇒',
  DoubleRightTee: '⊨',
  DoubleUpArrow: '⇑',
  DoubleUpDownArrow: '⇕',
  DoubleVerticalBar: '∥',
  DownArrow: '↓',
  DownArrowBar: '⤓',
  DownArrowUpArrow: '⇵',
  DownBreve: '̑',
  DownLeftRightVector: '⥐',
  DownLeftTeeVector: '⥞',
  DownLeftVector: '↽',
  DownLeftVectorBar: '⥖',
  DownRightTeeVector: '⥟',
  DownRightVector: '⇁',
  DownRightVectorBar: '⥗',
  DownTee: '⊤',
  DownTeeArrow: '↧',
  Downarrow: '⇓',
  Dscr: '𝒟',
  Dstrok: 'Đ',
  ENG: 'Ŋ',
  ETH: 'Ð',
  Eacute: 'É',
  Ecaron: 'Ě',
  Ecirc: 'Ê',
  Ecy: 'Э',
  Edot: 'Ė',
  Efr: '𝔈',
  Egrave: 'È',
  Element: '∈',
  Emacr: 'Ē',
  EmptySmallSquare: '◻',
  EmptyVerySmallSquare: '▫',
  Eogon: 'Ę',
  Eopf: '𝔼',
  Epsilon: 'Ε',
  Equal: '⩵',
  EqualTilde: '≂',
  Equilibrium: '⇌',
  Escr: 'ℰ',
  Esim: '⩳',
  Eta: 'Η',
  Euml: 'Ë',
  Exists: '∃',
  ExponentialE: 'ⅇ',
  Fcy: 'Ф',
  Ffr: '𝔉',
  FilledSmallSquare: '◼',
  FilledVerySmallSquare: '▪',
  Fopf: '𝔽',
  ForAll: '∀',
  Fouriertrf: 'ℱ',
  Fscr: 'ℱ',
  GJcy: 'Ѓ',
  GT: '>',
  Gamma: 'Γ',
  Gammad: 'Ϝ',
  Gbreve: 'Ğ',
  Gcedil: 'Ģ',
  Gcirc: 'Ĝ',
  Gcy: 'Г',
  Gdot: 'Ġ',
  Gfr: '𝔊',
  Gg: '⋙',
  Gopf: '𝔾',
  GreaterEqual: '≥',
  GreaterEqualLess: '⋛',
  GreaterFullEqual: '≧',
  GreaterGreater: '⪢',
  GreaterLess: '≷',
  GreaterSlantEqual: '⩾',
  GreaterTilde: '≳',
  Gscr: '𝒢',
  Gt: '≫',
  HARDcy: 'Ъ',
  Hacek: 'ˇ',
  Hat: '^',
  Hcirc: 'Ĥ',
  Hfr: 'ℌ',
  HilbertSpace: 'ℋ',
  Hopf: 'ℍ',
  HorizontalLine: '─',
  Hscr: 'ℋ',
  Hstrok: 'Ħ',
  HumpDownHump: '≎',
  HumpEqual: '≏',
  IEcy: 'Е',
  IJlig: 'Ĳ',
  IOcy: 'Ё',
  Iacute: 'Í',
  Icirc: 'Î',
  Icy: 'И',
  Idot: 'İ',
  Ifr: 'ℑ',
  Igrave: 'Ì',
  Im: 'ℑ',
  Imacr: 'Ī',
  ImaginaryI: 'ⅈ',
  Implies: '⇒',
  Int: '∬',
  Integral: '∫',
  Intersection: '⋂',
  InvisibleComma: '⁣',
  InvisibleTimes: '⁢',
  Iogon: 'Į',
  Iopf: '𝕀',
  Iota: 'Ι',
  Iscr: 'ℐ',
  Itilde: 'Ĩ',
  Iukcy: 'І',
  Iuml: 'Ï',
  Jcirc: 'Ĵ',
  Jcy: 'Й',
  Jfr: '𝔍',
  Jopf: '𝕁',
  Jscr: '𝒥',
  Jsercy: 'Ј',
  Jukcy: 'Є',
  KHcy: 'Х',
  KJcy: 'Ќ',
  Kappa: 'Κ',
  Kcedil: 'Ķ',
  Kcy: 'К',
  Kfr: '𝔎',
  Kopf: '𝕂',
  Kscr: '𝒦',
  LJcy: 'Љ',
  LT: '<',
  Lacute: 'Ĺ',
  Lambda: 'Λ',
  Lang: '⟪',
  Laplacetrf: 'ℒ',
  Larr: '↞',
  Lcaron: 'Ľ',
  Lcedil: 'Ļ',
  Lcy: 'Л',
  LeftAngleBracket: '⟨',
  LeftArrow: '←',
  LeftArrowBar: '⇤',
  LeftArrowRightArrow: '⇆',
  LeftCeiling: '⌈',
  LeftDoubleBracket: '⟦',
  LeftDownTeeVector: '⥡',
  LeftDownVector: '⇃',
  LeftDownVectorBar: '⥙',
  LeftFloor: '⌊',
  LeftRightArrow: '↔',
  LeftRightVector: '⥎',
  LeftTee: '⊣',
  LeftTeeArrow: '↤',
  LeftTeeVector: '⥚',
  LeftTriangle: '⊲',
  LeftTriangleBar: '⧏',
  LeftTriangleEqual: '⊴',
  LeftUpDownVector: '⥑',
  LeftUpTeeVector: '⥠',
  LeftUpVector: '↿',
  LeftUpVectorBar: '⥘',
  LeftVector: '↼',
  LeftVectorBar: '⥒',
  Leftarrow: '⇐',
  Leftrightarrow: '⇔',
  LessEqualGreater: '⋚',
  LessFullEqual: '≦',
  LessGreater: '≶',
  LessLess: '⪡',
  LessSlantEqual: '⩽',
  LessTilde: '≲',
  Lfr: '𝔏',
  Ll: '⋘',
  Lleftarrow: '⇚',
  Lmidot: 'Ŀ',
  LongLeftArrow: '⟵',
  LongLeftRightArrow: '⟷',
  LongRightArrow: '⟶',
  Longleftarrow: '⟸',
  Longleftrightarrow: '⟺',
  Longrightarrow: '⟹',
  Lopf: '𝕃',
  LowerLeftArrow: '↙',
  LowerRightArrow: '↘',
  Lscr: 'ℒ',
  Lsh: '↰',
  Lstrok: 'Ł',
  Lt: '≪',
  Map: '⤅',
  Mcy: 'М',
  MediumSpace: ' ',
  Mellintrf: 'ℳ',
  Mfr: '𝔐',
  MinusPlus: '∓',
  Mopf: '𝕄',
  Mscr: 'ℳ',
  Mu: 'Μ',
  NJcy: 'Њ',
  Nacute: 'Ń',
  Ncaron: 'Ň',
  Ncedil: 'Ņ',
  Ncy: 'Н',
  NegativeMediumSpace: '​',
  NegativeThickSpace: '​',
  NegativeThinSpace: '​',
  NegativeVeryThinSpace: '​',
  NestedGreaterGreater: '≫',
  NestedLessLess: '≪',
  NewLine: '\n',
  Nfr: '𝔑',
  NoBreak: '⁠',
  NonBreakingSpace: ' ',
  Nopf: 'ℕ',
  Not: '⫬',
  NotCongruent: '≢',
  NotCupCap: '≭',
  NotDoubleVerticalBar: '∦',
  NotElement: '∉',
  NotEqual: '≠',
  NotEqualTilde: '≂̸',
  NotExists: '∄',
  NotGreater: '≯',
  NotGreaterEqual: '≱',
  NotGreaterFullEqual: '≧̸',
  NotGreaterGreater: '≫̸',
  NotGreaterLess: '≹',
  NotGreaterSlantEqual: '⩾̸',
  NotGreaterTilde: '≵',
  NotHumpDownHump: '≎̸',
  NotHumpEqual: '≏̸',
  NotLeftTriangle: '⋪',
  NotLeftTriangleBar: '⧏̸',
  NotLeftTriangleEqual: '⋬',
  NotLess: '≮',
  NotLessEqual: '≰',
  NotLessGreater: '≸',
  NotLessLess: '≪̸',
  NotLessSlantEqual: '⩽̸',
  NotLessTilde: '≴',
  NotNestedGreaterGreater: '⪢̸',
  NotNestedLessLess: '⪡̸',
  NotPrecedes: '⊀',
  NotPrecedesEqual: '⪯̸',
  NotPrecedesSlantEqual: '⋠',
  NotReverseElement: '∌',
  NotRightTriangle: '⋫',
  NotRightTriangleBar: '⧐̸',
  NotRightTriangleEqual: '⋭',
  NotSquareSubset: '⊏̸',
  NotSquareSubsetEqual: '⋢',
  NotSquareSuperset: '⊐̸',
  NotSquareSupersetEqual: '⋣',
  NotSubset: '⊂⃒',
  NotSubsetEqual: '⊈',
  NotSucceeds: '⊁',
  NotSucceedsEqual: '⪰̸',
  NotSucceedsSlantEqual: '⋡',
  NotSucceedsTilde: '≿̸',
  NotSuperset: '⊃⃒',
  NotSupersetEqual: '⊉',
  NotTilde: '≁',
  NotTildeEqual: '≄',
  NotTildeFullEqual: '≇',
  NotTildeTilde: '≉',
  NotVerticalBar: '∤',
  Nscr: '𝒩',
  Ntilde: 'Ñ',
  Nu: 'Ν',
  OElig: 'Œ',
  Oacute: 'Ó',
  Ocirc: 'Ô',
  Ocy: 'О',
  Odblac: 'Ő',
  Ofr: '𝔒',
  Ograve: 'Ò',
  Omacr: 'Ō',
  Omega: 'Ω',
  Omicron: 'Ο',
  Oopf: '𝕆',
  OpenCurlyDoubleQuote: '“',
  OpenCurlyQuote: '‘',
  Or: '⩔',
  Oscr: '𝒪',
  Oslash: 'Ø',
  Otilde: 'Õ',
  Otimes: '⨷',
  Ouml: 'Ö',
  OverBar: '‾',
  OverBrace: '⏞',
  OverBracket: '⎴',
  OverParenthesis: '⏜',
  PartialD: '∂',
  Pcy: 'П',
  Pfr: '𝔓',
  Phi: 'Φ',
  Pi: 'Π',
  PlusMinus: '±',
  Poincareplane: 'ℌ',
  Popf: 'ℙ',
  Pr: '⪻',
  Precedes: '≺',
  PrecedesEqual: '⪯',
  PrecedesSlantEqual: '≼',
  PrecedesTilde: '≾',
  Prime: '″',
  Product: '∏',
  Proportion: '∷',
  Proportional: '∝',
  Pscr: '𝒫',
  Psi: 'Ψ',
  QUOT: '"',
  Qfr: '𝔔',
  Qopf: 'ℚ',
  Qscr: '𝒬',
  RBarr: '⤐',
  REG: '®',
  Racute: 'Ŕ',
  Rang: '⟫',
  Rarr: '↠',
  Rarrtl: '⤖',
  Rcaron: 'Ř',
  Rcedil: 'Ŗ',
  Rcy: 'Р',
  Re: 'ℜ',
  ReverseElement: '∋',
  ReverseEquilibrium: '⇋',
  ReverseUpEquilibrium: '⥯',
  Rfr: 'ℜ',
  Rho: 'Ρ',
  RightAngleBracket: '⟩',
  RightArrow: '→',
  RightArrowBar: '⇥',
  RightArrowLeftArrow: '⇄',
  RightCeiling: '⌉',
  RightDoubleBracket: '⟧',
  RightDownTeeVector: '⥝',
  RightDownVector: '⇂',
  RightDownVectorBar: '⥕',
  RightFloor: '⌋',
  RightTee: '⊢',
  RightTeeArrow: '↦',
  RightTeeVector: '⥛',
  RightTriangle: '⊳',
  RightTriangleBar: '⧐',
  RightTriangleEqual: '⊵',
  RightUpDownVector: '⥏',
  RightUpTeeVector: '⥜',
  RightUpVector: '↾',
  RightUpVectorBar: '⥔',
  RightVector: '⇀',
  RightVectorBar: '⥓',
  Rightarrow: '⇒',
  Ropf: 'ℝ',
  RoundImplies: '⥰',
  Rrightarrow: '⇛',
  Rscr: 'ℛ',
  Rsh: '↱',
  RuleDelayed: '⧴',
  SHCHcy: 'Щ',
  SHcy: 'Ш',
  SOFTcy: 'Ь',
  Sacute: 'Ś',
  Sc: '⪼',
  Scaron: 'Š',
  Scedil: 'Ş',
  Scirc: 'Ŝ',
  Scy: 'С',
  Sfr: '𝔖',
  ShortDownArrow: '↓',
  ShortLeftArrow: '←',
  ShortRightArrow: '→',
  ShortUpArrow: '↑',
  Sigma: 'Σ',
  SmallCircle: '∘',
  Sopf: '𝕊',
  Sqrt: '√',
  Square: '□',
  SquareIntersection: '⊓',
  SquareSubset: '⊏',
  SquareSubsetEqual: '⊑',
  SquareSuperset: '⊐',
  SquareSupersetEqual: '⊒',
  SquareUnion: '⊔',
  Sscr: '𝒮',
  Star: '⋆',
  Sub: '⋐',
  Subset: '⋐',
  SubsetEqual: '⊆',
  Succeeds: '≻',
  SucceedsEqual: '⪰',
  SucceedsSlantEqual: '≽',
  SucceedsTilde: '≿',
  SuchThat: '∋',
  Sum: '∑',
  Sup: '⋑',
  Superset: '⊃',
  SupersetEqual: '⊇',
  Supset: '⋑',
  THORN: 'Þ',
  TRADE: '™',
  TSHcy: 'Ћ',
  TScy: 'Ц',
  Tab: '\t',
  Tau: 'Τ',
  Tcaron: 'Ť',
  Tcedil: 'Ţ',
  Tcy: 'Т',
  Tfr: '𝔗',
  Therefore: '∴',
  Theta: 'Θ',
  ThickSpace: '  ',
  ThinSpace: ' ',
  Tilde: '∼',
  TildeEqual: '≃',
  TildeFullEqual: '≅',
  TildeTilde: '≈',
  Topf: '𝕋',
  TripleDot: '⃛',
  Tscr: '𝒯',
  Tstrok: 'Ŧ',
  Uacute: 'Ú',
  Uarr: '↟',
  Uarrocir: '⥉',
  Ubrcy: 'Ў',
  Ubreve: 'Ŭ',
  Ucirc: 'Û',
  Ucy: 'У',
  Udblac: 'Ű',
  Ufr: '𝔘',
  Ugrave: 'Ù',
  Umacr: 'Ū',
  UnderBar: '_',
  UnderBrace: '⏟',
  UnderBracket: '⎵',
  UnderParenthesis: '⏝',
  Union: '⋃',
  UnionPlus: '⊎',
  Uogon: 'Ų',
  Uopf: '𝕌',
  UpArrow: '↑',
  UpArrowBar: '⤒',
  UpArrowDownArrow: '⇅',
  UpDownArrow: '↕',
  UpEquilibrium: '⥮',
  UpTee: '⊥',
  UpTeeArrow: '↥',
  Uparrow: '⇑',
  Updownarrow: '⇕',
  UpperLeftArrow: '↖',
  UpperRightArrow: '↗',
  Upsi: 'ϒ',
  Upsilon: 'Υ',
  Uring: 'Ů',
  Uscr: '𝒰',
  Utilde: 'Ũ',
  Uuml: 'Ü',
  VDash: '⊫',
  Vbar: '⫫',
  Vcy: 'В',
  Vdash: '⊩',
  Vdashl: '⫦',
  Vee: '⋁',
  Verbar: '‖',
  Vert: '‖',
  VerticalBar: '∣',
  VerticalLine: '|',
  VerticalSeparator: '❘',
  VerticalTilde: '≀',
  VeryThinSpace: ' ',
  Vfr: '𝔙',
  Vopf: '𝕍',
  Vscr: '𝒱',
  Vvdash: '⊪',
  Wcirc: 'Ŵ',
  Wedge: '⋀',
  Wfr: '𝔚',
  Wopf: '𝕎',
  Wscr: '𝒲',
  Xfr: '𝔛',
  Xi: 'Ξ',
  Xopf: '𝕏',
  Xscr: '𝒳',
  YAcy: 'Я',
  YIcy: 'Ї',
  YUcy: 'Ю',
  Yacute: 'Ý',
  Ycirc: 'Ŷ',
  Ycy: 'Ы',
  Yfr: '𝔜',
  Yopf: '𝕐',
  Yscr: '𝒴',
  Yuml: 'Ÿ',
  ZHcy: 'Ж',
  Zacute: 'Ź',
  Zcaron: 'Ž',
  Zcy: 'З',
  Zdot: 'Ż',
  ZeroWidthSpace: '​',
  Zeta: 'Ζ',
  Zfr: 'ℨ',
  Zopf: 'ℤ',
  Zscr: '𝒵',
  aacute: 'á',
  abreve: 'ă',
  ac: '∾',
  acE: '∾̳',
  acd: '∿',
  acirc: 'â',
  acute: '´',
  acy: 'а',
  aelig: 'æ',
  af: '⁡',
  afr: '𝔞',
  agrave: 'à',
  alefsym: 'ℵ',
  aleph: 'ℵ',
  alpha: 'α',
  amacr: 'ā',
  amalg: '⨿',
  amp: '&',
  and: '∧',
  andand: '⩕',
  andd: '⩜',
  andslope: '⩘',
  andv: '⩚',
  ang: '∠',
  ange: '⦤',
  angle: '∠',
  angmsd: '∡',
  angmsdaa: '⦨',
  angmsdab: '⦩',
  angmsdac: '⦪',
  angmsdad: '⦫',
  angmsdae: '⦬',
  angmsdaf: '⦭',
  angmsdag: '⦮',
  angmsdah: '⦯',
  angrt: '∟',
  angrtvb: '⊾',
  angrtvbd: '⦝',
  angsph: '∢',
  angst: 'Å',
  angzarr: '⍼',
  aogon: 'ą',
  aopf: '𝕒',
  ap: '≈',
  apE: '⩰',
  apacir: '⩯',
  ape: '≊',
  apid: '≋',
  apos: "'",
  approx: '≈',
  approxeq: '≊',
  aring: 'å',
  ascr: '𝒶',
  ast: '*',
  asymp: '≈',
  asympeq: '≍',
  atilde: 'ã',
  auml: 'ä',
  awconint: '∳',
  awint: '⨑',
  bNot: '⫭',
  backcong: '≌',
  backepsilon: '϶',
  backprime: '‵',
  backsim: '∽',
  backsimeq: '⋍',
  barvee: '⊽',
  barwed: '⌅',
  barwedge: '⌅',
  bbrk: '⎵',
  bbrktbrk: '⎶',
  bcong: '≌',
  bcy: 'б',
  bdquo: '„',
  becaus: '∵',
  because: '∵',
  bemptyv: '⦰',
  bepsi: '϶',
  bernou: 'ℬ',
  beta: 'β',
  beth: 'ℶ',
  between: '≬',
  bfr: '𝔟',
  bigcap: '⋂',
  bigcirc: '◯',
  bigcup: '⋃',
  bigodot: '⨀',
  bigoplus: '⨁',
  bigotimes: '⨂',
  bigsqcup: '⨆',
  bigstar: '★',
  bigtriangledown: '▽',
  bigtriangleup: '△',
  biguplus: '⨄',
  bigvee: '⋁',
  bigwedge: '⋀',
  bkarow: '⤍',
  blacklozenge: '⧫',
  blacksquare: '▪',
  blacktriangle: '▴',
  blacktriangledown: '▾',
  blacktriangleleft: '◂',
  blacktriangleright: '▸',
  blank: '␣',
  blk12: '▒',
  blk14: '░',
  blk34: '▓',
  block: '█',
  bne: '=⃥',
  bnequiv: '≡⃥',
  bnot: '⌐',
  bopf: '𝕓',
  bot: '⊥',
  bottom: '⊥',
  bowtie: '⋈',
  boxDL: '╗',
  boxDR: '╔',
  boxDl: '╖',
  boxDr: '╓',
  boxH: '═',
  boxHD: '╦',
  boxHU: '╩',
  boxHd: '╤',
  boxHu: '╧',
  boxUL: '╝',
  boxUR: '╚',
  boxUl: '╜',
  boxUr: '╙',
  boxV: '║',
  boxVH: '╬',
  boxVL: '╣',
  boxVR: '╠',
  boxVh: '╫',
  boxVl: '╢',
  boxVr: '╟',
  boxbox: '⧉',
  boxdL: '╕',
  boxdR: '╒',
  boxdl: '┐',
  boxdr: '┌',
  boxh: '─',
  boxhD: '╥',
  boxhU: '╨',
  boxhd: '┬',
  boxhu: '┴',
  boxminus: '⊟',
  boxplus: '⊞',
  boxtimes: '⊠',
  boxuL: '╛',
  boxuR: '╘',
  boxul: '┘',
  boxur: '└',
  boxv: '│',
  boxvH: '╪',
  boxvL: '╡',
  boxvR: '╞',
  boxvh: '┼',
  boxvl: '┤',
  boxvr: '├',
  bprime: '‵',
  breve: '˘',
  brvbar: '¦',
  bscr: '𝒷',
  bsemi: '⁏',
  bsim: '∽',
  bsime: '⋍',
  bsol: '\\',
  bsolb: '⧅',
  bsolhsub: '⟈',
  bull: '•',
  bullet: '•',
  bump: '≎',
  bumpE: '⪮',
  bumpe: '≏',
  bumpeq: '≏',
  cacute: 'ć',
  cap: '∩',
  capand: '⩄',
  capbrcup: '⩉',
  capcap: '⩋',
  capcup: '⩇',
  capdot: '⩀',
  caps: '∩︀',
  caret: '⁁',
  caron: 'ˇ',
  ccaps: '⩍',
  ccaron: 'č',
  ccedil: 'ç',
  ccirc: 'ĉ',
  ccups: '⩌',
  ccupssm: '⩐',
  cdot: 'ċ',
  cedil: '¸',
  cemptyv: '⦲',
  cent: '¢',
  centerdot: '·',
  cfr: '𝔠',
  chcy: 'ч',
  check: '✓',
  checkmark: '✓',
  chi: 'χ',
  cir: '○',
  cirE: '⧃',
  circ: 'ˆ',
  circeq: '≗',
  circlearrowleft: '↺',
  circlearrowright: '↻',
  circledR: '®',
  circledS: 'Ⓢ',
  circledast: '⊛',
  circledcirc: '⊚',
  circleddash: '⊝',
  cire: '≗',
  cirfnint: '⨐',
  cirmid: '⫯',
  cirscir: '⧂',
  clubs: '♣',
  clubsuit: '♣',
  colon: ':',
  colone: '≔',
  coloneq: '≔',
  comma: ',',
  commat: '@',
  comp: '∁',
  compfn: '∘',
  complement: '∁',
  complexes: 'ℂ',
  cong: '≅',
  congdot: '⩭',
  conint: '∮',
  copf: '𝕔',
  coprod: '∐',
  copy: '©',
  copysr: '℗',
  crarr: '↵',
  cross: '✗',
  cscr: '𝒸',
  csub: '⫏',
  csube: '⫑',
  csup: '⫐',
  csupe: '⫒',
  ctdot: '⋯',
  cudarrl: '⤸',
  cudarrr: '⤵',
  cuepr: '⋞',
  cuesc: '⋟',
  cularr: '↶',
  cularrp: '⤽',
  cup: '∪',
  cupbrcap: '⩈',
  cupcap: '⩆',
  cupcup: '⩊',
  cupdot: '⊍',
  cupor: '⩅',
  cups: '∪︀',
  curarr: '↷',
  curarrm: '⤼',
  curlyeqprec: '⋞',
  curlyeqsucc: '⋟',
  curlyvee: '⋎',
  curlywedge: '⋏',
  curren: '¤',
  curvearrowleft: '↶',
  curvearrowright: '↷',
  cuvee: '⋎',
  cuwed: '⋏',
  cwconint: '∲',
  cwint: '∱',
  cylcty: '⌭',
  dArr: '⇓',
  dHar: '⥥',
  dagger: '†',
  daleth: 'ℸ',
  darr: '↓',
  dash: '‐',
  dashv: '⊣',
  dbkarow: '⤏',
  dblac: '˝',
  dcaron: 'ď',
  dcy: 'д',
  dd: 'ⅆ',
  ddagger: '‡',
  ddarr: '⇊',
  ddotseq: '⩷',
  deg: '°',
  delta: 'δ',
  demptyv: '⦱',
  dfisht: '⥿',
  dfr: '𝔡',
  dharl: '⇃',
  dharr: '⇂',
  diam: '⋄',
  diamond: '⋄',
  diamondsuit: '♦',
  diams: '♦',
  die: '¨',
  digamma: 'ϝ',
  disin: '⋲',
  div: '÷',
  divide: '÷',
  divideontimes: '⋇',
  divonx: '⋇',
  djcy: 'ђ',
  dlcorn: '⌞',
  dlcrop: '⌍',
  dollar: '$',
  dopf: '𝕕',
  dot: '˙',
  doteq: '≐',
  doteqdot: '≑',
  dotminus: '∸',
  dotplus: '∔',
  dotsquare: '⊡',
  doublebarwedge: '⌆',
  downarrow: '↓',
  downdownarrows: '⇊',
  downharpoonleft: '⇃',
  downharpoonright: '⇂',
  drbkarow: '⤐',
  drcorn: '⌟',
  drcrop: '⌌',
  dscr: '𝒹',
  dscy: 'ѕ',
  dsol: '⧶',
  dstrok: 'đ',
  dtdot: '⋱',
  dtri: '▿',
  dtrif: '▾',
  duarr: '⇵',
  duhar: '⥯',
  dwangle: '⦦',
  dzcy: 'џ',
  dzigrarr: '⟿',
  eDDot: '⩷',
  eDot: '≑',
  eacute: 'é',
  easter: '⩮',
  ecaron: 'ě',
  ecir: '≖',
  ecirc: 'ê',
  ecolon: '≕',
  ecy: 'э',
  edot: 'ė',
  ee: 'ⅇ',
  efDot: '≒',
  efr: '𝔢',
  eg: '⪚',
  egrave: 'è',
  egs: '⪖',
  egsdot: '⪘',
  el: '⪙',
  elinters: '⏧',
  ell: 'ℓ',
  els: '⪕',
  elsdot: '⪗',
  emacr: 'ē',
  empty: '∅',
  emptyset: '∅',
  emptyv: '∅',
  emsp13: ' ',
  emsp14: ' ',
  emsp: ' ',
  eng: 'ŋ',
  ensp: ' ',
  eogon: 'ę',
  eopf: '𝕖',
  epar: '⋕',
  eparsl: '⧣',
  eplus: '⩱',
  epsi: 'ε',
  epsilon: 'ε',
  epsiv: 'ϵ',
  eqcirc: '≖',
  eqcolon: '≕',
  eqsim: '≂',
  eqslantgtr: '⪖',
  eqslantless: '⪕',
  equals: '=',
  equest: '≟',
  equiv: '≡',
  equivDD: '⩸',
  eqvparsl: '⧥',
  erDot: '≓',
  erarr: '⥱',
  escr: 'ℯ',
  esdot: '≐',
  esim: '≂',
  eta: 'η',
  eth: 'ð',
  euml: 'ë',
  euro: '€',
  excl: '!',
  exist: '∃',
  expectation: 'ℰ',
  exponentiale: 'ⅇ',
  fallingdotseq: '≒',
  fcy: 'ф',
  female: '♀',
  ffilig: 'ﬃ',
  fflig: 'ﬀ',
  ffllig: 'ﬄ',
  ffr: '𝔣',
  filig: 'ﬁ',
  fjlig: 'fj',
  flat: '♭',
  fllig: 'ﬂ',
  fltns: '▱',
  fnof: 'ƒ',
  fopf: '𝕗',
  forall: '∀',
  fork: '⋔',
  forkv: '⫙',
  fpartint: '⨍',
  frac12: '½',
  frac13: '⅓',
  frac14: '¼',
  frac15: '⅕',
  frac16: '⅙',
  frac18: '⅛',
  frac23: '⅔',
  frac25: '⅖',
  frac34: '¾',
  frac35: '⅗',
  frac38: '⅜',
  frac45: '⅘',
  frac56: '⅚',
  frac58: '⅝',
  frac78: '⅞',
  frasl: '⁄',
  frown: '⌢',
  fscr: '𝒻',
  gE: '≧',
  gEl: '⪌',
  gacute: 'ǵ',
  gamma: 'γ',
  gammad: 'ϝ',
  gap: '⪆',
  gbreve: 'ğ',
  gcirc: 'ĝ',
  gcy: 'г',
  gdot: 'ġ',
  ge: '≥',
  gel: '⋛',
  geq: '≥',
  geqq: '≧',
  geqslant: '⩾',
  ges: '⩾',
  gescc: '⪩',
  gesdot: '⪀',
  gesdoto: '⪂',
  gesdotol: '⪄',
  gesl: '⋛︀',
  gesles: '⪔',
  gfr: '𝔤',
  gg: '≫',
  ggg: '⋙',
  gimel: 'ℷ',
  gjcy: 'ѓ',
  gl: '≷',
  glE: '⪒',
  gla: '⪥',
  glj: '⪤',
  gnE: '≩',
  gnap: '⪊',
  gnapprox: '⪊',
  gne: '⪈',
  gneq: '⪈',
  gneqq: '≩',
  gnsim: '⋧',
  gopf: '𝕘',
  grave: '`',
  gscr: 'ℊ',
  gsim: '≳',
  gsime: '⪎',
  gsiml: '⪐',
  gt: '>',
  gtcc: '⪧',
  gtcir: '⩺',
  gtdot: '⋗',
  gtlPar: '⦕',
  gtquest: '⩼',
  gtrapprox: '⪆',
  gtrarr: '⥸',
  gtrdot: '⋗',
  gtreqless: '⋛',
  gtreqqless: '⪌',
  gtrless: '≷',
  gtrsim: '≳',
  gvertneqq: '≩︀',
  gvnE: '≩︀',
  hArr: '⇔',
  hairsp: ' ',
  half: '½',
  hamilt: 'ℋ',
  hardcy: 'ъ',
  harr: '↔',
  harrcir: '⥈',
  harrw: '↭',
  hbar: 'ℏ',
  hcirc: 'ĥ',
  hearts: '♥',
  heartsuit: '♥',
  hellip: '…',
  hercon: '⊹',
  hfr: '𝔥',
  hksearow: '⤥',
  hkswarow: '⤦',
  hoarr: '⇿',
  homtht: '∻',
  hookleftarrow: '↩',
  hookrightarrow: '↪',
  hopf: '𝕙',
  horbar: '―',
  hscr: '𝒽',
  hslash: 'ℏ',
  hstrok: 'ħ',
  hybull: '⁃',
  hyphen: '‐',
  iacute: 'í',
  ic: '⁣',
  icirc: 'î',
  icy: 'и',
  iecy: 'е',
  iexcl: '¡',
  iff: '⇔',
  ifr: '𝔦',
  igrave: 'ì',
  ii: 'ⅈ',
  iiiint: '⨌',
  iiint: '∭',
  iinfin: '⧜',
  iiota: '℩',
  ijlig: 'ĳ',
  imacr: 'ī',
  image: 'ℑ',
  imagline: 'ℐ',
  imagpart: 'ℑ',
  imath: 'ı',
  imof: '⊷',
  imped: 'Ƶ',
  in: '∈',
  incare: '℅',
  infin: '∞',
  infintie: '⧝',
  inodot: 'ı',
  int: '∫',
  intcal: '⊺',
  integers: 'ℤ',
  intercal: '⊺',
  intlarhk: '⨗',
  intprod: '⨼',
  iocy: 'ё',
  iogon: 'į',
  iopf: '𝕚',
  iota: 'ι',
  iprod: '⨼',
  iquest: '¿',
  iscr: '𝒾',
  isin: '∈',
  isinE: '⋹',
  isindot: '⋵',
  isins: '⋴',
  isinsv: '⋳',
  isinv: '∈',
  it: '⁢',
  itilde: 'ĩ',
  iukcy: 'і',
  iuml: 'ï',
  jcirc: 'ĵ',
  jcy: 'й',
  jfr: '𝔧',
  jmath: 'ȷ',
  jopf: '𝕛',
  jscr: '𝒿',
  jsercy: 'ј',
  jukcy: 'є',
  kappa: 'κ',
  kappav: 'ϰ',
  kcedil: 'ķ',
  kcy: 'к',
  kfr: '𝔨',
  kgreen: 'ĸ',
  khcy: 'х',
  kjcy: 'ќ',
  kopf: '𝕜',
  kscr: '𝓀',
  lAarr: '⇚',
  lArr: '⇐',
  lAtail: '⤛',
  lBarr: '⤎',
  lE: '≦',
  lEg: '⪋',
  lHar: '⥢',
  lacute: 'ĺ',
  laemptyv: '⦴',
  lagran: 'ℒ',
  lambda: 'λ',
  lang: '⟨',
  langd: '⦑',
  langle: '⟨',
  lap: '⪅',
  laquo: '«',
  larr: '←',
  larrb: '⇤',
  larrbfs: '⤟',
  larrfs: '⤝',
  larrhk: '↩',
  larrlp: '↫',
  larrpl: '⤹',
  larrsim: '⥳',
  larrtl: '↢',
  lat: '⪫',
  latail: '⤙',
  late: '⪭',
  lates: '⪭︀',
  lbarr: '⤌',
  lbbrk: '❲',
  lbrace: '{',
  lbrack: '[',
  lbrke: '⦋',
  lbrksld: '⦏',
  lbrkslu: '⦍',
  lcaron: 'ľ',
  lcedil: 'ļ',
  lceil: '⌈',
  lcub: '{',
  lcy: 'л',
  ldca: '⤶',
  ldquo: '“',
  ldquor: '„',
  ldrdhar: '⥧',
  ldrushar: '⥋',
  ldsh: '↲',
  le: '≤',
  leftarrow: '←',
  leftarrowtail: '↢',
  leftharpoondown: '↽',
  leftharpoonup: '↼',
  leftleftarrows: '⇇',
  leftrightarrow: '↔',
  leftrightarrows: '⇆',
  leftrightharpoons: '⇋',
  leftrightsquigarrow: '↭',
  leftthreetimes: '⋋',
  leg: '⋚',
  leq: '≤',
  leqq: '≦',
  leqslant: '⩽',
  les: '⩽',
  lescc: '⪨',
  lesdot: '⩿',
  lesdoto: '⪁',
  lesdotor: '⪃',
  lesg: '⋚︀',
  lesges: '⪓',
  lessapprox: '⪅',
  lessdot: '⋖',
  lesseqgtr: '⋚',
  lesseqqgtr: '⪋',
  lessgtr: '≶',
  lesssim: '≲',
  lfisht: '⥼',
  lfloor: '⌊',
  lfr: '𝔩',
  lg: '≶',
  lgE: '⪑',
  lhard: '↽',
  lharu: '↼',
  lharul: '⥪',
  lhblk: '▄',
  ljcy: 'љ',
  ll: '≪',
  llarr: '⇇',
  llcorner: '⌞',
  llhard: '⥫',
  lltri: '◺',
  lmidot: 'ŀ',
  lmoust: '⎰',
  lmoustache: '⎰',
  lnE: '≨',
  lnap: '⪉',
  lnapprox: '⪉',
  lne: '⪇',
  lneq: '⪇',
  lneqq: '≨',
  lnsim: '⋦',
  loang: '⟬',
  loarr: '⇽',
  lobrk: '⟦',
  longleftarrow: '⟵',
  longleftrightarrow: '⟷',
  longmapsto: '⟼',
  longrightarrow: '⟶',
  looparrowleft: '↫',
  looparrowright: '↬',
  lopar: '⦅',
  lopf: '𝕝',
  loplus: '⨭',
  lotimes: '⨴',
  lowast: '∗',
  lowbar: '_',
  loz: '◊',
  lozenge: '◊',
  lozf: '⧫',
  lpar: '(',
  lparlt: '⦓',
  lrarr: '⇆',
  lrcorner: '⌟',
  lrhar: '⇋',
  lrhard: '⥭',
  lrm: '‎',
  lrtri: '⊿',
  lsaquo: '‹',
  lscr: '𝓁',
  lsh: '↰',
  lsim: '≲',
  lsime: '⪍',
  lsimg: '⪏',
  lsqb: '[',
  lsquo: '‘',
  lsquor: '‚',
  lstrok: 'ł',
  lt: '<',
  ltcc: '⪦',
  ltcir: '⩹',
  ltdot: '⋖',
  lthree: '⋋',
  ltimes: '⋉',
  ltlarr: '⥶',
  ltquest: '⩻',
  ltrPar: '⦖',
  ltri: '◃',
  ltrie: '⊴',
  ltrif: '◂',
  lurdshar: '⥊',
  luruhar: '⥦',
  lvertneqq: '≨︀',
  lvnE: '≨︀',
  mDDot: '∺',
  macr: '¯',
  male: '♂',
  malt: '✠',
  maltese: '✠',
  map: '↦',
  mapsto: '↦',
  mapstodown: '↧',
  mapstoleft: '↤',
  mapstoup: '↥',
  marker: '▮',
  mcomma: '⨩',
  mcy: 'м',
  mdash: '—',
  measuredangle: '∡',
  mfr: '𝔪',
  mho: '℧',
  micro: 'µ',
  mid: '∣',
  midast: '*',
  midcir: '⫰',
  middot: '·',
  minus: '−',
  minusb: '⊟',
  minusd: '∸',
  minusdu: '⨪',
  mlcp: '⫛',
  mldr: '…',
  mnplus: '∓',
  models: '⊧',
  mopf: '𝕞',
  mp: '∓',
  mscr: '𝓂',
  mstpos: '∾',
  mu: 'μ',
  multimap: '⊸',
  mumap: '⊸',
  nGg: '⋙̸',
  nGt: '≫⃒',
  nGtv: '≫̸',
  nLeftarrow: '⇍',
  nLeftrightarrow: '⇎',
  nLl: '⋘̸',
  nLt: '≪⃒',
  nLtv: '≪̸',
  nRightarrow: '⇏',
  nVDash: '⊯',
  nVdash: '⊮',
  nabla: '∇',
  nacute: 'ń',
  nang: '∠⃒',
  nap: '≉',
  napE: '⩰̸',
  napid: '≋̸',
  napos: 'ŉ',
  napprox: '≉',
  natur: '♮',
  natural: '♮',
  naturals: 'ℕ',
  nbsp: ' ',
  nbump: '≎̸',
  nbumpe: '≏̸',
  ncap: '⩃',
  ncaron: 'ň',
  ncedil: 'ņ',
  ncong: '≇',
  ncongdot: '⩭̸',
  ncup: '⩂',
  ncy: 'н',
  ndash: '–',
  ne: '≠',
  neArr: '⇗',
  nearhk: '⤤',
  nearr: '↗',
  nearrow: '↗',
  nedot: '≐̸',
  nequiv: '≢',
  nesear: '⤨',
  nesim: '≂̸',
  nexist: '∄',
  nexists: '∄',
  nfr: '𝔫',
  ngE: '≧̸',
  nge: '≱',
  ngeq: '≱',
  ngeqq: '≧̸',
  ngeqslant: '⩾̸',
  nges: '⩾̸',
  ngsim: '≵',
  ngt: '≯',
  ngtr: '≯',
  nhArr: '⇎',
  nharr: '↮',
  nhpar: '⫲',
  ni: '∋',
  nis: '⋼',
  nisd: '⋺',
  niv: '∋',
  njcy: 'њ',
  nlArr: '⇍',
  nlE: '≦̸',
  nlarr: '↚',
  nldr: '‥',
  nle: '≰',
  nleftarrow: '↚',
  nleftrightarrow: '↮',
  nleq: '≰',
  nleqq: '≦̸',
  nleqslant: '⩽̸',
  nles: '⩽̸',
  nless: '≮',
  nlsim: '≴',
  nlt: '≮',
  nltri: '⋪',
  nltrie: '⋬',
  nmid: '∤',
  nopf: '𝕟',
  not: '¬',
  notin: '∉',
  notinE: '⋹̸',
  notindot: '⋵̸',
  notinva: '∉',
  notinvb: '⋷',
  notinvc: '⋶',
  notni: '∌',
  notniva: '∌',
  notnivb: '⋾',
  notnivc: '⋽',
  npar: '∦',
  nparallel: '∦',
  nparsl: '⫽⃥',
  npart: '∂̸',
  npolint: '⨔',
  npr: '⊀',
  nprcue: '⋠',
  npre: '⪯̸',
  nprec: '⊀',
  npreceq: '⪯̸',
  nrArr: '⇏',
  nrarr: '↛',
  nrarrc: '⤳̸',
  nrarrw: '↝̸',
  nrightarrow: '↛',
  nrtri: '⋫',
  nrtrie: '⋭',
  nsc: '⊁',
  nsccue: '⋡',
  nsce: '⪰̸',
  nscr: '𝓃',
  nshortmid: '∤',
  nshortparallel: '∦',
  nsim: '≁',
  nsime: '≄',
  nsimeq: '≄',
  nsmid: '∤',
  nspar: '∦',
  nsqsube: '⋢',
  nsqsupe: '⋣',
  nsub: '⊄',
  nsubE: '⫅̸',
  nsube: '⊈',
  nsubset: '⊂⃒',
  nsubseteq: '⊈',
  nsubseteqq: '⫅̸',
  nsucc: '⊁',
  nsucceq: '⪰̸',
  nsup: '⊅',
  nsupE: '⫆̸',
  nsupe: '⊉',
  nsupset: '⊃⃒',
  nsupseteq: '⊉',
  nsupseteqq: '⫆̸',
  ntgl: '≹',
  ntilde: 'ñ',
  ntlg: '≸',
  ntriangleleft: '⋪',
  ntrianglelefteq: '⋬',
  ntriangleright: '⋫',
  ntrianglerighteq: '⋭',
  nu: 'ν',
  num: '#',
  numero: '№',
  numsp: ' ',
  nvDash: '⊭',
  nvHarr: '⤄',
  nvap: '≍⃒',
  nvdash: '⊬',
  nvge: '≥⃒',
  nvgt: '>⃒',
  nvinfin: '⧞',
  nvlArr: '⤂',
  nvle: '≤⃒',
  nvlt: '<⃒',
  nvltrie: '⊴⃒',
  nvrArr: '⤃',
  nvrtrie: '⊵⃒',
  nvsim: '∼⃒',
  nwArr: '⇖',
  nwarhk: '⤣',
  nwarr: '↖',
  nwarrow: '↖',
  nwnear: '⤧',
  oS: 'Ⓢ',
  oacute: 'ó',
  oast: '⊛',
  ocir: '⊚',
  ocirc: 'ô',
  ocy: 'о',
  odash: '⊝',
  odblac: 'ő',
  odiv: '⨸',
  odot: '⊙',
  odsold: '⦼',
  oelig: 'œ',
  ofcir: '⦿',
  ofr: '𝔬',
  ogon: '˛',
  ograve: 'ò',
  ogt: '⧁',
  ohbar: '⦵',
  ohm: 'Ω',
  oint: '∮',
  olarr: '↺',
  olcir: '⦾',
  olcross: '⦻',
  oline: '‾',
  olt: '⧀',
  omacr: 'ō',
  omega: 'ω',
  omicron: 'ο',
  omid: '⦶',
  ominus: '⊖',
  oopf: '𝕠',
  opar: '⦷',
  operp: '⦹',
  oplus: '⊕',
  or: '∨',
  orarr: '↻',
  ord: '⩝',
  order: 'ℴ',
  orderof: 'ℴ',
  ordf: 'ª',
  ordm: 'º',
  origof: '⊶',
  oror: '⩖',
  orslope: '⩗',
  orv: '⩛',
  oscr: 'ℴ',
  oslash: 'ø',
  osol: '⊘',
  otilde: 'õ',
  otimes: '⊗',
  otimesas: '⨶',
  ouml: 'ö',
  ovbar: '⌽',
  par: '∥',
  para: '¶',
  parallel: '∥',
  parsim: '⫳',
  parsl: '⫽',
  part: '∂',
  pcy: 'п',
  percnt: '%',
  period: '.',
  permil: '‰',
  perp: '⊥',
  pertenk: '‱',
  pfr: '𝔭',
  phi: 'φ',
  phiv: 'ϕ',
  phmmat: 'ℳ',
  phone: '☎',
  pi: 'π',
  pitchfork: '⋔',
  piv: 'ϖ',
  planck: 'ℏ',
  planckh: 'ℎ',
  plankv: 'ℏ',
  plus: '+',
  plusacir: '⨣',
  plusb: '⊞',
  pluscir: '⨢',
  plusdo: '∔',
  plusdu: '⨥',
  pluse: '⩲',
  plusmn: '±',
  plussim: '⨦',
  plustwo: '⨧',
  pm: '±',
  pointint: '⨕',
  popf: '𝕡',
  pound: '£',
  pr: '≺',
  prE: '⪳',
  prap: '⪷',
  prcue: '≼',
  pre: '⪯',
  prec: '≺',
  precapprox: '⪷',
  preccurlyeq: '≼',
  preceq: '⪯',
  precnapprox: '⪹',
  precneqq: '⪵',
  precnsim: '⋨',
  precsim: '≾',
  prime: '′',
  primes: 'ℙ',
  prnE: '⪵',
  prnap: '⪹',
  prnsim: '⋨',
  prod: '∏',
  profalar: '⌮',
  profline: '⌒',
  profsurf: '⌓',
  prop: '∝',
  propto: '∝',
  prsim: '≾',
  prurel: '⊰',
  pscr: '𝓅',
  psi: 'ψ',
  puncsp: ' ',
  qfr: '𝔮',
  qint: '⨌',
  qopf: '𝕢',
  qprime: '⁗',
  qscr: '𝓆',
  quaternions: 'ℍ',
  quatint: '⨖',
  quest: '?',
  questeq: '≟',
  quot: '"',
  rAarr: '⇛',
  rArr: '⇒',
  rAtail: '⤜',
  rBarr: '⤏',
  rHar: '⥤',
  race: '∽̱',
  racute: 'ŕ',
  radic: '√',
  raemptyv: '⦳',
  rang: '⟩',
  rangd: '⦒',
  range: '⦥',
  rangle: '⟩',
  raquo: '»',
  rarr: '→',
  rarrap: '⥵',
  rarrb: '⇥',
  rarrbfs: '⤠',
  rarrc: '⤳',
  rarrfs: '⤞',
  rarrhk: '↪',
  rarrlp: '↬',
  rarrpl: '⥅',
  rarrsim: '⥴',
  rarrtl: '↣',
  rarrw: '↝',
  ratail: '⤚',
  ratio: '∶',
  rationals: 'ℚ',
  rbarr: '⤍',
  rbbrk: '❳',
  rbrace: '}',
  rbrack: ']',
  rbrke: '⦌',
  rbrksld: '⦎',
  rbrkslu: '⦐',
  rcaron: 'ř',
  rcedil: 'ŗ',
  rceil: '⌉',
  rcub: '}',
  rcy: 'р',
  rdca: '⤷',
  rdldhar: '⥩',
  rdquo: '”',
  rdquor: '”',
  rdsh: '↳',
  real: 'ℜ',
  realine: 'ℛ',
  realpart: 'ℜ',
  reals: 'ℝ',
  rect: '▭',
  reg: '®',
  rfisht: '⥽',
  rfloor: '⌋',
  rfr: '𝔯',
  rhard: '⇁',
  rharu: '⇀',
  rharul: '⥬',
  rho: 'ρ',
  rhov: 'ϱ',
  rightarrow: '→',
  rightarrowtail: '↣',
  rightharpoondown: '⇁',
  rightharpoonup: '⇀',
  rightleftarrows: '⇄',
  rightleftharpoons: '⇌',
  rightrightarrows: '⇉',
  rightsquigarrow: '↝',
  rightthreetimes: '⋌',
  ring: '˚',
  risingdotseq: '≓',
  rlarr: '⇄',
  rlhar: '⇌',
  rlm: '‏',
  rmoust: '⎱',
  rmoustache: '⎱',
  rnmid: '⫮',
  roang: '⟭',
  roarr: '⇾',
  robrk: '⟧',
  ropar: '⦆',
  ropf: '𝕣',
  roplus: '⨮',
  rotimes: '⨵',
  rpar: ')',
  rpargt: '⦔',
  rppolint: '⨒',
  rrarr: '⇉',
  rsaquo: '›',
  rscr: '𝓇',
  rsh: '↱',
  rsqb: ']',
  rsquo: '’',
  rsquor: '’',
  rthree: '⋌',
  rtimes: '⋊',
  rtri: '▹',
  rtrie: '⊵',
  rtrif: '▸',
  rtriltri: '⧎',
  ruluhar: '⥨',
  rx: '℞',
  sacute: 'ś',
  sbquo: '‚',
  sc: '≻',
  scE: '⪴',
  scap: '⪸',
  scaron: 'š',
  sccue: '≽',
  sce: '⪰',
  scedil: 'ş',
  scirc: 'ŝ',
  scnE: '⪶',
  scnap: '⪺',
  scnsim: '⋩',
  scpolint: '⨓',
  scsim: '≿',
  scy: 'с',
  sdot: '⋅',
  sdotb: '⊡',
  sdote: '⩦',
  seArr: '⇘',
  searhk: '⤥',
  searr: '↘',
  searrow: '↘',
  sect: '§',
  semi: ';',
  seswar: '⤩',
  setminus: '∖',
  setmn: '∖',
  sext: '✶',
  sfr: '𝔰',
  sfrown: '⌢',
  sharp: '♯',
  shchcy: 'щ',
  shcy: 'ш',
  shortmid: '∣',
  shortparallel: '∥',
  shy: '­',
  sigma: 'σ',
  sigmaf: 'ς',
  sigmav: 'ς',
  sim: '∼',
  simdot: '⩪',
  sime: '≃',
  simeq: '≃',
  simg: '⪞',
  simgE: '⪠',
  siml: '⪝',
  simlE: '⪟',
  simne: '≆',
  simplus: '⨤',
  simrarr: '⥲',
  slarr: '←',
  smallsetminus: '∖',
  smashp: '⨳',
  smeparsl: '⧤',
  smid: '∣',
  smile: '⌣',
  smt: '⪪',
  smte: '⪬',
  smtes: '⪬︀',
  softcy: 'ь',
  sol: '/',
  solb: '⧄',
  solbar: '⌿',
  sopf: '𝕤',
  spades: '♠',
  spadesuit: '♠',
  spar: '∥',
  sqcap: '⊓',
  sqcaps: '⊓︀',
  sqcup: '⊔',
  sqcups: '⊔︀',
  sqsub: '⊏',
  sqsube: '⊑',
  sqsubset: '⊏',
  sqsubseteq: '⊑',
  sqsup: '⊐',
  sqsupe: '⊒',
  sqsupset: '⊐',
  sqsupseteq: '⊒',
  squ: '□',
  square: '□',
  squarf: '▪',
  squf: '▪',
  srarr: '→',
  sscr: '𝓈',
  ssetmn: '∖',
  ssmile: '⌣',
  sstarf: '⋆',
  star: '☆',
  starf: '★',
  straightepsilon: 'ϵ',
  straightphi: 'ϕ',
  strns: '¯',
  sub: '⊂',
  subE: '⫅',
  subdot: '⪽',
  sube: '⊆',
  subedot: '⫃',
  submult: '⫁',
  subnE: '⫋',
  subne: '⊊',
  subplus: '⪿',
  subrarr: '⥹',
  subset: '⊂',
  subseteq: '⊆',
  subseteqq: '⫅',
  subsetneq: '⊊',
  subsetneqq: '⫋',
  subsim: '⫇',
  subsub: '⫕',
  subsup: '⫓',
  succ: '≻',
  succapprox: '⪸',
  succcurlyeq: '≽',
  succeq: '⪰',
  succnapprox: '⪺',
  succneqq: '⪶',
  succnsim: '⋩',
  succsim: '≿',
  sum: '∑',
  sung: '♪',
  sup1: '¹',
  sup2: '²',
  sup3: '³',
  sup: '⊃',
  supE: '⫆',
  supdot: '⪾',
  supdsub: '⫘',
  supe: '⊇',
  supedot: '⫄',
  suphsol: '⟉',
  suphsub: '⫗',
  suplarr: '⥻',
  supmult: '⫂',
  supnE: '⫌',
  supne: '⊋',
  supplus: '⫀',
  supset: '⊃',
  supseteq: '⊇',
  supseteqq: '⫆',
  supsetneq: '⊋',
  supsetneqq: '⫌',
  supsim: '⫈',
  supsub: '⫔',
  supsup: '⫖',
  swArr: '⇙',
  swarhk: '⤦',
  swarr: '↙',
  swarrow: '↙',
  swnwar: '⤪',
  szlig: 'ß',
  target: '⌖',
  tau: 'τ',
  tbrk: '⎴',
  tcaron: 'ť',
  tcedil: 'ţ',
  tcy: 'т',
  tdot: '⃛',
  telrec: '⌕',
  tfr: '𝔱',
  there4: '∴',
  therefore: '∴',
  theta: 'θ',
  thetasym: 'ϑ',
  thetav: 'ϑ',
  thickapprox: '≈',
  thicksim: '∼',
  thinsp: ' ',
  thkap: '≈',
  thksim: '∼',
  thorn: 'þ',
  tilde: '˜',
  times: '×',
  timesb: '⊠',
  timesbar: '⨱',
  timesd: '⨰',
  tint: '∭',
  toea: '⤨',
  top: '⊤',
  topbot: '⌶',
  topcir: '⫱',
  topf: '𝕥',
  topfork: '⫚',
  tosa: '⤩',
  tprime: '‴',
  trade: '™',
  triangle: '▵',
  triangledown: '▿',
  triangleleft: '◃',
  trianglelefteq: '⊴',
  triangleq: '≜',
  triangleright: '▹',
  trianglerighteq: '⊵',
  tridot: '◬',
  trie: '≜',
  triminus: '⨺',
  triplus: '⨹',
  trisb: '⧍',
  tritime: '⨻',
  trpezium: '⏢',
  tscr: '𝓉',
  tscy: 'ц',
  tshcy: 'ћ',
  tstrok: 'ŧ',
  twixt: '≬',
  twoheadleftarrow: '↞',
  twoheadrightarrow: '↠',
  uArr: '⇑',
  uHar: '⥣',
  uacute: 'ú',
  uarr: '↑',
  ubrcy: 'ў',
  ubreve: 'ŭ',
  ucirc: 'û',
  ucy: 'у',
  udarr: '⇅',
  udblac: 'ű',
  udhar: '⥮',
  ufisht: '⥾',
  ufr: '𝔲',
  ugrave: 'ù',
  uharl: '↿',
  uharr: '↾',
  uhblk: '▀',
  ulcorn: '⌜',
  ulcorner: '⌜',
  ulcrop: '⌏',
  ultri: '◸',
  umacr: 'ū',
  uml: '¨',
  uogon: 'ų',
  uopf: '𝕦',
  uparrow: '↑',
  updownarrow: '↕',
  upharpoonleft: '↿',
  upharpoonright: '↾',
  uplus: '⊎',
  upsi: 'υ',
  upsih: 'ϒ',
  upsilon: 'υ',
  upuparrows: '⇈',
  urcorn: '⌝',
  urcorner: '⌝',
  urcrop: '⌎',
  uring: 'ů',
  urtri: '◹',
  uscr: '𝓊',
  utdot: '⋰',
  utilde: 'ũ',
  utri: '▵',
  utrif: '▴',
  uuarr: '⇈',
  uuml: 'ü',
  uwangle: '⦧',
  vArr: '⇕',
  vBar: '⫨',
  vBarv: '⫩',
  vDash: '⊨',
  vangrt: '⦜',
  varepsilon: 'ϵ',
  varkappa: 'ϰ',
  varnothing: '∅',
  varphi: 'ϕ',
  varpi: 'ϖ',
  varpropto: '∝',
  varr: '↕',
  varrho: 'ϱ',
  varsigma: 'ς',
  varsubsetneq: '⊊︀',
  varsubsetneqq: '⫋︀',
  varsupsetneq: '⊋︀',
  varsupsetneqq: '⫌︀',
  vartheta: 'ϑ',
  vartriangleleft: '⊲',
  vartriangleright: '⊳',
  vcy: 'в',
  vdash: '⊢',
  vee: '∨',
  veebar: '⊻',
  veeeq: '≚',
  vellip: '⋮',
  verbar: '|',
  vert: '|',
  vfr: '𝔳',
  vltri: '⊲',
  vnsub: '⊂⃒',
  vnsup: '⊃⃒',
  vopf: '𝕧',
  vprop: '∝',
  vrtri: '⊳',
  vscr: '𝓋',
  vsubnE: '⫋︀',
  vsubne: '⊊︀',
  vsupnE: '⫌︀',
  vsupne: '⊋︀',
  vzigzag: '⦚',
  wcirc: 'ŵ',
  wedbar: '⩟',
  wedge: '∧',
  wedgeq: '≙',
  weierp: '℘',
  wfr: '𝔴',
  wopf: '𝕨',
  wp: '℘',
  wr: '≀',
  wreath: '≀',
  wscr: '𝓌',
  xcap: '⋂',
  xcirc: '◯',
  xcup: '⋃',
  xdtri: '▽',
  xfr: '𝔵',
  xhArr: '⟺',
  xharr: '⟷',
  xi: 'ξ',
  xlArr: '⟸',
  xlarr: '⟵',
  xmap: '⟼',
  xnis: '⋻',
  xodot: '⨀',
  xopf: '𝕩',
  xoplus: '⨁',
  xotime: '⨂',
  xrArr: '⟹',
  xrarr: '⟶',
  xscr: '𝓍',
  xsqcup: '⨆',
  xuplus: '⨄',
  xutri: '△',
  xvee: '⋁',
  xwedge: '⋀',
  yacute: 'ý',
  yacy: 'я',
  ycirc: 'ŷ',
  ycy: 'ы',
  yen: '¥',
  yfr: '𝔶',
  yicy: 'ї',
  yopf: '𝕪',
  yscr: '𝓎',
  yucy: 'ю',
  yuml: 'ÿ',
  zacute: 'ź',
  zcaron: 'ž',
  zcy: 'з',
  zdot: 'ż',
  zeetrf: 'ℨ',
  zeta: 'ζ',
  zfr: '𝔷',
  zhcy: 'ж',
  zigrarr: '⇝',
  zopf: '𝕫',
  zscr: '𝓏',
  zwj: '‍',
  zwnj: '‌'
};

const own$4 = {}.hasOwnProperty;
function decodeNamedCharacterReference(value) {
  return own$4.call(characterEntities, value) ? characterEntities[value] : false
}

function splice(list, start, remove, items) {
  const end = list.length;
  let chunkStart = 0;
  let parameters;
  if (start < 0) {
    start = -start > end ? 0 : end + start;
  } else {
    start = start > end ? end : start;
  }
  remove = remove > 0 ? remove : 0;
  if (items.length < 10000) {
    parameters = Array.from(items);
    parameters.unshift(start, remove);
    list.splice(...parameters);
  } else {
    if (remove) list.splice(start, remove);
    while (chunkStart < items.length) {
      parameters = items.slice(chunkStart, chunkStart + 10000);
      parameters.unshift(start, 0);
      list.splice(...parameters);
      chunkStart += 10000;
      start += 10000;
    }
  }
}
function push(list, items) {
  if (list.length > 0) {
    splice(list, list.length, 0, items);
    return list
  }
  return items
}

const hasOwnProperty = {}.hasOwnProperty;
function combineExtensions(extensions) {
  const all = {};
  let index = -1;
  while (++index < extensions.length) {
    syntaxExtension(all, extensions[index]);
  }
  return all
}
function syntaxExtension(all, extension) {
  let hook;
  for (hook in extension) {
    const maybe = hasOwnProperty.call(all, hook) ? all[hook] : undefined;
    const left = maybe || (all[hook] = {});
    const right = extension[hook];
    let code;
    if (right) {
      for (code in right) {
        if (!hasOwnProperty.call(left, code)) left[code] = [];
        const value = right[code];
        constructs(
          left[code],
          Array.isArray(value) ? value : value ? [value] : []
        );
      }
    }
  }
}
function constructs(existing, list) {
  let index = -1;
  const before = [];
  while (++index < list.length) {
(list[index].add === 'after' ? existing : before).push(list[index]);
  }
  splice(existing, 0, 0, before);
}

function decodeNumericCharacterReference(value, base) {
  const code = Number.parseInt(value, base);
  if (
  code < 9 || code === 11 || code > 13 && code < 32 ||
  code > 126 && code < 160 ||
  code > 55_295 && code < 57_344 ||
  code > 64_975 && code < 65_008 ||
  (code & 65_535) === 65_535 || (code & 65_535) === 65_534 ||
  code > 1_114_111) {
    return "\uFFFD";
  }
  return String.fromCodePoint(code);
}

function normalizeIdentifier(value) {
  return (
    value
      .replace(/[\t\n\r ]+/g, ' ')
      .replace(/^ | $/g, '')
      .toLowerCase()
      .toUpperCase()
  )
}

const asciiAlpha = regexCheck(/[A-Za-z]/);
const asciiAlphanumeric = regexCheck(/[\dA-Za-z]/);
const asciiAtext = regexCheck(/[#-'*+\--9=?A-Z^-~]/);
function asciiControl(code) {
  return (
    code !== null && (code < 32 || code === 127)
  );
}
const asciiDigit = regexCheck(/\d/);
const asciiHexDigit = regexCheck(/[\dA-Fa-f]/);
const asciiPunctuation = regexCheck(/[!-/:-@[-`{-~]/);
function markdownLineEnding(code) {
  return code !== null && code < -2;
}
function markdownLineEndingOrSpace(code) {
  return code !== null && (code < 0 || code === 32);
}
function markdownSpace(code) {
  return code === -2 || code === -1 || code === 32;
}
const unicodePunctuation = regexCheck(/\p{P}|\p{S}/u);
const unicodeWhitespace = regexCheck(/\s/);
function regexCheck(regex) {
  return check;
  function check(code) {
    return code !== null && code > -1 && regex.test(String.fromCharCode(code));
  }
}

function factorySpace(effects, ok, type, max) {
  const limit = max ? max - 1 : Number.POSITIVE_INFINITY;
  let size = 0;
  return start
  function start(code) {
    if (markdownSpace(code)) {
      effects.enter(type);
      return prefix(code)
    }
    return ok(code)
  }
  function prefix(code) {
    if (markdownSpace(code) && size++ < limit) {
      effects.consume(code);
      return prefix
    }
    effects.exit(type);
    return ok(code)
  }
}

const content$1 = {
  tokenize: initializeContent
};
function initializeContent(effects) {
  const contentStart = effects.attempt(
    this.parser.constructs.contentInitial,
    afterContentStartConstruct,
    paragraphInitial
  );
  let previous;
  return contentStart
  function afterContentStartConstruct(code) {
    if (code === null) {
      effects.consume(code);
      return
    }
    effects.enter('lineEnding');
    effects.consume(code);
    effects.exit('lineEnding');
    return factorySpace(effects, contentStart, 'linePrefix')
  }
  function paragraphInitial(code) {
    effects.enter('paragraph');
    return lineStart(code)
  }
  function lineStart(code) {
    const token = effects.enter('chunkText', {
      contentType: 'text',
      previous
    });
    if (previous) {
      previous.next = token;
    }
    previous = token;
    return data(code)
  }
  function data(code) {
    if (code === null) {
      effects.exit('chunkText');
      effects.exit('paragraph');
      effects.consume(code);
      return
    }
    if (markdownLineEnding(code)) {
      effects.consume(code);
      effects.exit('chunkText');
      return lineStart
    }
    effects.consume(code);
    return data
  }
}

const document$1 = {
  tokenize: initializeDocument
};
const containerConstruct = {
  tokenize: tokenizeContainer
};
function initializeDocument(effects) {
  const self = this;
  const stack = [];
  let continued = 0;
  let childFlow;
  let childToken;
  let lineStartOffset;
  return start
  function start(code) {
    if (continued < stack.length) {
      const item = stack[continued];
      self.containerState = item[1];
      return effects.attempt(
        item[0].continuation,
        documentContinue,
        checkNewContainers
      )(code)
    }
    return checkNewContainers(code)
  }
  function documentContinue(code) {
    continued++;
    if (self.containerState._closeFlow) {
      self.containerState._closeFlow = undefined;
      if (childFlow) {
        closeFlow();
      }
      const indexBeforeExits = self.events.length;
      let indexBeforeFlow = indexBeforeExits;
      let point;
      while (indexBeforeFlow--) {
        if (
          self.events[indexBeforeFlow][0] === 'exit' &&
          self.events[indexBeforeFlow][1].type === 'chunkFlow'
        ) {
          point = self.events[indexBeforeFlow][1].end;
          break
        }
      }
      exitContainers(continued);
      let index = indexBeforeExits;
      while (index < self.events.length) {
        self.events[index][1].end = Object.assign({}, point);
        index++;
      }
      splice(
        self.events,
        indexBeforeFlow + 1,
        0,
        self.events.slice(indexBeforeExits)
      );
      self.events.length = index;
      return checkNewContainers(code)
    }
    return start(code)
  }
  function checkNewContainers(code) {
    if (continued === stack.length) {
      if (!childFlow) {
        return documentContinued(code)
      }
      if (childFlow.currentConstruct && childFlow.currentConstruct.concrete) {
        return flowStart(code)
      }
      self.interrupt = Boolean(
        childFlow.currentConstruct && !childFlow._gfmTableDynamicInterruptHack
      );
    }
    self.containerState = {};
    return effects.check(
      containerConstruct,
      thereIsANewContainer,
      thereIsNoNewContainer
    )(code)
  }
  function thereIsANewContainer(code) {
    if (childFlow) closeFlow();
    exitContainers(continued);
    return documentContinued(code)
  }
  function thereIsNoNewContainer(code) {
    self.parser.lazy[self.now().line] = continued !== stack.length;
    lineStartOffset = self.now().offset;
    return flowStart(code)
  }
  function documentContinued(code) {
    self.containerState = {};
    return effects.attempt(
      containerConstruct,
      containerContinue,
      flowStart
    )(code)
  }
  function containerContinue(code) {
    continued++;
    stack.push([self.currentConstruct, self.containerState]);
    return documentContinued(code)
  }
  function flowStart(code) {
    if (code === null) {
      if (childFlow) closeFlow();
      exitContainers(0);
      effects.consume(code);
      return
    }
    childFlow = childFlow || self.parser.flow(self.now());
    effects.enter('chunkFlow', {
      contentType: 'flow',
      previous: childToken,
      _tokenizer: childFlow
    });
    return flowContinue(code)
  }
  function flowContinue(code) {
    if (code === null) {
      writeToChild(effects.exit('chunkFlow'), true);
      exitContainers(0);
      effects.consume(code);
      return
    }
    if (markdownLineEnding(code)) {
      effects.consume(code);
      writeToChild(effects.exit('chunkFlow'));
      continued = 0;
      self.interrupt = undefined;
      return start
    }
    effects.consume(code);
    return flowContinue
  }
  function writeToChild(token, eof) {
    const stream = self.sliceStream(token);
    if (eof) stream.push(null);
    token.previous = childToken;
    if (childToken) childToken.next = token;
    childToken = token;
    childFlow.defineSkip(token.start);
    childFlow.write(stream);
    if (self.parser.lazy[token.start.line]) {
      let index = childFlow.events.length;
      while (index--) {
        if (
          childFlow.events[index][1].start.offset < lineStartOffset &&
          (!childFlow.events[index][1].end ||
            childFlow.events[index][1].end.offset > lineStartOffset)
        ) {
          return
        }
      }
      const indexBeforeExits = self.events.length;
      let indexBeforeFlow = indexBeforeExits;
      let seen;
      let point;
      while (indexBeforeFlow--) {
        if (
          self.events[indexBeforeFlow][0] === 'exit' &&
          self.events[indexBeforeFlow][1].type === 'chunkFlow'
        ) {
          if (seen) {
            point = self.events[indexBeforeFlow][1].end;
            break
          }
          seen = true;
        }
      }
      exitContainers(continued);
      index = indexBeforeExits;
      while (index < self.events.length) {
        self.events[index][1].end = Object.assign({}, point);
        index++;
      }
      splice(
        self.events,
        indexBeforeFlow + 1,
        0,
        self.events.slice(indexBeforeExits)
      );
      self.events.length = index;
    }
  }
  function exitContainers(size) {
    let index = stack.length;
    while (index-- > size) {
      const entry = stack[index];
      self.containerState = entry[1];
      entry[0].exit.call(self, effects);
    }
    stack.length = size;
  }
  function closeFlow() {
    childFlow.write([null]);
    childToken = undefined;
    childFlow = undefined;
    self.containerState._closeFlow = undefined;
  }
}
function tokenizeContainer(effects, ok, nok) {
  return factorySpace(
    effects,
    effects.attempt(this.parser.constructs.document, ok, nok),
    'linePrefix',
    this.parser.constructs.disable.null.includes('codeIndented') ? undefined : 4
  )
}

function classifyCharacter(code) {
  if (
    code === null ||
    markdownLineEndingOrSpace(code) ||
    unicodeWhitespace(code)
  ) {
    return 1
  }
  if (unicodePunctuation(code)) {
    return 2
  }
}

function resolveAll(constructs, events, context) {
  const called = [];
  let index = -1;
  while (++index < constructs.length) {
    const resolve = constructs[index].resolveAll;
    if (resolve && !called.includes(resolve)) {
      events = resolve(events, context);
      called.push(resolve);
    }
  }
  return events
}

const attention = {
  name: 'attention',
  tokenize: tokenizeAttention,
  resolveAll: resolveAllAttention
};
function resolveAllAttention(events, context) {
  let index = -1;
  let open;
  let group;
  let text;
  let openingSequence;
  let closingSequence;
  let use;
  let nextEvents;
  let offset;
  while (++index < events.length) {
    if (events[index][0] === 'enter' && events[index][1].type === 'attentionSequence' && events[index][1]._close) {
      open = index;
      while (open--) {
        if (events[open][0] === 'exit' && events[open][1].type === 'attentionSequence' && events[open][1]._open &&
        context.sliceSerialize(events[open][1]).charCodeAt(0) === context.sliceSerialize(events[index][1]).charCodeAt(0)) {
          if ((events[open][1]._close || events[index][1]._open) && (events[index][1].end.offset - events[index][1].start.offset) % 3 && !((events[open][1].end.offset - events[open][1].start.offset + events[index][1].end.offset - events[index][1].start.offset) % 3)) {
            continue;
          }
          use = events[open][1].end.offset - events[open][1].start.offset > 1 && events[index][1].end.offset - events[index][1].start.offset > 1 ? 2 : 1;
          const start = Object.assign({}, events[open][1].end);
          const end = Object.assign({}, events[index][1].start);
          movePoint(start, -use);
          movePoint(end, use);
          openingSequence = {
            type: use > 1 ? "strongSequence" : "emphasisSequence",
            start,
            end: Object.assign({}, events[open][1].end)
          };
          closingSequence = {
            type: use > 1 ? "strongSequence" : "emphasisSequence",
            start: Object.assign({}, events[index][1].start),
            end
          };
          text = {
            type: use > 1 ? "strongText" : "emphasisText",
            start: Object.assign({}, events[open][1].end),
            end: Object.assign({}, events[index][1].start)
          };
          group = {
            type: use > 1 ? "strong" : "emphasis",
            start: Object.assign({}, openingSequence.start),
            end: Object.assign({}, closingSequence.end)
          };
          events[open][1].end = Object.assign({}, openingSequence.start);
          events[index][1].start = Object.assign({}, closingSequence.end);
          nextEvents = [];
          if (events[open][1].end.offset - events[open][1].start.offset) {
            nextEvents = push(nextEvents, [['enter', events[open][1], context], ['exit', events[open][1], context]]);
          }
          nextEvents = push(nextEvents, [['enter', group, context], ['enter', openingSequence, context], ['exit', openingSequence, context], ['enter', text, context]]);
          nextEvents = push(nextEvents, resolveAll(context.parser.constructs.insideSpan.null, events.slice(open + 1, index), context));
          nextEvents = push(nextEvents, [['exit', text, context], ['enter', closingSequence, context], ['exit', closingSequence, context], ['exit', group, context]]);
          if (events[index][1].end.offset - events[index][1].start.offset) {
            offset = 2;
            nextEvents = push(nextEvents, [['enter', events[index][1], context], ['exit', events[index][1], context]]);
          } else {
            offset = 0;
          }
          splice(events, open - 1, index - open + 3, nextEvents);
          index = open + nextEvents.length - offset - 2;
          break;
        }
      }
    }
  }
  index = -1;
  while (++index < events.length) {
    if (events[index][1].type === 'attentionSequence') {
      events[index][1].type = 'data';
    }
  }
  return events;
}
function tokenizeAttention(effects, ok) {
  const attentionMarkers = this.parser.constructs.attentionMarkers.null;
  const previous = this.previous;
  const before = classifyCharacter(previous);
  let marker;
  return start;
  function start(code) {
    marker = code;
    effects.enter('attentionSequence');
    return inside(code);
  }
  function inside(code) {
    if (code === marker) {
      effects.consume(code);
      return inside;
    }
    const token = effects.exit('attentionSequence');
    const after = classifyCharacter(code);
    const open = !after || after === 2 && before || attentionMarkers.includes(code);
    const close = !before || before === 2 && after || attentionMarkers.includes(previous);
    token._open = Boolean(marker === 42 ? open : open && (before || !close));
    token._close = Boolean(marker === 42 ? close : close && (after || !open));
    return ok(code);
  }
}
function movePoint(point, offset) {
  point.column += offset;
  point.offset += offset;
  point._bufferIndex += offset;
}

const autolink = {
  name: 'autolink',
  tokenize: tokenizeAutolink
};
function tokenizeAutolink(effects, ok, nok) {
  let size = 0;
  return start;
  function start(code) {
    effects.enter("autolink");
    effects.enter("autolinkMarker");
    effects.consume(code);
    effects.exit("autolinkMarker");
    effects.enter("autolinkProtocol");
    return open;
  }
  function open(code) {
    if (asciiAlpha(code)) {
      effects.consume(code);
      return schemeOrEmailAtext;
    }
    if (code === 64) {
      return nok(code);
    }
    return emailAtext(code);
  }
  function schemeOrEmailAtext(code) {
    if (code === 43 || code === 45 || code === 46 || asciiAlphanumeric(code)) {
      size = 1;
      return schemeInsideOrEmailAtext(code);
    }
    return emailAtext(code);
  }
  function schemeInsideOrEmailAtext(code) {
    if (code === 58) {
      effects.consume(code);
      size = 0;
      return urlInside;
    }
    if ((code === 43 || code === 45 || code === 46 || asciiAlphanumeric(code)) && size++ < 32) {
      effects.consume(code);
      return schemeInsideOrEmailAtext;
    }
    size = 0;
    return emailAtext(code);
  }
  function urlInside(code) {
    if (code === 62) {
      effects.exit("autolinkProtocol");
      effects.enter("autolinkMarker");
      effects.consume(code);
      effects.exit("autolinkMarker");
      effects.exit("autolink");
      return ok;
    }
    if (code === null || code === 32 || code === 60 || asciiControl(code)) {
      return nok(code);
    }
    effects.consume(code);
    return urlInside;
  }
  function emailAtext(code) {
    if (code === 64) {
      effects.consume(code);
      return emailAtSignOrDot;
    }
    if (asciiAtext(code)) {
      effects.consume(code);
      return emailAtext;
    }
    return nok(code);
  }
  function emailAtSignOrDot(code) {
    return asciiAlphanumeric(code) ? emailLabel(code) : nok(code);
  }
  function emailLabel(code) {
    if (code === 46) {
      effects.consume(code);
      size = 0;
      return emailAtSignOrDot;
    }
    if (code === 62) {
      effects.exit("autolinkProtocol").type = "autolinkEmail";
      effects.enter("autolinkMarker");
      effects.consume(code);
      effects.exit("autolinkMarker");
      effects.exit("autolink");
      return ok;
    }
    return emailValue(code);
  }
  function emailValue(code) {
    if ((code === 45 || asciiAlphanumeric(code)) && size++ < 63) {
      const next = code === 45 ? emailValue : emailLabel;
      effects.consume(code);
      return next;
    }
    return nok(code);
  }
}

const blankLine = {
  tokenize: tokenizeBlankLine,
  partial: true
};
function tokenizeBlankLine(effects, ok, nok) {
  return start;
  function start(code) {
    return markdownSpace(code) ? factorySpace(effects, after, "linePrefix")(code) : after(code);
  }
  function after(code) {
    return code === null || markdownLineEnding(code) ? ok(code) : nok(code);
  }
}

const blockQuote = {
  name: 'blockQuote',
  tokenize: tokenizeBlockQuoteStart,
  continuation: {
    tokenize: tokenizeBlockQuoteContinuation
  },
  exit: exit$1
};
function tokenizeBlockQuoteStart(effects, ok, nok) {
  const self = this;
  return start;
  function start(code) {
    if (code === 62) {
      const state = self.containerState;
      if (!state.open) {
        effects.enter("blockQuote", {
          _container: true
        });
        state.open = true;
      }
      effects.enter("blockQuotePrefix");
      effects.enter("blockQuoteMarker");
      effects.consume(code);
      effects.exit("blockQuoteMarker");
      return after;
    }
    return nok(code);
  }
  function after(code) {
    if (markdownSpace(code)) {
      effects.enter("blockQuotePrefixWhitespace");
      effects.consume(code);
      effects.exit("blockQuotePrefixWhitespace");
      effects.exit("blockQuotePrefix");
      return ok;
    }
    effects.exit("blockQuotePrefix");
    return ok(code);
  }
}
function tokenizeBlockQuoteContinuation(effects, ok, nok) {
  const self = this;
  return contStart;
  function contStart(code) {
    if (markdownSpace(code)) {
      return factorySpace(effects, contBefore, "linePrefix", self.parser.constructs.disable.null.includes('codeIndented') ? undefined : 4)(code);
    }
    return contBefore(code);
  }
  function contBefore(code) {
    return effects.attempt(blockQuote, ok, nok)(code);
  }
}
function exit$1(effects) {
  effects.exit("blockQuote");
}

const characterEscape = {
  name: 'characterEscape',
  tokenize: tokenizeCharacterEscape
};
function tokenizeCharacterEscape(effects, ok, nok) {
  return start;
  function start(code) {
    effects.enter("characterEscape");
    effects.enter("escapeMarker");
    effects.consume(code);
    effects.exit("escapeMarker");
    return inside;
  }
  function inside(code) {
    if (asciiPunctuation(code)) {
      effects.enter("characterEscapeValue");
      effects.consume(code);
      effects.exit("characterEscapeValue");
      effects.exit("characterEscape");
      return ok;
    }
    return nok(code);
  }
}

const characterReference = {
  name: 'characterReference',
  tokenize: tokenizeCharacterReference
};
function tokenizeCharacterReference(effects, ok, nok) {
  const self = this;
  let size = 0;
  let max;
  let test;
  return start;
  function start(code) {
    effects.enter("characterReference");
    effects.enter("characterReferenceMarker");
    effects.consume(code);
    effects.exit("characterReferenceMarker");
    return open;
  }
  function open(code) {
    if (code === 35) {
      effects.enter("characterReferenceMarkerNumeric");
      effects.consume(code);
      effects.exit("characterReferenceMarkerNumeric");
      return numeric;
    }
    effects.enter("characterReferenceValue");
    max = 31;
    test = asciiAlphanumeric;
    return value(code);
  }
  function numeric(code) {
    if (code === 88 || code === 120) {
      effects.enter("characterReferenceMarkerHexadecimal");
      effects.consume(code);
      effects.exit("characterReferenceMarkerHexadecimal");
      effects.enter("characterReferenceValue");
      max = 6;
      test = asciiHexDigit;
      return value;
    }
    effects.enter("characterReferenceValue");
    max = 7;
    test = asciiDigit;
    return value(code);
  }
  function value(code) {
    if (code === 59 && size) {
      const token = effects.exit("characterReferenceValue");
      if (test === asciiAlphanumeric && !decodeNamedCharacterReference(self.sliceSerialize(token))) {
        return nok(code);
      }
      effects.enter("characterReferenceMarker");
      effects.consume(code);
      effects.exit("characterReferenceMarker");
      effects.exit("characterReference");
      return ok;
    }
    if (test(code) && size++ < max) {
      effects.consume(code);
      return value;
    }
    return nok(code);
  }
}

const nonLazyContinuation = {
  tokenize: tokenizeNonLazyContinuation,
  partial: true
};
const codeFenced = {
  name: 'codeFenced',
  tokenize: tokenizeCodeFenced,
  concrete: true
};
function tokenizeCodeFenced(effects, ok, nok) {
  const self = this;
  const closeStart = {
    tokenize: tokenizeCloseStart,
    partial: true
  };
  let initialPrefix = 0;
  let sizeOpen = 0;
  let marker;
  return start;
  function start(code) {
    return beforeSequenceOpen(code);
  }
  function beforeSequenceOpen(code) {
    const tail = self.events[self.events.length - 1];
    initialPrefix = tail && tail[1].type === "linePrefix" ? tail[2].sliceSerialize(tail[1], true).length : 0;
    marker = code;
    effects.enter("codeFenced");
    effects.enter("codeFencedFence");
    effects.enter("codeFencedFenceSequence");
    return sequenceOpen(code);
  }
  function sequenceOpen(code) {
    if (code === marker) {
      sizeOpen++;
      effects.consume(code);
      return sequenceOpen;
    }
    if (sizeOpen < 3) {
      return nok(code);
    }
    effects.exit("codeFencedFenceSequence");
    return markdownSpace(code) ? factorySpace(effects, infoBefore, "whitespace")(code) : infoBefore(code);
  }
  function infoBefore(code) {
    if (code === null || markdownLineEnding(code)) {
      effects.exit("codeFencedFence");
      return self.interrupt ? ok(code) : effects.check(nonLazyContinuation, atNonLazyBreak, after)(code);
    }
    effects.enter("codeFencedFenceInfo");
    effects.enter("chunkString", {
      contentType: "string"
    });
    return info(code);
  }
  function info(code) {
    if (code === null || markdownLineEnding(code)) {
      effects.exit("chunkString");
      effects.exit("codeFencedFenceInfo");
      return infoBefore(code);
    }
    if (markdownSpace(code)) {
      effects.exit("chunkString");
      effects.exit("codeFencedFenceInfo");
      return factorySpace(effects, metaBefore, "whitespace")(code);
    }
    if (code === 96 && code === marker) {
      return nok(code);
    }
    effects.consume(code);
    return info;
  }
  function metaBefore(code) {
    if (code === null || markdownLineEnding(code)) {
      return infoBefore(code);
    }
    effects.enter("codeFencedFenceMeta");
    effects.enter("chunkString", {
      contentType: "string"
    });
    return meta(code);
  }
  function meta(code) {
    if (code === null || markdownLineEnding(code)) {
      effects.exit("chunkString");
      effects.exit("codeFencedFenceMeta");
      return infoBefore(code);
    }
    if (code === 96 && code === marker) {
      return nok(code);
    }
    effects.consume(code);
    return meta;
  }
  function atNonLazyBreak(code) {
    return effects.attempt(closeStart, after, contentBefore)(code);
  }
  function contentBefore(code) {
    effects.enter("lineEnding");
    effects.consume(code);
    effects.exit("lineEnding");
    return contentStart;
  }
  function contentStart(code) {
    return initialPrefix > 0 && markdownSpace(code) ? factorySpace(effects, beforeContentChunk, "linePrefix", initialPrefix + 1)(code) : beforeContentChunk(code);
  }
  function beforeContentChunk(code) {
    if (code === null || markdownLineEnding(code)) {
      return effects.check(nonLazyContinuation, atNonLazyBreak, after)(code);
    }
    effects.enter("codeFlowValue");
    return contentChunk(code);
  }
  function contentChunk(code) {
    if (code === null || markdownLineEnding(code)) {
      effects.exit("codeFlowValue");
      return beforeContentChunk(code);
    }
    effects.consume(code);
    return contentChunk;
  }
  function after(code) {
    effects.exit("codeFenced");
    return ok(code);
  }
  function tokenizeCloseStart(effects, ok, nok) {
    let size = 0;
    return startBefore;
    function startBefore(code) {
      effects.enter("lineEnding");
      effects.consume(code);
      effects.exit("lineEnding");
      return start;
    }
    function start(code) {
      effects.enter("codeFencedFence");
      return markdownSpace(code) ? factorySpace(effects, beforeSequenceClose, "linePrefix", self.parser.constructs.disable.null.includes('codeIndented') ? undefined : 4)(code) : beforeSequenceClose(code);
    }
    function beforeSequenceClose(code) {
      if (code === marker) {
        effects.enter("codeFencedFenceSequence");
        return sequenceClose(code);
      }
      return nok(code);
    }
    function sequenceClose(code) {
      if (code === marker) {
        size++;
        effects.consume(code);
        return sequenceClose;
      }
      if (size >= sizeOpen) {
        effects.exit("codeFencedFenceSequence");
        return markdownSpace(code) ? factorySpace(effects, sequenceCloseAfter, "whitespace")(code) : sequenceCloseAfter(code);
      }
      return nok(code);
    }
    function sequenceCloseAfter(code) {
      if (code === null || markdownLineEnding(code)) {
        effects.exit("codeFencedFence");
        return ok(code);
      }
      return nok(code);
    }
  }
}
function tokenizeNonLazyContinuation(effects, ok, nok) {
  const self = this;
  return start;
  function start(code) {
    if (code === null) {
      return nok(code);
    }
    effects.enter("lineEnding");
    effects.consume(code);
    effects.exit("lineEnding");
    return lineStart;
  }
  function lineStart(code) {
    return self.parser.lazy[self.now().line] ? nok(code) : ok(code);
  }
}

const codeIndented = {
  name: 'codeIndented',
  tokenize: tokenizeCodeIndented
};
const furtherStart = {
  tokenize: tokenizeFurtherStart,
  partial: true
};
function tokenizeCodeIndented(effects, ok, nok) {
  const self = this;
  return start;
  function start(code) {
    effects.enter("codeIndented");
    return factorySpace(effects, afterPrefix, "linePrefix", 4 + 1)(code);
  }
  function afterPrefix(code) {
    const tail = self.events[self.events.length - 1];
    return tail && tail[1].type === "linePrefix" && tail[2].sliceSerialize(tail[1], true).length >= 4 ? atBreak(code) : nok(code);
  }
  function atBreak(code) {
    if (code === null) {
      return after(code);
    }
    if (markdownLineEnding(code)) {
      return effects.attempt(furtherStart, atBreak, after)(code);
    }
    effects.enter("codeFlowValue");
    return inside(code);
  }
  function inside(code) {
    if (code === null || markdownLineEnding(code)) {
      effects.exit("codeFlowValue");
      return atBreak(code);
    }
    effects.consume(code);
    return inside;
  }
  function after(code) {
    effects.exit("codeIndented");
    return ok(code);
  }
}
function tokenizeFurtherStart(effects, ok, nok) {
  const self = this;
  return furtherStart;
  function furtherStart(code) {
    if (self.parser.lazy[self.now().line]) {
      return nok(code);
    }
    if (markdownLineEnding(code)) {
      effects.enter("lineEnding");
      effects.consume(code);
      effects.exit("lineEnding");
      return furtherStart;
    }
    return factorySpace(effects, afterPrefix, "linePrefix", 4 + 1)(code);
  }
  function afterPrefix(code) {
    const tail = self.events[self.events.length - 1];
    return tail && tail[1].type === "linePrefix" && tail[2].sliceSerialize(tail[1], true).length >= 4 ? ok(code) : markdownLineEnding(code) ? furtherStart(code) : nok(code);
  }
}

const codeText = {
  name: 'codeText',
  tokenize: tokenizeCodeText,
  resolve: resolveCodeText,
  previous: previous$1
};
function resolveCodeText(events) {
  let tailExitIndex = events.length - 4;
  let headEnterIndex = 3;
  let index;
  let enter;
  if ((events[headEnterIndex][1].type === "lineEnding" || events[headEnterIndex][1].type === 'space') && (events[tailExitIndex][1].type === "lineEnding" || events[tailExitIndex][1].type === 'space')) {
    index = headEnterIndex;
    while (++index < tailExitIndex) {
      if (events[index][1].type === "codeTextData") {
        events[headEnterIndex][1].type = "codeTextPadding";
        events[tailExitIndex][1].type = "codeTextPadding";
        headEnterIndex += 2;
        tailExitIndex -= 2;
        break;
      }
    }
  }
  index = headEnterIndex - 1;
  tailExitIndex++;
  while (++index <= tailExitIndex) {
    if (enter === undefined) {
      if (index !== tailExitIndex && events[index][1].type !== "lineEnding") {
        enter = index;
      }
    } else if (index === tailExitIndex || events[index][1].type === "lineEnding") {
      events[enter][1].type = "codeTextData";
      if (index !== enter + 2) {
        events[enter][1].end = events[index - 1][1].end;
        events.splice(enter + 2, index - enter - 2);
        tailExitIndex -= index - enter - 2;
        index = enter + 2;
      }
      enter = undefined;
    }
  }
  return events;
}
function previous$1(code) {
  return code !== 96 || this.events[this.events.length - 1][1].type === "characterEscape";
}
function tokenizeCodeText(effects, ok, nok) {
  let sizeOpen = 0;
  let size;
  let token;
  return start;
  function start(code) {
    effects.enter("codeText");
    effects.enter("codeTextSequence");
    return sequenceOpen(code);
  }
  function sequenceOpen(code) {
    if (code === 96) {
      effects.consume(code);
      sizeOpen++;
      return sequenceOpen;
    }
    effects.exit("codeTextSequence");
    return between(code);
  }
  function between(code) {
    if (code === null) {
      return nok(code);
    }
    if (code === 32) {
      effects.enter('space');
      effects.consume(code);
      effects.exit('space');
      return between;
    }
    if (code === 96) {
      token = effects.enter("codeTextSequence");
      size = 0;
      return sequenceClose(code);
    }
    if (markdownLineEnding(code)) {
      effects.enter("lineEnding");
      effects.consume(code);
      effects.exit("lineEnding");
      return between;
    }
    effects.enter("codeTextData");
    return data(code);
  }
  function data(code) {
    if (code === null || code === 32 || code === 96 || markdownLineEnding(code)) {
      effects.exit("codeTextData");
      return between(code);
    }
    effects.consume(code);
    return data;
  }
  function sequenceClose(code) {
    if (code === 96) {
      effects.consume(code);
      size++;
      return sequenceClose;
    }
    if (size === sizeOpen) {
      effects.exit("codeTextSequence");
      effects.exit("codeText");
      return ok(code);
    }
    token.type = "codeTextData";
    return data(code);
  }
}

class SpliceBuffer {
  constructor(initial) {
    this.left = initial ? [...initial] : [];
    this.right = [];
  }
  get(index) {
    if (index < 0 || index >= this.left.length + this.right.length) {
      throw new RangeError('Cannot access index `' + index + '` in a splice buffer of size `' + (this.left.length + this.right.length) + '`');
    }
    if (index < this.left.length) return this.left[index];
    return this.right[this.right.length - index + this.left.length - 1];
  }
  get length() {
    return this.left.length + this.right.length;
  }
  shift() {
    this.setCursor(0);
    return this.right.pop();
  }
  slice(start, end) {
    const stop = end === null || end === undefined ? Number.POSITIVE_INFINITY : end;
    if (stop < this.left.length) {
      return this.left.slice(start, stop);
    }
    if (start > this.left.length) {
      return this.right.slice(this.right.length - stop + this.left.length, this.right.length - start + this.left.length).reverse();
    }
    return this.left.slice(start).concat(this.right.slice(this.right.length - stop + this.left.length).reverse());
  }
  splice(start, deleteCount, items) {
    const count = deleteCount || 0;
    this.setCursor(Math.trunc(start));
    const removed = this.right.splice(this.right.length - count, Number.POSITIVE_INFINITY);
    if (items) chunkedPush(this.left, items);
    return removed.reverse();
  }
  pop() {
    this.setCursor(Number.POSITIVE_INFINITY);
    return this.left.pop();
  }
  push(item) {
    this.setCursor(Number.POSITIVE_INFINITY);
    this.left.push(item);
  }
  pushMany(items) {
    this.setCursor(Number.POSITIVE_INFINITY);
    chunkedPush(this.left, items);
  }
  unshift(item) {
    this.setCursor(0);
    this.right.push(item);
  }
  unshiftMany(items) {
    this.setCursor(0);
    chunkedPush(this.right, items.reverse());
  }
  setCursor(n) {
    if (n === this.left.length || n > this.left.length && this.right.length === 0 || n < 0 && this.left.length === 0) return;
    if (n < this.left.length) {
      const removed = this.left.splice(n, Number.POSITIVE_INFINITY);
      chunkedPush(this.right, removed.reverse());
    } else {
      const removed = this.right.splice(this.left.length + this.right.length - n, Number.POSITIVE_INFINITY);
      chunkedPush(this.left, removed.reverse());
    }
  }
}
function chunkedPush(list, right) {
  let chunkStart = 0;
  if (right.length < 10000) {
    list.push(...right);
  } else {
    while (chunkStart < right.length) {
      list.push(...right.slice(chunkStart, chunkStart + 10000));
      chunkStart += 10000;
    }
  }
}

function subtokenize(eventsArray) {
  const jumps = {};
  let index = -1;
  let event;
  let lineIndex;
  let otherIndex;
  let otherEvent;
  let parameters;
  let subevents;
  let more;
  const events = new SpliceBuffer(eventsArray);
  while (++index < events.length) {
    while (index in jumps) {
      index = jumps[index];
    }
    event = events.get(index);
    if (index && event[1].type === "chunkFlow" && events.get(index - 1)[1].type === "listItemPrefix") {
      subevents = event[1]._tokenizer.events;
      otherIndex = 0;
      if (otherIndex < subevents.length && subevents[otherIndex][1].type === "lineEndingBlank") {
        otherIndex += 2;
      }
      if (otherIndex < subevents.length && subevents[otherIndex][1].type === "content") {
        while (++otherIndex < subevents.length) {
          if (subevents[otherIndex][1].type === "content") {
            break;
          }
          if (subevents[otherIndex][1].type === "chunkText") {
            subevents[otherIndex][1]._isInFirstContentOfListItem = true;
            otherIndex++;
          }
        }
      }
    }
    if (event[0] === 'enter') {
      if (event[1].contentType) {
        Object.assign(jumps, subcontent(events, index));
        index = jumps[index];
        more = true;
      }
    }
    else if (event[1]._container) {
      otherIndex = index;
      lineIndex = undefined;
      while (otherIndex--) {
        otherEvent = events.get(otherIndex);
        if (otherEvent[1].type === "lineEnding" || otherEvent[1].type === "lineEndingBlank") {
          if (otherEvent[0] === 'enter') {
            if (lineIndex) {
              events.get(lineIndex)[1].type = "lineEndingBlank";
            }
            otherEvent[1].type = "lineEnding";
            lineIndex = otherIndex;
          }
        } else {
          break;
        }
      }
      if (lineIndex) {
        event[1].end = Object.assign({}, events.get(lineIndex)[1].start);
        parameters = events.slice(lineIndex, index);
        parameters.unshift(event);
        events.splice(lineIndex, index - lineIndex + 1, parameters);
      }
    }
  }
  splice(eventsArray, 0, Number.POSITIVE_INFINITY, events.slice(0));
  return !more;
}
function subcontent(events, eventIndex) {
  const token = events.get(eventIndex)[1];
  const context = events.get(eventIndex)[2];
  let startPosition = eventIndex - 1;
  const startPositions = [];
  const tokenizer = token._tokenizer || context.parser[token.contentType](token.start);
  const childEvents = tokenizer.events;
  const jumps = [];
  const gaps = {};
  let stream;
  let previous;
  let index = -1;
  let current = token;
  let adjust = 0;
  let start = 0;
  const breaks = [start];
  while (current) {
    while (events.get(++startPosition)[1] !== current) {
    }
    startPositions.push(startPosition);
    if (!current._tokenizer) {
      stream = context.sliceStream(current);
      if (!current.next) {
        stream.push(null);
      }
      if (previous) {
        tokenizer.defineSkip(current.start);
      }
      if (current._isInFirstContentOfListItem) {
        tokenizer._gfmTasklistFirstContentOfListItem = true;
      }
      tokenizer.write(stream);
      if (current._isInFirstContentOfListItem) {
        tokenizer._gfmTasklistFirstContentOfListItem = undefined;
      }
    }
    previous = current;
    current = current.next;
  }
  current = token;
  while (++index < childEvents.length) {
    if (
    childEvents[index][0] === 'exit' && childEvents[index - 1][0] === 'enter' && childEvents[index][1].type === childEvents[index - 1][1].type && childEvents[index][1].start.line !== childEvents[index][1].end.line) {
      start = index + 1;
      breaks.push(start);
      current._tokenizer = undefined;
      current.previous = undefined;
      current = current.next;
    }
  }
  tokenizer.events = [];
  if (current) {
    current._tokenizer = undefined;
    current.previous = undefined;
  } else {
    breaks.pop();
  }
  index = breaks.length;
  while (index--) {
    const slice = childEvents.slice(breaks[index], breaks[index + 1]);
    const start = startPositions.pop();
    jumps.push([start, start + slice.length - 1]);
    events.splice(start, 2, slice);
  }
  jumps.reverse();
  index = -1;
  while (++index < jumps.length) {
    gaps[adjust + jumps[index][0]] = adjust + jumps[index][1];
    adjust += jumps[index][1] - jumps[index][0] - 1;
  }
  return gaps;
}

const content = {
  tokenize: tokenizeContent,
  resolve: resolveContent
};
const continuationConstruct = {
  tokenize: tokenizeContinuation,
  partial: true
};
function resolveContent(events) {
  subtokenize(events);
  return events;
}
function tokenizeContent(effects, ok) {
  let previous;
  return chunkStart;
  function chunkStart(code) {
    effects.enter("content");
    previous = effects.enter("chunkContent", {
      contentType: "content"
    });
    return chunkInside(code);
  }
  function chunkInside(code) {
    if (code === null) {
      return contentEnd(code);
    }
    if (markdownLineEnding(code)) {
      return effects.check(continuationConstruct, contentContinue, contentEnd)(code);
    }
    effects.consume(code);
    return chunkInside;
  }
  function contentEnd(code) {
    effects.exit("chunkContent");
    effects.exit("content");
    return ok(code);
  }
  function contentContinue(code) {
    effects.consume(code);
    effects.exit("chunkContent");
    previous.next = effects.enter("chunkContent", {
      contentType: "content",
      previous
    });
    previous = previous.next;
    return chunkInside;
  }
}
function tokenizeContinuation(effects, ok, nok) {
  const self = this;
  return startLookahead;
  function startLookahead(code) {
    effects.exit("chunkContent");
    effects.enter("lineEnding");
    effects.consume(code);
    effects.exit("lineEnding");
    return factorySpace(effects, prefixed, "linePrefix");
  }
  function prefixed(code) {
    if (code === null || markdownLineEnding(code)) {
      return nok(code);
    }
    const tail = self.events[self.events.length - 1];
    if (!self.parser.constructs.disable.null.includes('codeIndented') && tail && tail[1].type === "linePrefix" && tail[2].sliceSerialize(tail[1], true).length >= 4) {
      return ok(code);
    }
    return effects.interrupt(self.parser.constructs.flow, nok, ok)(code);
  }
}

function factoryDestination(
  effects,
  ok,
  nok,
  type,
  literalType,
  literalMarkerType,
  rawType,
  stringType,
  max
) {
  const limit = max || Number.POSITIVE_INFINITY;
  let balance = 0;
  return start
  function start(code) {
    if (code === 60) {
      effects.enter(type);
      effects.enter(literalType);
      effects.enter(literalMarkerType);
      effects.consume(code);
      effects.exit(literalMarkerType);
      return enclosedBefore
    }
    if (code === null || code === 32 || code === 41 || asciiControl(code)) {
      return nok(code)
    }
    effects.enter(type);
    effects.enter(rawType);
    effects.enter(stringType);
    effects.enter('chunkString', {
      contentType: 'string'
    });
    return raw(code)
  }
  function enclosedBefore(code) {
    if (code === 62) {
      effects.enter(literalMarkerType);
      effects.consume(code);
      effects.exit(literalMarkerType);
      effects.exit(literalType);
      effects.exit(type);
      return ok
    }
    effects.enter(stringType);
    effects.enter('chunkString', {
      contentType: 'string'
    });
    return enclosed(code)
  }
  function enclosed(code) {
    if (code === 62) {
      effects.exit('chunkString');
      effects.exit(stringType);
      return enclosedBefore(code)
    }
    if (code === null || code === 60 || markdownLineEnding(code)) {
      return nok(code)
    }
    effects.consume(code);
    return code === 92 ? enclosedEscape : enclosed
  }
  function enclosedEscape(code) {
    if (code === 60 || code === 62 || code === 92) {
      effects.consume(code);
      return enclosed
    }
    return enclosed(code)
  }
  function raw(code) {
    if (
      !balance &&
      (code === null || code === 41 || markdownLineEndingOrSpace(code))
    ) {
      effects.exit('chunkString');
      effects.exit(stringType);
      effects.exit(rawType);
      effects.exit(type);
      return ok(code)
    }
    if (balance < limit && code === 40) {
      effects.consume(code);
      balance++;
      return raw
    }
    if (code === 41) {
      effects.consume(code);
      balance--;
      return raw
    }
    if (code === null || code === 32 || code === 40 || asciiControl(code)) {
      return nok(code)
    }
    effects.consume(code);
    return code === 92 ? rawEscape : raw
  }
  function rawEscape(code) {
    if (code === 40 || code === 41 || code === 92) {
      effects.consume(code);
      return raw
    }
    return raw(code)
  }
}

function factoryLabel(effects, ok, nok, type, markerType, stringType) {
  const self = this;
  let size = 0;
  let seen;
  return start
  function start(code) {
    effects.enter(type);
    effects.enter(markerType);
    effects.consume(code);
    effects.exit(markerType);
    effects.enter(stringType);
    return atBreak
  }
  function atBreak(code) {
    if (
      size > 999 ||
      code === null ||
      code === 91 ||
      (code === 93 && !seen) ||
      (code === 94 &&
        !size &&
        '_hiddenFootnoteSupport' in self.parser.constructs)
    ) {
      return nok(code)
    }
    if (code === 93) {
      effects.exit(stringType);
      effects.enter(markerType);
      effects.consume(code);
      effects.exit(markerType);
      effects.exit(type);
      return ok
    }
    if (markdownLineEnding(code)) {
      effects.enter('lineEnding');
      effects.consume(code);
      effects.exit('lineEnding');
      return atBreak
    }
    effects.enter('chunkString', {
      contentType: 'string'
    });
    return labelInside(code)
  }
  function labelInside(code) {
    if (
      code === null ||
      code === 91 ||
      code === 93 ||
      markdownLineEnding(code) ||
      size++ > 999
    ) {
      effects.exit('chunkString');
      return atBreak(code)
    }
    effects.consume(code);
    if (!seen) seen = !markdownSpace(code);
    return code === 92 ? labelEscape : labelInside
  }
  function labelEscape(code) {
    if (code === 91 || code === 92 || code === 93) {
      effects.consume(code);
      size++;
      return labelInside
    }
    return labelInside(code)
  }
}

function factoryTitle(effects, ok, nok, type, markerType, stringType) {
  let marker;
  return start
  function start(code) {
    if (code === 34 || code === 39 || code === 40) {
      effects.enter(type);
      effects.enter(markerType);
      effects.consume(code);
      effects.exit(markerType);
      marker = code === 40 ? 41 : code;
      return begin
    }
    return nok(code)
  }
  function begin(code) {
    if (code === marker) {
      effects.enter(markerType);
      effects.consume(code);
      effects.exit(markerType);
      effects.exit(type);
      return ok
    }
    effects.enter(stringType);
    return atBreak(code)
  }
  function atBreak(code) {
    if (code === marker) {
      effects.exit(stringType);
      return begin(marker)
    }
    if (code === null) {
      return nok(code)
    }
    if (markdownLineEnding(code)) {
      effects.enter('lineEnding');
      effects.consume(code);
      effects.exit('lineEnding');
      return factorySpace(effects, atBreak, 'linePrefix')
    }
    effects.enter('chunkString', {
      contentType: 'string'
    });
    return inside(code)
  }
  function inside(code) {
    if (code === marker || code === null || markdownLineEnding(code)) {
      effects.exit('chunkString');
      return atBreak(code)
    }
    effects.consume(code);
    return code === 92 ? escape : inside
  }
  function escape(code) {
    if (code === marker || code === 92) {
      effects.consume(code);
      return inside
    }
    return inside(code)
  }
}

function factoryWhitespace(effects, ok) {
  let seen;
  return start
  function start(code) {
    if (markdownLineEnding(code)) {
      effects.enter('lineEnding');
      effects.consume(code);
      effects.exit('lineEnding');
      seen = true;
      return start
    }
    if (markdownSpace(code)) {
      return factorySpace(
        effects,
        start,
        seen ? 'linePrefix' : 'lineSuffix'
      )(code)
    }
    return ok(code)
  }
}

const definition$1 = {
  name: 'definition',
  tokenize: tokenizeDefinition
};
const titleBefore = {
  tokenize: tokenizeTitleBefore,
  partial: true
};
function tokenizeDefinition(effects, ok, nok) {
  const self = this;
  let identifier;
  return start;
  function start(code) {
    effects.enter("definition");
    return before(code);
  }
  function before(code) {
    return factoryLabel.call(self, effects, labelAfter,
    nok, "definitionLabel", "definitionLabelMarker", "definitionLabelString")(code);
  }
  function labelAfter(code) {
    identifier = normalizeIdentifier(self.sliceSerialize(self.events[self.events.length - 1][1]).slice(1, -1));
    if (code === 58) {
      effects.enter("definitionMarker");
      effects.consume(code);
      effects.exit("definitionMarker");
      return markerAfter;
    }
    return nok(code);
  }
  function markerAfter(code) {
    return markdownLineEndingOrSpace(code) ? factoryWhitespace(effects, destinationBefore)(code) : destinationBefore(code);
  }
  function destinationBefore(code) {
    return factoryDestination(effects, destinationAfter,
    nok, "definitionDestination", "definitionDestinationLiteral", "definitionDestinationLiteralMarker", "definitionDestinationRaw", "definitionDestinationString")(code);
  }
  function destinationAfter(code) {
    return effects.attempt(titleBefore, after, after)(code);
  }
  function after(code) {
    return markdownSpace(code) ? factorySpace(effects, afterWhitespace, "whitespace")(code) : afterWhitespace(code);
  }
  function afterWhitespace(code) {
    if (code === null || markdownLineEnding(code)) {
      effects.exit("definition");
      self.parser.defined.push(identifier);
      return ok(code);
    }
    return nok(code);
  }
}
function tokenizeTitleBefore(effects, ok, nok) {
  return titleBefore;
  function titleBefore(code) {
    return markdownLineEndingOrSpace(code) ? factoryWhitespace(effects, beforeMarker)(code) : nok(code);
  }
  function beforeMarker(code) {
    return factoryTitle(effects, titleAfter, nok, "definitionTitle", "definitionTitleMarker", "definitionTitleString")(code);
  }
  function titleAfter(code) {
    return markdownSpace(code) ? factorySpace(effects, titleAfterOptionalWhitespace, "whitespace")(code) : titleAfterOptionalWhitespace(code);
  }
  function titleAfterOptionalWhitespace(code) {
    return code === null || markdownLineEnding(code) ? ok(code) : nok(code);
  }
}

const hardBreakEscape = {
  name: 'hardBreakEscape',
  tokenize: tokenizeHardBreakEscape
};
function tokenizeHardBreakEscape(effects, ok, nok) {
  return start;
  function start(code) {
    effects.enter("hardBreakEscape");
    effects.consume(code);
    return after;
  }
  function after(code) {
    if (markdownLineEnding(code)) {
      effects.exit("hardBreakEscape");
      return ok(code);
    }
    return nok(code);
  }
}

const headingAtx = {
  name: 'headingAtx',
  tokenize: tokenizeHeadingAtx,
  resolve: resolveHeadingAtx
};
function resolveHeadingAtx(events, context) {
  let contentEnd = events.length - 2;
  let contentStart = 3;
  let content;
  let text;
  if (events[contentStart][1].type === "whitespace") {
    contentStart += 2;
  }
  if (contentEnd - 2 > contentStart && events[contentEnd][1].type === "whitespace") {
    contentEnd -= 2;
  }
  if (events[contentEnd][1].type === "atxHeadingSequence" && (contentStart === contentEnd - 1 || contentEnd - 4 > contentStart && events[contentEnd - 2][1].type === "whitespace")) {
    contentEnd -= contentStart + 1 === contentEnd ? 2 : 4;
  }
  if (contentEnd > contentStart) {
    content = {
      type: "atxHeadingText",
      start: events[contentStart][1].start,
      end: events[contentEnd][1].end
    };
    text = {
      type: "chunkText",
      start: events[contentStart][1].start,
      end: events[contentEnd][1].end,
      contentType: "text"
    };
    splice(events, contentStart, contentEnd - contentStart + 1, [['enter', content, context], ['enter', text, context], ['exit', text, context], ['exit', content, context]]);
  }
  return events;
}
function tokenizeHeadingAtx(effects, ok, nok) {
  let size = 0;
  return start;
  function start(code) {
    effects.enter("atxHeading");
    return before(code);
  }
  function before(code) {
    effects.enter("atxHeadingSequence");
    return sequenceOpen(code);
  }
  function sequenceOpen(code) {
    if (code === 35 && size++ < 6) {
      effects.consume(code);
      return sequenceOpen;
    }
    if (code === null || markdownLineEndingOrSpace(code)) {
      effects.exit("atxHeadingSequence");
      return atBreak(code);
    }
    return nok(code);
  }
  function atBreak(code) {
    if (code === 35) {
      effects.enter("atxHeadingSequence");
      return sequenceFurther(code);
    }
    if (code === null || markdownLineEnding(code)) {
      effects.exit("atxHeading");
      return ok(code);
    }
    if (markdownSpace(code)) {
      return factorySpace(effects, atBreak, "whitespace")(code);
    }
    effects.enter("atxHeadingText");
    return data(code);
  }
  function sequenceFurther(code) {
    if (code === 35) {
      effects.consume(code);
      return sequenceFurther;
    }
    effects.exit("atxHeadingSequence");
    return atBreak(code);
  }
  function data(code) {
    if (code === null || code === 35 || markdownLineEndingOrSpace(code)) {
      effects.exit("atxHeadingText");
      return atBreak(code);
    }
    effects.consume(code);
    return data;
  }
}

const htmlBlockNames = [
  'address',
  'article',
  'aside',
  'base',
  'basefont',
  'blockquote',
  'body',
  'caption',
  'center',
  'col',
  'colgroup',
  'dd',
  'details',
  'dialog',
  'dir',
  'div',
  'dl',
  'dt',
  'fieldset',
  'figcaption',
  'figure',
  'footer',
  'form',
  'frame',
  'frameset',
  'h1',
  'h2',
  'h3',
  'h4',
  'h5',
  'h6',
  'head',
  'header',
  'hr',
  'html',
  'iframe',
  'legend',
  'li',
  'link',
  'main',
  'menu',
  'menuitem',
  'nav',
  'noframes',
  'ol',
  'optgroup',
  'option',
  'p',
  'param',
  'search',
  'section',
  'summary',
  'table',
  'tbody',
  'td',
  'tfoot',
  'th',
  'thead',
  'title',
  'tr',
  'track',
  'ul'
];
const htmlRawNames = ['pre', 'script', 'style', 'textarea'];

const htmlFlow = {
  name: 'htmlFlow',
  tokenize: tokenizeHtmlFlow,
  resolveTo: resolveToHtmlFlow,
  concrete: true
};
const blankLineBefore = {
  tokenize: tokenizeBlankLineBefore,
  partial: true
};
const nonLazyContinuationStart = {
  tokenize: tokenizeNonLazyContinuationStart,
  partial: true
};
function resolveToHtmlFlow(events) {
  let index = events.length;
  while (index--) {
    if (events[index][0] === 'enter' && events[index][1].type === "htmlFlow") {
      break;
    }
  }
  if (index > 1 && events[index - 2][1].type === "linePrefix") {
    events[index][1].start = events[index - 2][1].start;
    events[index + 1][1].start = events[index - 2][1].start;
    events.splice(index - 2, 2);
  }
  return events;
}
function tokenizeHtmlFlow(effects, ok, nok) {
  const self = this;
  let marker;
  let closingTag;
  let buffer;
  let index;
  let markerB;
  return start;
  function start(code) {
    return before(code);
  }
  function before(code) {
    effects.enter("htmlFlow");
    effects.enter("htmlFlowData");
    effects.consume(code);
    return open;
  }
  function open(code) {
    if (code === 33) {
      effects.consume(code);
      return declarationOpen;
    }
    if (code === 47) {
      effects.consume(code);
      closingTag = true;
      return tagCloseStart;
    }
    if (code === 63) {
      effects.consume(code);
      marker = 3;
      return self.interrupt ? ok : continuationDeclarationInside;
    }
    if (asciiAlpha(code)) {
      effects.consume(code);
      buffer = String.fromCharCode(code);
      return tagName;
    }
    return nok(code);
  }
  function declarationOpen(code) {
    if (code === 45) {
      effects.consume(code);
      marker = 2;
      return commentOpenInside;
    }
    if (code === 91) {
      effects.consume(code);
      marker = 5;
      index = 0;
      return cdataOpenInside;
    }
    if (asciiAlpha(code)) {
      effects.consume(code);
      marker = 4;
      return self.interrupt ? ok : continuationDeclarationInside;
    }
    return nok(code);
  }
  function commentOpenInside(code) {
    if (code === 45) {
      effects.consume(code);
      return self.interrupt ? ok : continuationDeclarationInside;
    }
    return nok(code);
  }
  function cdataOpenInside(code) {
    const value = "CDATA[";
    if (code === value.charCodeAt(index++)) {
      effects.consume(code);
      if (index === value.length) {
        return self.interrupt ? ok : continuation;
      }
      return cdataOpenInside;
    }
    return nok(code);
  }
  function tagCloseStart(code) {
    if (asciiAlpha(code)) {
      effects.consume(code);
      buffer = String.fromCharCode(code);
      return tagName;
    }
    return nok(code);
  }
  function tagName(code) {
    if (code === null || code === 47 || code === 62 || markdownLineEndingOrSpace(code)) {
      const slash = code === 47;
      const name = buffer.toLowerCase();
      if (!slash && !closingTag && htmlRawNames.includes(name)) {
        marker = 1;
        return self.interrupt ? ok(code) : continuation(code);
      }
      if (htmlBlockNames.includes(buffer.toLowerCase())) {
        marker = 6;
        if (slash) {
          effects.consume(code);
          return basicSelfClosing;
        }
        return self.interrupt ? ok(code) : continuation(code);
      }
      marker = 7;
      return self.interrupt && !self.parser.lazy[self.now().line] ? nok(code) : closingTag ? completeClosingTagAfter(code) : completeAttributeNameBefore(code);
    }
    if (code === 45 || asciiAlphanumeric(code)) {
      effects.consume(code);
      buffer += String.fromCharCode(code);
      return tagName;
    }
    return nok(code);
  }
  function basicSelfClosing(code) {
    if (code === 62) {
      effects.consume(code);
      return self.interrupt ? ok : continuation;
    }
    return nok(code);
  }
  function completeClosingTagAfter(code) {
    if (markdownSpace(code)) {
      effects.consume(code);
      return completeClosingTagAfter;
    }
    return completeEnd(code);
  }
  function completeAttributeNameBefore(code) {
    if (code === 47) {
      effects.consume(code);
      return completeEnd;
    }
    if (code === 58 || code === 95 || asciiAlpha(code)) {
      effects.consume(code);
      return completeAttributeName;
    }
    if (markdownSpace(code)) {
      effects.consume(code);
      return completeAttributeNameBefore;
    }
    return completeEnd(code);
  }
  function completeAttributeName(code) {
    if (code === 45 || code === 46 || code === 58 || code === 95 || asciiAlphanumeric(code)) {
      effects.consume(code);
      return completeAttributeName;
    }
    return completeAttributeNameAfter(code);
  }
  function completeAttributeNameAfter(code) {
    if (code === 61) {
      effects.consume(code);
      return completeAttributeValueBefore;
    }
    if (markdownSpace(code)) {
      effects.consume(code);
      return completeAttributeNameAfter;
    }
    return completeAttributeNameBefore(code);
  }
  function completeAttributeValueBefore(code) {
    if (code === null || code === 60 || code === 61 || code === 62 || code === 96) {
      return nok(code);
    }
    if (code === 34 || code === 39) {
      effects.consume(code);
      markerB = code;
      return completeAttributeValueQuoted;
    }
    if (markdownSpace(code)) {
      effects.consume(code);
      return completeAttributeValueBefore;
    }
    return completeAttributeValueUnquoted(code);
  }
  function completeAttributeValueQuoted(code) {
    if (code === markerB) {
      effects.consume(code);
      markerB = null;
      return completeAttributeValueQuotedAfter;
    }
    if (code === null || markdownLineEnding(code)) {
      return nok(code);
    }
    effects.consume(code);
    return completeAttributeValueQuoted;
  }
  function completeAttributeValueUnquoted(code) {
    if (code === null || code === 34 || code === 39 || code === 47 || code === 60 || code === 61 || code === 62 || code === 96 || markdownLineEndingOrSpace(code)) {
      return completeAttributeNameAfter(code);
    }
    effects.consume(code);
    return completeAttributeValueUnquoted;
  }
  function completeAttributeValueQuotedAfter(code) {
    if (code === 47 || code === 62 || markdownSpace(code)) {
      return completeAttributeNameBefore(code);
    }
    return nok(code);
  }
  function completeEnd(code) {
    if (code === 62) {
      effects.consume(code);
      return completeAfter;
    }
    return nok(code);
  }
  function completeAfter(code) {
    if (code === null || markdownLineEnding(code)) {
      return continuation(code);
    }
    if (markdownSpace(code)) {
      effects.consume(code);
      return completeAfter;
    }
    return nok(code);
  }
  function continuation(code) {
    if (code === 45 && marker === 2) {
      effects.consume(code);
      return continuationCommentInside;
    }
    if (code === 60 && marker === 1) {
      effects.consume(code);
      return continuationRawTagOpen;
    }
    if (code === 62 && marker === 4) {
      effects.consume(code);
      return continuationClose;
    }
    if (code === 63 && marker === 3) {
      effects.consume(code);
      return continuationDeclarationInside;
    }
    if (code === 93 && marker === 5) {
      effects.consume(code);
      return continuationCdataInside;
    }
    if (markdownLineEnding(code) && (marker === 6 || marker === 7)) {
      effects.exit("htmlFlowData");
      return effects.check(blankLineBefore, continuationAfter, continuationStart)(code);
    }
    if (code === null || markdownLineEnding(code)) {
      effects.exit("htmlFlowData");
      return continuationStart(code);
    }
    effects.consume(code);
    return continuation;
  }
  function continuationStart(code) {
    return effects.check(nonLazyContinuationStart, continuationStartNonLazy, continuationAfter)(code);
  }
  function continuationStartNonLazy(code) {
    effects.enter("lineEnding");
    effects.consume(code);
    effects.exit("lineEnding");
    return continuationBefore;
  }
  function continuationBefore(code) {
    if (code === null || markdownLineEnding(code)) {
      return continuationStart(code);
    }
    effects.enter("htmlFlowData");
    return continuation(code);
  }
  function continuationCommentInside(code) {
    if (code === 45) {
      effects.consume(code);
      return continuationDeclarationInside;
    }
    return continuation(code);
  }
  function continuationRawTagOpen(code) {
    if (code === 47) {
      effects.consume(code);
      buffer = '';
      return continuationRawEndTag;
    }
    return continuation(code);
  }
  function continuationRawEndTag(code) {
    if (code === 62) {
      const name = buffer.toLowerCase();
      if (htmlRawNames.includes(name)) {
        effects.consume(code);
        return continuationClose;
      }
      return continuation(code);
    }
    if (asciiAlpha(code) && buffer.length < 8) {
      effects.consume(code);
      buffer += String.fromCharCode(code);
      return continuationRawEndTag;
    }
    return continuation(code);
  }
  function continuationCdataInside(code) {
    if (code === 93) {
      effects.consume(code);
      return continuationDeclarationInside;
    }
    return continuation(code);
  }
  function continuationDeclarationInside(code) {
    if (code === 62) {
      effects.consume(code);
      return continuationClose;
    }
    if (code === 45 && marker === 2) {
      effects.consume(code);
      return continuationDeclarationInside;
    }
    return continuation(code);
  }
  function continuationClose(code) {
    if (code === null || markdownLineEnding(code)) {
      effects.exit("htmlFlowData");
      return continuationAfter(code);
    }
    effects.consume(code);
    return continuationClose;
  }
  function continuationAfter(code) {
    effects.exit("htmlFlow");
    return ok(code);
  }
}
function tokenizeNonLazyContinuationStart(effects, ok, nok) {
  const self = this;
  return start;
  function start(code) {
    if (markdownLineEnding(code)) {
      effects.enter("lineEnding");
      effects.consume(code);
      effects.exit("lineEnding");
      return after;
    }
    return nok(code);
  }
  function after(code) {
    return self.parser.lazy[self.now().line] ? nok(code) : ok(code);
  }
}
function tokenizeBlankLineBefore(effects, ok, nok) {
  return start;
  function start(code) {
    effects.enter("lineEnding");
    effects.consume(code);
    effects.exit("lineEnding");
    return effects.attempt(blankLine, ok, nok);
  }
}

const htmlText = {
  name: 'htmlText',
  tokenize: tokenizeHtmlText
};
function tokenizeHtmlText(effects, ok, nok) {
  const self = this;
  let marker;
  let index;
  let returnState;
  return start;
  function start(code) {
    effects.enter("htmlText");
    effects.enter("htmlTextData");
    effects.consume(code);
    return open;
  }
  function open(code) {
    if (code === 33) {
      effects.consume(code);
      return declarationOpen;
    }
    if (code === 47) {
      effects.consume(code);
      return tagCloseStart;
    }
    if (code === 63) {
      effects.consume(code);
      return instruction;
    }
    if (asciiAlpha(code)) {
      effects.consume(code);
      return tagOpen;
    }
    return nok(code);
  }
  function declarationOpen(code) {
    if (code === 45) {
      effects.consume(code);
      return commentOpenInside;
    }
    if (code === 91) {
      effects.consume(code);
      index = 0;
      return cdataOpenInside;
    }
    if (asciiAlpha(code)) {
      effects.consume(code);
      return declaration;
    }
    return nok(code);
  }
  function commentOpenInside(code) {
    if (code === 45) {
      effects.consume(code);
      return commentEnd;
    }
    return nok(code);
  }
  function comment(code) {
    if (code === null) {
      return nok(code);
    }
    if (code === 45) {
      effects.consume(code);
      return commentClose;
    }
    if (markdownLineEnding(code)) {
      returnState = comment;
      return lineEndingBefore(code);
    }
    effects.consume(code);
    return comment;
  }
  function commentClose(code) {
    if (code === 45) {
      effects.consume(code);
      return commentEnd;
    }
    return comment(code);
  }
  function commentEnd(code) {
    return code === 62 ? end(code) : code === 45 ? commentClose(code) : comment(code);
  }
  function cdataOpenInside(code) {
    const value = "CDATA[";
    if (code === value.charCodeAt(index++)) {
      effects.consume(code);
      return index === value.length ? cdata : cdataOpenInside;
    }
    return nok(code);
  }
  function cdata(code) {
    if (code === null) {
      return nok(code);
    }
    if (code === 93) {
      effects.consume(code);
      return cdataClose;
    }
    if (markdownLineEnding(code)) {
      returnState = cdata;
      return lineEndingBefore(code);
    }
    effects.consume(code);
    return cdata;
  }
  function cdataClose(code) {
    if (code === 93) {
      effects.consume(code);
      return cdataEnd;
    }
    return cdata(code);
  }
  function cdataEnd(code) {
    if (code === 62) {
      return end(code);
    }
    if (code === 93) {
      effects.consume(code);
      return cdataEnd;
    }
    return cdata(code);
  }
  function declaration(code) {
    if (code === null || code === 62) {
      return end(code);
    }
    if (markdownLineEnding(code)) {
      returnState = declaration;
      return lineEndingBefore(code);
    }
    effects.consume(code);
    return declaration;
  }
  function instruction(code) {
    if (code === null) {
      return nok(code);
    }
    if (code === 63) {
      effects.consume(code);
      return instructionClose;
    }
    if (markdownLineEnding(code)) {
      returnState = instruction;
      return lineEndingBefore(code);
    }
    effects.consume(code);
    return instruction;
  }
  function instructionClose(code) {
    return code === 62 ? end(code) : instruction(code);
  }
  function tagCloseStart(code) {
    if (asciiAlpha(code)) {
      effects.consume(code);
      return tagClose;
    }
    return nok(code);
  }
  function tagClose(code) {
    if (code === 45 || asciiAlphanumeric(code)) {
      effects.consume(code);
      return tagClose;
    }
    return tagCloseBetween(code);
  }
  function tagCloseBetween(code) {
    if (markdownLineEnding(code)) {
      returnState = tagCloseBetween;
      return lineEndingBefore(code);
    }
    if (markdownSpace(code)) {
      effects.consume(code);
      return tagCloseBetween;
    }
    return end(code);
  }
  function tagOpen(code) {
    if (code === 45 || asciiAlphanumeric(code)) {
      effects.consume(code);
      return tagOpen;
    }
    if (code === 47 || code === 62 || markdownLineEndingOrSpace(code)) {
      return tagOpenBetween(code);
    }
    return nok(code);
  }
  function tagOpenBetween(code) {
    if (code === 47) {
      effects.consume(code);
      return end;
    }
    if (code === 58 || code === 95 || asciiAlpha(code)) {
      effects.consume(code);
      return tagOpenAttributeName;
    }
    if (markdownLineEnding(code)) {
      returnState = tagOpenBetween;
      return lineEndingBefore(code);
    }
    if (markdownSpace(code)) {
      effects.consume(code);
      return tagOpenBetween;
    }
    return end(code);
  }
  function tagOpenAttributeName(code) {
    if (code === 45 || code === 46 || code === 58 || code === 95 || asciiAlphanumeric(code)) {
      effects.consume(code);
      return tagOpenAttributeName;
    }
    return tagOpenAttributeNameAfter(code);
  }
  function tagOpenAttributeNameAfter(code) {
    if (code === 61) {
      effects.consume(code);
      return tagOpenAttributeValueBefore;
    }
    if (markdownLineEnding(code)) {
      returnState = tagOpenAttributeNameAfter;
      return lineEndingBefore(code);
    }
    if (markdownSpace(code)) {
      effects.consume(code);
      return tagOpenAttributeNameAfter;
    }
    return tagOpenBetween(code);
  }
  function tagOpenAttributeValueBefore(code) {
    if (code === null || code === 60 || code === 61 || code === 62 || code === 96) {
      return nok(code);
    }
    if (code === 34 || code === 39) {
      effects.consume(code);
      marker = code;
      return tagOpenAttributeValueQuoted;
    }
    if (markdownLineEnding(code)) {
      returnState = tagOpenAttributeValueBefore;
      return lineEndingBefore(code);
    }
    if (markdownSpace(code)) {
      effects.consume(code);
      return tagOpenAttributeValueBefore;
    }
    effects.consume(code);
    return tagOpenAttributeValueUnquoted;
  }
  function tagOpenAttributeValueQuoted(code) {
    if (code === marker) {
      effects.consume(code);
      marker = undefined;
      return tagOpenAttributeValueQuotedAfter;
    }
    if (code === null) {
      return nok(code);
    }
    if (markdownLineEnding(code)) {
      returnState = tagOpenAttributeValueQuoted;
      return lineEndingBefore(code);
    }
    effects.consume(code);
    return tagOpenAttributeValueQuoted;
  }
  function tagOpenAttributeValueUnquoted(code) {
    if (code === null || code === 34 || code === 39 || code === 60 || code === 61 || code === 96) {
      return nok(code);
    }
    if (code === 47 || code === 62 || markdownLineEndingOrSpace(code)) {
      return tagOpenBetween(code);
    }
    effects.consume(code);
    return tagOpenAttributeValueUnquoted;
  }
  function tagOpenAttributeValueQuotedAfter(code) {
    if (code === 47 || code === 62 || markdownLineEndingOrSpace(code)) {
      return tagOpenBetween(code);
    }
    return nok(code);
  }
  function end(code) {
    if (code === 62) {
      effects.consume(code);
      effects.exit("htmlTextData");
      effects.exit("htmlText");
      return ok;
    }
    return nok(code);
  }
  function lineEndingBefore(code) {
    effects.exit("htmlTextData");
    effects.enter("lineEnding");
    effects.consume(code);
    effects.exit("lineEnding");
    return lineEndingAfter;
  }
  function lineEndingAfter(code) {
    return markdownSpace(code) ? factorySpace(effects, lineEndingAfterPrefix, "linePrefix", self.parser.constructs.disable.null.includes('codeIndented') ? undefined : 4)(code) : lineEndingAfterPrefix(code);
  }
  function lineEndingAfterPrefix(code) {
    effects.enter("htmlTextData");
    return returnState(code);
  }
}

const labelEnd = {
  name: 'labelEnd',
  tokenize: tokenizeLabelEnd,
  resolveTo: resolveToLabelEnd,
  resolveAll: resolveAllLabelEnd
};
const resourceConstruct = {
  tokenize: tokenizeResource
};
const referenceFullConstruct = {
  tokenize: tokenizeReferenceFull
};
const referenceCollapsedConstruct = {
  tokenize: tokenizeReferenceCollapsed
};
function resolveAllLabelEnd(events) {
  let index = -1;
  while (++index < events.length) {
    const token = events[index][1];
    if (token.type === "labelImage" || token.type === "labelLink" || token.type === "labelEnd") {
      events.splice(index + 1, token.type === "labelImage" ? 4 : 2);
      token.type = "data";
      index++;
    }
  }
  return events;
}
function resolveToLabelEnd(events, context) {
  let index = events.length;
  let offset = 0;
  let token;
  let open;
  let close;
  let media;
  while (index--) {
    token = events[index][1];
    if (open) {
      if (token.type === "link" || token.type === "labelLink" && token._inactive) {
        break;
      }
      if (events[index][0] === 'enter' && token.type === "labelLink") {
        token._inactive = true;
      }
    } else if (close) {
      if (events[index][0] === 'enter' && (token.type === "labelImage" || token.type === "labelLink") && !token._balanced) {
        open = index;
        if (token.type !== "labelLink") {
          offset = 2;
          break;
        }
      }
    } else if (token.type === "labelEnd") {
      close = index;
    }
  }
  const group = {
    type: events[open][1].type === "labelLink" ? "link" : "image",
    start: Object.assign({}, events[open][1].start),
    end: Object.assign({}, events[events.length - 1][1].end)
  };
  const label = {
    type: "label",
    start: Object.assign({}, events[open][1].start),
    end: Object.assign({}, events[close][1].end)
  };
  const text = {
    type: "labelText",
    start: Object.assign({}, events[open + offset + 2][1].end),
    end: Object.assign({}, events[close - 2][1].start)
  };
  media = [['enter', group, context], ['enter', label, context]];
  media = push(media, events.slice(open + 1, open + offset + 3));
  media = push(media, [['enter', text, context]]);
  media = push(media, resolveAll(context.parser.constructs.insideSpan.null, events.slice(open + offset + 4, close - 3), context));
  media = push(media, [['exit', text, context], events[close - 2], events[close - 1], ['exit', label, context]]);
  media = push(media, events.slice(close + 1));
  media = push(media, [['exit', group, context]]);
  splice(events, open, events.length, media);
  return events;
}
function tokenizeLabelEnd(effects, ok, nok) {
  const self = this;
  let index = self.events.length;
  let labelStart;
  let defined;
  while (index--) {
    if ((self.events[index][1].type === "labelImage" || self.events[index][1].type === "labelLink") && !self.events[index][1]._balanced) {
      labelStart = self.events[index][1];
      break;
    }
  }
  return start;
  function start(code) {
    if (!labelStart) {
      return nok(code);
    }
    if (labelStart._inactive) {
      return labelEndNok(code);
    }
    defined = self.parser.defined.includes(normalizeIdentifier(self.sliceSerialize({
      start: labelStart.end,
      end: self.now()
    })));
    effects.enter("labelEnd");
    effects.enter("labelMarker");
    effects.consume(code);
    effects.exit("labelMarker");
    effects.exit("labelEnd");
    return after;
  }
  function after(code) {
    if (code === 40) {
      return effects.attempt(resourceConstruct, labelEndOk, defined ? labelEndOk : labelEndNok)(code);
    }
    if (code === 91) {
      return effects.attempt(referenceFullConstruct, labelEndOk, defined ? referenceNotFull : labelEndNok)(code);
    }
    return defined ? labelEndOk(code) : labelEndNok(code);
  }
  function referenceNotFull(code) {
    return effects.attempt(referenceCollapsedConstruct, labelEndOk, labelEndNok)(code);
  }
  function labelEndOk(code) {
    return ok(code);
  }
  function labelEndNok(code) {
    labelStart._balanced = true;
    return nok(code);
  }
}
function tokenizeResource(effects, ok, nok) {
  return resourceStart;
  function resourceStart(code) {
    effects.enter("resource");
    effects.enter("resourceMarker");
    effects.consume(code);
    effects.exit("resourceMarker");
    return resourceBefore;
  }
  function resourceBefore(code) {
    return markdownLineEndingOrSpace(code) ? factoryWhitespace(effects, resourceOpen)(code) : resourceOpen(code);
  }
  function resourceOpen(code) {
    if (code === 41) {
      return resourceEnd(code);
    }
    return factoryDestination(effects, resourceDestinationAfter, resourceDestinationMissing, "resourceDestination", "resourceDestinationLiteral", "resourceDestinationLiteralMarker", "resourceDestinationRaw", "resourceDestinationString", 32)(code);
  }
  function resourceDestinationAfter(code) {
    return markdownLineEndingOrSpace(code) ? factoryWhitespace(effects, resourceBetween)(code) : resourceEnd(code);
  }
  function resourceDestinationMissing(code) {
    return nok(code);
  }
  function resourceBetween(code) {
    if (code === 34 || code === 39 || code === 40) {
      return factoryTitle(effects, resourceTitleAfter, nok, "resourceTitle", "resourceTitleMarker", "resourceTitleString")(code);
    }
    return resourceEnd(code);
  }
  function resourceTitleAfter(code) {
    return markdownLineEndingOrSpace(code) ? factoryWhitespace(effects, resourceEnd)(code) : resourceEnd(code);
  }
  function resourceEnd(code) {
    if (code === 41) {
      effects.enter("resourceMarker");
      effects.consume(code);
      effects.exit("resourceMarker");
      effects.exit("resource");
      return ok;
    }
    return nok(code);
  }
}
function tokenizeReferenceFull(effects, ok, nok) {
  const self = this;
  return referenceFull;
  function referenceFull(code) {
    return factoryLabel.call(self, effects, referenceFullAfter, referenceFullMissing, "reference", "referenceMarker", "referenceString")(code);
  }
  function referenceFullAfter(code) {
    return self.parser.defined.includes(normalizeIdentifier(self.sliceSerialize(self.events[self.events.length - 1][1]).slice(1, -1))) ? ok(code) : nok(code);
  }
  function referenceFullMissing(code) {
    return nok(code);
  }
}
function tokenizeReferenceCollapsed(effects, ok, nok) {
  return referenceCollapsedStart;
  function referenceCollapsedStart(code) {
    effects.enter("reference");
    effects.enter("referenceMarker");
    effects.consume(code);
    effects.exit("referenceMarker");
    return referenceCollapsedOpen;
  }
  function referenceCollapsedOpen(code) {
    if (code === 93) {
      effects.enter("referenceMarker");
      effects.consume(code);
      effects.exit("referenceMarker");
      effects.exit("reference");
      return ok;
    }
    return nok(code);
  }
}

const labelStartImage = {
  name: 'labelStartImage',
  tokenize: tokenizeLabelStartImage,
  resolveAll: labelEnd.resolveAll
};
function tokenizeLabelStartImage(effects, ok, nok) {
  const self = this;
  return start;
  function start(code) {
    effects.enter("labelImage");
    effects.enter("labelImageMarker");
    effects.consume(code);
    effects.exit("labelImageMarker");
    return open;
  }
  function open(code) {
    if (code === 91) {
      effects.enter("labelMarker");
      effects.consume(code);
      effects.exit("labelMarker");
      effects.exit("labelImage");
      return after;
    }
    return nok(code);
  }
  function after(code) {
    return code === 94 && '_hiddenFootnoteSupport' in self.parser.constructs ? nok(code) : ok(code);
  }
}

const labelStartLink = {
  name: 'labelStartLink',
  tokenize: tokenizeLabelStartLink,
  resolveAll: labelEnd.resolveAll
};
function tokenizeLabelStartLink(effects, ok, nok) {
  const self = this;
  return start;
  function start(code) {
    effects.enter("labelLink");
    effects.enter("labelMarker");
    effects.consume(code);
    effects.exit("labelMarker");
    effects.exit("labelLink");
    return after;
  }
  function after(code) {
    return code === 94 && '_hiddenFootnoteSupport' in self.parser.constructs ? nok(code) : ok(code);
  }
}

const lineEnding = {
  name: 'lineEnding',
  tokenize: tokenizeLineEnding
};
function tokenizeLineEnding(effects, ok) {
  return start;
  function start(code) {
    effects.enter("lineEnding");
    effects.consume(code);
    effects.exit("lineEnding");
    return factorySpace(effects, ok, "linePrefix");
  }
}

const thematicBreak$1 = {
  name: 'thematicBreak',
  tokenize: tokenizeThematicBreak
};
function tokenizeThematicBreak(effects, ok, nok) {
  let size = 0;
  let marker;
  return start;
  function start(code) {
    effects.enter("thematicBreak");
    return before(code);
  }
  function before(code) {
    marker = code;
    return atBreak(code);
  }
  function atBreak(code) {
    if (code === marker) {
      effects.enter("thematicBreakSequence");
      return sequence(code);
    }
    if (size >= 3 && (code === null || markdownLineEnding(code))) {
      effects.exit("thematicBreak");
      return ok(code);
    }
    return nok(code);
  }
  function sequence(code) {
    if (code === marker) {
      effects.consume(code);
      size++;
      return sequence;
    }
    effects.exit("thematicBreakSequence");
    return markdownSpace(code) ? factorySpace(effects, atBreak, "whitespace")(code) : atBreak(code);
  }
}

const list$2 = {
  name: 'list',
  tokenize: tokenizeListStart,
  continuation: {
    tokenize: tokenizeListContinuation
  },
  exit: tokenizeListEnd
};
const listItemPrefixWhitespaceConstruct = {
  tokenize: tokenizeListItemPrefixWhitespace,
  partial: true
};
const indentConstruct = {
  tokenize: tokenizeIndent$1,
  partial: true
};
function tokenizeListStart(effects, ok, nok) {
  const self = this;
  const tail = self.events[self.events.length - 1];
  let initialSize = tail && tail[1].type === "linePrefix" ? tail[2].sliceSerialize(tail[1], true).length : 0;
  let size = 0;
  return start;
  function start(code) {
    const kind = self.containerState.type || (code === 42 || code === 43 || code === 45 ? "listUnordered" : "listOrdered");
    if (kind === "listUnordered" ? !self.containerState.marker || code === self.containerState.marker : asciiDigit(code)) {
      if (!self.containerState.type) {
        self.containerState.type = kind;
        effects.enter(kind, {
          _container: true
        });
      }
      if (kind === "listUnordered") {
        effects.enter("listItemPrefix");
        return code === 42 || code === 45 ? effects.check(thematicBreak$1, nok, atMarker)(code) : atMarker(code);
      }
      if (!self.interrupt || code === 49) {
        effects.enter("listItemPrefix");
        effects.enter("listItemValue");
        return inside(code);
      }
    }
    return nok(code);
  }
  function inside(code) {
    if (asciiDigit(code) && ++size < 10) {
      effects.consume(code);
      return inside;
    }
    if ((!self.interrupt || size < 2) && (self.containerState.marker ? code === self.containerState.marker : code === 41 || code === 46)) {
      effects.exit("listItemValue");
      return atMarker(code);
    }
    return nok(code);
  }
  function atMarker(code) {
    effects.enter("listItemMarker");
    effects.consume(code);
    effects.exit("listItemMarker");
    self.containerState.marker = self.containerState.marker || code;
    return effects.check(blankLine,
    self.interrupt ? nok : onBlank, effects.attempt(listItemPrefixWhitespaceConstruct, endOfPrefix, otherPrefix));
  }
  function onBlank(code) {
    self.containerState.initialBlankLine = true;
    initialSize++;
    return endOfPrefix(code);
  }
  function otherPrefix(code) {
    if (markdownSpace(code)) {
      effects.enter("listItemPrefixWhitespace");
      effects.consume(code);
      effects.exit("listItemPrefixWhitespace");
      return endOfPrefix;
    }
    return nok(code);
  }
  function endOfPrefix(code) {
    self.containerState.size = initialSize + self.sliceSerialize(effects.exit("listItemPrefix"), true).length;
    return ok(code);
  }
}
function tokenizeListContinuation(effects, ok, nok) {
  const self = this;
  self.containerState._closeFlow = undefined;
  return effects.check(blankLine, onBlank, notBlank);
  function onBlank(code) {
    self.containerState.furtherBlankLines = self.containerState.furtherBlankLines || self.containerState.initialBlankLine;
    return factorySpace(effects, ok, "listItemIndent", self.containerState.size + 1)(code);
  }
  function notBlank(code) {
    if (self.containerState.furtherBlankLines || !markdownSpace(code)) {
      self.containerState.furtherBlankLines = undefined;
      self.containerState.initialBlankLine = undefined;
      return notInCurrentItem(code);
    }
    self.containerState.furtherBlankLines = undefined;
    self.containerState.initialBlankLine = undefined;
    return effects.attempt(indentConstruct, ok, notInCurrentItem)(code);
  }
  function notInCurrentItem(code) {
    self.containerState._closeFlow = true;
    self.interrupt = undefined;
    return factorySpace(effects, effects.attempt(list$2, ok, nok), "linePrefix", self.parser.constructs.disable.null.includes('codeIndented') ? undefined : 4)(code);
  }
}
function tokenizeIndent$1(effects, ok, nok) {
  const self = this;
  return factorySpace(effects, afterPrefix, "listItemIndent", self.containerState.size + 1);
  function afterPrefix(code) {
    const tail = self.events[self.events.length - 1];
    return tail && tail[1].type === "listItemIndent" && tail[2].sliceSerialize(tail[1], true).length === self.containerState.size ? ok(code) : nok(code);
  }
}
function tokenizeListEnd(effects) {
  effects.exit(this.containerState.type);
}
function tokenizeListItemPrefixWhitespace(effects, ok, nok) {
  const self = this;
  return factorySpace(effects, afterPrefix, "listItemPrefixWhitespace", self.parser.constructs.disable.null.includes('codeIndented') ? undefined : 4 + 1);
  function afterPrefix(code) {
    const tail = self.events[self.events.length - 1];
    return !markdownSpace(code) && tail && tail[1].type === "listItemPrefixWhitespace" ? ok(code) : nok(code);
  }
}

const setextUnderline = {
  name: 'setextUnderline',
  tokenize: tokenizeSetextUnderline,
  resolveTo: resolveToSetextUnderline
};
function resolveToSetextUnderline(events, context) {
  let index = events.length;
  let content;
  let text;
  let definition;
  while (index--) {
    if (events[index][0] === 'enter') {
      if (events[index][1].type === "content") {
        content = index;
        break;
      }
      if (events[index][1].type === "paragraph") {
        text = index;
      }
    }
    else {
      if (events[index][1].type === "content") {
        events.splice(index, 1);
      }
      if (!definition && events[index][1].type === "definition") {
        definition = index;
      }
    }
  }
  const heading = {
    type: "setextHeading",
    start: Object.assign({}, events[text][1].start),
    end: Object.assign({}, events[events.length - 1][1].end)
  };
  events[text][1].type = "setextHeadingText";
  if (definition) {
    events.splice(text, 0, ['enter', heading, context]);
    events.splice(definition + 1, 0, ['exit', events[content][1], context]);
    events[content][1].end = Object.assign({}, events[definition][1].end);
  } else {
    events[content][1] = heading;
  }
  events.push(['exit', heading, context]);
  return events;
}
function tokenizeSetextUnderline(effects, ok, nok) {
  const self = this;
  let marker;
  return start;
  function start(code) {
    let index = self.events.length;
    let paragraph;
    while (index--) {
      if (self.events[index][1].type !== "lineEnding" && self.events[index][1].type !== "linePrefix" && self.events[index][1].type !== "content") {
        paragraph = self.events[index][1].type === "paragraph";
        break;
      }
    }
    if (!self.parser.lazy[self.now().line] && (self.interrupt || paragraph)) {
      effects.enter("setextHeadingLine");
      marker = code;
      return before(code);
    }
    return nok(code);
  }
  function before(code) {
    effects.enter("setextHeadingLineSequence");
    return inside(code);
  }
  function inside(code) {
    if (code === marker) {
      effects.consume(code);
      return inside;
    }
    effects.exit("setextHeadingLineSequence");
    return markdownSpace(code) ? factorySpace(effects, after, "lineSuffix")(code) : after(code);
  }
  function after(code) {
    if (code === null || markdownLineEnding(code)) {
      effects.exit("setextHeadingLine");
      return ok(code);
    }
    return nok(code);
  }
}

const flow$1 = {
  tokenize: initializeFlow
};
function initializeFlow(effects) {
  const self = this;
  const initial = effects.attempt(
    blankLine,
    atBlankEnding,
    effects.attempt(
      this.parser.constructs.flowInitial,
      afterConstruct,
      factorySpace(
        effects,
        effects.attempt(
          this.parser.constructs.flow,
          afterConstruct,
          effects.attempt(content, afterConstruct)
        ),
        'linePrefix'
      )
    )
  );
  return initial
  function atBlankEnding(code) {
    if (code === null) {
      effects.consume(code);
      return
    }
    effects.enter('lineEndingBlank');
    effects.consume(code);
    effects.exit('lineEndingBlank');
    self.currentConstruct = undefined;
    return initial
  }
  function afterConstruct(code) {
    if (code === null) {
      effects.consume(code);
      return
    }
    effects.enter('lineEnding');
    effects.consume(code);
    effects.exit('lineEnding');
    self.currentConstruct = undefined;
    return initial
  }
}

const resolver = {
  resolveAll: createResolver()
};
const string$1 = initializeFactory('string');
const text$3 = initializeFactory('text');
function initializeFactory(field) {
  return {
    tokenize: initializeText,
    resolveAll: createResolver(
      field === 'text' ? resolveAllLineSuffixes : undefined
    )
  }
  function initializeText(effects) {
    const self = this;
    const constructs = this.parser.constructs[field];
    const text = effects.attempt(constructs, start, notText);
    return start
    function start(code) {
      return atBreak(code) ? text(code) : notText(code)
    }
    function notText(code) {
      if (code === null) {
        effects.consume(code);
        return
      }
      effects.enter('data');
      effects.consume(code);
      return data
    }
    function data(code) {
      if (atBreak(code)) {
        effects.exit('data');
        return text(code)
      }
      effects.consume(code);
      return data
    }
    function atBreak(code) {
      if (code === null) {
        return true
      }
      const list = constructs[code];
      let index = -1;
      if (list) {
        while (++index < list.length) {
          const item = list[index];
          if (!item.previous || item.previous.call(self, self.previous)) {
            return true
          }
        }
      }
      return false
    }
  }
}
function createResolver(extraResolver) {
  return resolveAllText
  function resolveAllText(events, context) {
    let index = -1;
    let enter;
    while (++index <= events.length) {
      if (enter === undefined) {
        if (events[index] && events[index][1].type === 'data') {
          enter = index;
          index++;
        }
      } else if (!events[index] || events[index][1].type !== 'data') {
        if (index !== enter + 2) {
          events[enter][1].end = events[index - 1][1].end;
          events.splice(enter + 2, index - enter - 2);
          index = enter + 2;
        }
        enter = undefined;
      }
    }
    return extraResolver ? extraResolver(events, context) : events
  }
}
function resolveAllLineSuffixes(events, context) {
  let eventIndex = 0;
  while (++eventIndex <= events.length) {
    if (
      (eventIndex === events.length ||
        events[eventIndex][1].type === 'lineEnding') &&
      events[eventIndex - 1][1].type === 'data'
    ) {
      const data = events[eventIndex - 1][1];
      const chunks = context.sliceStream(data);
      let index = chunks.length;
      let bufferIndex = -1;
      let size = 0;
      let tabs;
      while (index--) {
        const chunk = chunks[index];
        if (typeof chunk === 'string') {
          bufferIndex = chunk.length;
          while (chunk.charCodeAt(bufferIndex - 1) === 32) {
            size++;
            bufferIndex--;
          }
          if (bufferIndex) break
          bufferIndex = -1;
        }
        else if (chunk === -2) {
          tabs = true;
          size++;
        } else if (chunk === -1) ; else {
          index++;
          break
        }
      }
      if (size) {
        const token = {
          type:
            eventIndex === events.length || tabs || size < 2
              ? 'lineSuffix'
              : 'hardBreakTrailing',
          start: {
            line: data.end.line,
            column: data.end.column - size,
            offset: data.end.offset - size,
            _index: data.start._index + index,
            _bufferIndex: index
              ? bufferIndex
              : data.start._bufferIndex + bufferIndex
          },
          end: Object.assign({}, data.end)
        };
        data.end = Object.assign({}, token.start);
        if (data.start.offset === data.end.offset) {
          Object.assign(data, token);
        } else {
          events.splice(
            eventIndex,
            0,
            ['enter', token, context],
            ['exit', token, context]
          );
          eventIndex += 2;
        }
      }
      eventIndex++;
    }
  }
  return events
}

function createTokenizer(parser, initialize, from) {
  let point = Object.assign(
    from
      ? Object.assign({}, from)
      : {
          line: 1,
          column: 1,
          offset: 0
        },
    {
      _index: 0,
      _bufferIndex: -1
    }
  );
  const columnStart = {};
  const resolveAllConstructs = [];
  let chunks = [];
  let stack = [];
  const effects = {
    consume,
    enter,
    exit,
    attempt: constructFactory(onsuccessfulconstruct),
    check: constructFactory(onsuccessfulcheck),
    interrupt: constructFactory(onsuccessfulcheck, {
      interrupt: true
    })
  };
  const context = {
    previous: null,
    code: null,
    containerState: {},
    events: [],
    parser,
    sliceStream,
    sliceSerialize,
    now,
    defineSkip,
    write
  };
  let state = initialize.tokenize.call(context, effects);
  if (initialize.resolveAll) {
    resolveAllConstructs.push(initialize);
  }
  return context
  function write(slice) {
    chunks = push(chunks, slice);
    main();
    if (chunks[chunks.length - 1] !== null) {
      return []
    }
    addResult(initialize, 0);
    context.events = resolveAll(resolveAllConstructs, context.events, context);
    return context.events
  }
  function sliceSerialize(token, expandTabs) {
    return serializeChunks(sliceStream(token), expandTabs)
  }
  function sliceStream(token) {
    return sliceChunks(chunks, token)
  }
  function now() {
    const {line, column, offset, _index, _bufferIndex} = point;
    return {
      line,
      column,
      offset,
      _index,
      _bufferIndex
    }
  }
  function defineSkip(value) {
    columnStart[value.line] = value.column;
    accountForPotentialSkip();
  }
  function main() {
    let chunkIndex;
    while (point._index < chunks.length) {
      const chunk = chunks[point._index];
      if (typeof chunk === 'string') {
        chunkIndex = point._index;
        if (point._bufferIndex < 0) {
          point._bufferIndex = 0;
        }
        while (
          point._index === chunkIndex &&
          point._bufferIndex < chunk.length
        ) {
          go(chunk.charCodeAt(point._bufferIndex));
        }
      } else {
        go(chunk);
      }
    }
  }
  function go(code) {
    state = state(code);
  }
  function consume(code) {
    if (markdownLineEnding(code)) {
      point.line++;
      point.column = 1;
      point.offset += code === -3 ? 2 : 1;
      accountForPotentialSkip();
    } else if (code !== -1) {
      point.column++;
      point.offset++;
    }
    if (point._bufferIndex < 0) {
      point._index++;
    } else {
      point._bufferIndex++;
      if (point._bufferIndex === chunks[point._index].length) {
        point._bufferIndex = -1;
        point._index++;
      }
    }
    context.previous = code;
  }
  function enter(type, fields) {
    const token = fields || {};
    token.type = type;
    token.start = now();
    context.events.push(['enter', token, context]);
    stack.push(token);
    return token
  }
  function exit(type) {
    const token = stack.pop();
    token.end = now();
    context.events.push(['exit', token, context]);
    return token
  }
  function onsuccessfulconstruct(construct, info) {
    addResult(construct, info.from);
  }
  function onsuccessfulcheck(_, info) {
    info.restore();
  }
  function constructFactory(onreturn, fields) {
    return hook
    function hook(constructs, returnState, bogusState) {
      let listOfConstructs;
      let constructIndex;
      let currentConstruct;
      let info;
      return Array.isArray(constructs)
        ? handleListOfConstructs(constructs)
        : 'tokenize' in constructs
        ?
          handleListOfConstructs([constructs])
        : handleMapOfConstructs(constructs)
      function handleMapOfConstructs(map) {
        return start
        function start(code) {
          const def = code !== null && map[code];
          const all = code !== null && map.null;
          const list = [
            ...(Array.isArray(def) ? def : def ? [def] : []),
            ...(Array.isArray(all) ? all : all ? [all] : [])
          ];
          return handleListOfConstructs(list)(code)
        }
      }
      function handleListOfConstructs(list) {
        listOfConstructs = list;
        constructIndex = 0;
        if (list.length === 0) {
          return bogusState
        }
        return handleConstruct(list[constructIndex])
      }
      function handleConstruct(construct) {
        return start
        function start(code) {
          info = store();
          currentConstruct = construct;
          if (!construct.partial) {
            context.currentConstruct = construct;
          }
          if (
            construct.name &&
            context.parser.constructs.disable.null.includes(construct.name)
          ) {
            return nok()
          }
          return construct.tokenize.call(
            fields ? Object.assign(Object.create(context), fields) : context,
            effects,
            ok,
            nok
          )(code)
        }
      }
      function ok(code) {
        onreturn(currentConstruct, info);
        return returnState
      }
      function nok(code) {
        info.restore();
        if (++constructIndex < listOfConstructs.length) {
          return handleConstruct(listOfConstructs[constructIndex])
        }
        return bogusState
      }
    }
  }
  function addResult(construct, from) {
    if (construct.resolveAll && !resolveAllConstructs.includes(construct)) {
      resolveAllConstructs.push(construct);
    }
    if (construct.resolve) {
      splice(
        context.events,
        from,
        context.events.length - from,
        construct.resolve(context.events.slice(from), context)
      );
    }
    if (construct.resolveTo) {
      context.events = construct.resolveTo(context.events, context);
    }
  }
  function store() {
    const startPoint = now();
    const startPrevious = context.previous;
    const startCurrentConstruct = context.currentConstruct;
    const startEventsIndex = context.events.length;
    const startStack = Array.from(stack);
    return {
      restore,
      from: startEventsIndex
    }
    function restore() {
      point = startPoint;
      context.previous = startPrevious;
      context.currentConstruct = startCurrentConstruct;
      context.events.length = startEventsIndex;
      stack = startStack;
      accountForPotentialSkip();
    }
  }
  function accountForPotentialSkip() {
    if (point.line in columnStart && point.column < 2) {
      point.column = columnStart[point.line];
      point.offset += columnStart[point.line] - 1;
    }
  }
}
function sliceChunks(chunks, token) {
  const startIndex = token.start._index;
  const startBufferIndex = token.start._bufferIndex;
  const endIndex = token.end._index;
  const endBufferIndex = token.end._bufferIndex;
  let view;
  if (startIndex === endIndex) {
    view = [chunks[startIndex].slice(startBufferIndex, endBufferIndex)];
  } else {
    view = chunks.slice(startIndex, endIndex);
    if (startBufferIndex > -1) {
      const head = view[0];
      if (typeof head === 'string') {
        view[0] = head.slice(startBufferIndex);
      } else {
        view.shift();
      }
    }
    if (endBufferIndex > 0) {
      view.push(chunks[endIndex].slice(0, endBufferIndex));
    }
  }
  return view
}
function serializeChunks(chunks, expandTabs) {
  let index = -1;
  const result = [];
  let atTab;
  while (++index < chunks.length) {
    const chunk = chunks[index];
    let value;
    if (typeof chunk === 'string') {
      value = chunk;
    } else
      switch (chunk) {
        case -5: {
          value = '\r';
          break
        }
        case -4: {
          value = '\n';
          break
        }
        case -3: {
          value = '\r' + '\n';
          break
        }
        case -2: {
          value = expandTabs ? ' ' : '\t';
          break
        }
        case -1: {
          if (!expandTabs && atTab) continue
          value = ' ';
          break
        }
        default: {
          value = String.fromCharCode(chunk);
        }
      }
    atTab = chunk === -2;
    result.push(value);
  }
  return result.join('')
}

const document = {
  [42]: list$2,
  [43]: list$2,
  [45]: list$2,
  [48]: list$2,
  [49]: list$2,
  [50]: list$2,
  [51]: list$2,
  [52]: list$2,
  [53]: list$2,
  [54]: list$2,
  [55]: list$2,
  [56]: list$2,
  [57]: list$2,
  [62]: blockQuote
};
const contentInitial = {
  [91]: definition$1
};
const flowInitial = {
  [-2]: codeIndented,
  [-1]: codeIndented,
  [32]: codeIndented
};
const flow = {
  [35]: headingAtx,
  [42]: thematicBreak$1,
  [45]: [setextUnderline, thematicBreak$1],
  [60]: htmlFlow,
  [61]: setextUnderline,
  [95]: thematicBreak$1,
  [96]: codeFenced,
  [126]: codeFenced
};
const string = {
  [38]: characterReference,
  [92]: characterEscape
};
const text$2 = {
  [-5]: lineEnding,
  [-4]: lineEnding,
  [-3]: lineEnding,
  [33]: labelStartImage,
  [38]: characterReference,
  [42]: attention,
  [60]: [autolink, htmlText],
  [91]: labelStartLink,
  [92]: [hardBreakEscape, characterEscape],
  [93]: labelEnd,
  [95]: attention,
  [96]: codeText
};
const insideSpan = {
  null: [attention, resolver]
};
const attentionMarkers = {
  null: [42, 95]
};
const disable = {
  null: []
};

var defaultConstructs = /*#__PURE__*/Object.freeze({
  __proto__: null,
  attentionMarkers: attentionMarkers,
  contentInitial: contentInitial,
  disable: disable,
  document: document,
  flow: flow,
  flowInitial: flowInitial,
  insideSpan: insideSpan,
  string: string,
  text: text$2
});

function parse$2(options) {
  const settings = options || {};
  const constructs =
    combineExtensions([defaultConstructs, ...(settings.extensions || [])]);
  const parser = {
    defined: [],
    lazy: {},
    constructs,
    content: create(content$1),
    document: create(document$1),
    flow: create(flow$1),
    string: create(string$1),
    text: create(text$3)
  };
  return parser
  function create(initial) {
    return creator
    function creator(from) {
      return createTokenizer(parser, initial, from)
    }
  }
}

function postprocess(events) {
  while (!subtokenize(events)) {
  }
  return events
}

const search$1 = /[\0\t\n\r]/g;
function preprocess() {
  let column = 1;
  let buffer = '';
  let start = true;
  let atCarriageReturn;
  return preprocessor
  function preprocessor(value, encoding, end) {
    const chunks = [];
    let match;
    let next;
    let startPosition;
    let endPosition;
    let code;
    value =
      buffer +
      (typeof value === 'string'
        ? value.toString()
        : new TextDecoder(encoding || undefined).decode(value));
    startPosition = 0;
    buffer = '';
    if (start) {
      if (value.charCodeAt(0) === 65279) {
        startPosition++;
      }
      start = undefined;
    }
    while (startPosition < value.length) {
      search$1.lastIndex = startPosition;
      match = search$1.exec(value);
      endPosition =
        match && match.index !== undefined ? match.index : value.length;
      code = value.charCodeAt(endPosition);
      if (!match) {
        buffer = value.slice(startPosition);
        break
      }
      if (code === 10 && startPosition === endPosition && atCarriageReturn) {
        chunks.push(-3);
        atCarriageReturn = undefined;
      } else {
        if (atCarriageReturn) {
          chunks.push(-5);
          atCarriageReturn = undefined;
        }
        if (startPosition < endPosition) {
          chunks.push(value.slice(startPosition, endPosition));
          column += endPosition - startPosition;
        }
        switch (code) {
          case 0: {
            chunks.push(65533);
            column++;
            break
          }
          case 9: {
            next = Math.ceil(column / 4) * 4;
            chunks.push(-2);
            while (column++ < next) chunks.push(-1);
            break
          }
          case 10: {
            chunks.push(-4);
            column = 1;
            break
          }
          default: {
            atCarriageReturn = true;
            column = 1;
          }
        }
      }
      startPosition = endPosition + 1;
    }
    if (end) {
      if (atCarriageReturn) chunks.push(-5);
      if (buffer) chunks.push(buffer);
      chunks.push(null);
    }
    return chunks
  }
}

const characterEscapeOrReference =
  /\\([!-/:-@[-`{-~])|&(#(?:\d{1,7}|x[\da-f]{1,6})|[\da-z]{1,31});/gi;
function decodeString(value) {
  return value.replace(characterEscapeOrReference, decode)
}
function decode($0, $1, $2) {
  if ($1) {
    return $1
  }
  const head = $2.charCodeAt(0);
  if (head === 35) {
    const head = $2.charCodeAt(1);
    const hex = head === 120 || head === 88;
    return decodeNumericCharacterReference($2.slice(hex ? 2 : 1), hex ? 16 : 10)
  }
  return decodeNamedCharacterReference($2) || $0
}

const own$3 = {}.hasOwnProperty;
function fromMarkdown(value, encoding, options) {
  if (typeof encoding !== 'string') {
    options = encoding;
    encoding = undefined;
  }
  return compiler(options)(postprocess(parse$2(options).document().write(preprocess()(value, encoding, true))));
}
function compiler(options) {
  const config = {
    transforms: [],
    canContainEols: ['emphasis', 'fragment', 'heading', 'paragraph', 'strong'],
    enter: {
      autolink: opener(link),
      autolinkProtocol: onenterdata,
      autolinkEmail: onenterdata,
      atxHeading: opener(heading),
      blockQuote: opener(blockQuote),
      characterEscape: onenterdata,
      characterReference: onenterdata,
      codeFenced: opener(codeFlow),
      codeFencedFenceInfo: buffer,
      codeFencedFenceMeta: buffer,
      codeIndented: opener(codeFlow, buffer),
      codeText: opener(codeText, buffer),
      codeTextData: onenterdata,
      data: onenterdata,
      codeFlowValue: onenterdata,
      definition: opener(definition),
      definitionDestinationString: buffer,
      definitionLabelString: buffer,
      definitionTitleString: buffer,
      emphasis: opener(emphasis),
      hardBreakEscape: opener(hardBreak),
      hardBreakTrailing: opener(hardBreak),
      htmlFlow: opener(html, buffer),
      htmlFlowData: onenterdata,
      htmlText: opener(html, buffer),
      htmlTextData: onenterdata,
      image: opener(image),
      label: buffer,
      link: opener(link),
      listItem: opener(listItem),
      listItemValue: onenterlistitemvalue,
      listOrdered: opener(list, onenterlistordered),
      listUnordered: opener(list),
      paragraph: opener(paragraph),
      reference: onenterreference,
      referenceString: buffer,
      resourceDestinationString: buffer,
      resourceTitleString: buffer,
      setextHeading: opener(heading),
      strong: opener(strong),
      thematicBreak: opener(thematicBreak)
    },
    exit: {
      atxHeading: closer(),
      atxHeadingSequence: onexitatxheadingsequence,
      autolink: closer(),
      autolinkEmail: onexitautolinkemail,
      autolinkProtocol: onexitautolinkprotocol,
      blockQuote: closer(),
      characterEscapeValue: onexitdata,
      characterReferenceMarkerHexadecimal: onexitcharacterreferencemarker,
      characterReferenceMarkerNumeric: onexitcharacterreferencemarker,
      characterReferenceValue: onexitcharacterreferencevalue,
      characterReference: onexitcharacterreference,
      codeFenced: closer(onexitcodefenced),
      codeFencedFence: onexitcodefencedfence,
      codeFencedFenceInfo: onexitcodefencedfenceinfo,
      codeFencedFenceMeta: onexitcodefencedfencemeta,
      codeFlowValue: onexitdata,
      codeIndented: closer(onexitcodeindented),
      codeText: closer(onexitcodetext),
      codeTextData: onexitdata,
      data: onexitdata,
      definition: closer(),
      definitionDestinationString: onexitdefinitiondestinationstring,
      definitionLabelString: onexitdefinitionlabelstring,
      definitionTitleString: onexitdefinitiontitlestring,
      emphasis: closer(),
      hardBreakEscape: closer(onexithardbreak),
      hardBreakTrailing: closer(onexithardbreak),
      htmlFlow: closer(onexithtmlflow),
      htmlFlowData: onexitdata,
      htmlText: closer(onexithtmltext),
      htmlTextData: onexitdata,
      image: closer(onexitimage),
      label: onexitlabel,
      labelText: onexitlabeltext,
      lineEnding: onexitlineending,
      link: closer(onexitlink),
      listItem: closer(),
      listOrdered: closer(),
      listUnordered: closer(),
      paragraph: closer(),
      referenceString: onexitreferencestring,
      resourceDestinationString: onexitresourcedestinationstring,
      resourceTitleString: onexitresourcetitlestring,
      resource: onexitresource,
      setextHeading: closer(onexitsetextheading),
      setextHeadingLineSequence: onexitsetextheadinglinesequence,
      setextHeadingText: onexitsetextheadingtext,
      strong: closer(),
      thematicBreak: closer()
    }
  };
  configure$1(config, (options || {}).mdastExtensions || []);
  const data = {};
  return compile;
  function compile(events) {
    let tree = {
      type: 'root',
      children: []
    };
    const context = {
      stack: [tree],
      tokenStack: [],
      config,
      enter,
      exit,
      buffer,
      resume,
      data
    };
    const listStack = [];
    let index = -1;
    while (++index < events.length) {
      if (events[index][1].type === "listOrdered" || events[index][1].type === "listUnordered") {
        if (events[index][0] === 'enter') {
          listStack.push(index);
        } else {
          const tail = listStack.pop();
          index = prepareList(events, tail, index);
        }
      }
    }
    index = -1;
    while (++index < events.length) {
      const handler = config[events[index][0]];
      if (own$3.call(handler, events[index][1].type)) {
        handler[events[index][1].type].call(Object.assign({
          sliceSerialize: events[index][2].sliceSerialize
        }, context), events[index][1]);
      }
    }
    if (context.tokenStack.length > 0) {
      const tail = context.tokenStack[context.tokenStack.length - 1];
      const handler = tail[1] || defaultOnError;
      handler.call(context, undefined, tail[0]);
    }
    tree.position = {
      start: point$1(events.length > 0 ? events[0][1].start : {
        line: 1,
        column: 1,
        offset: 0
      }),
      end: point$1(events.length > 0 ? events[events.length - 2][1].end : {
        line: 1,
        column: 1,
        offset: 0
      })
    };
    index = -1;
    while (++index < config.transforms.length) {
      tree = config.transforms[index](tree) || tree;
    }
    return tree;
  }
  function prepareList(events, start, length) {
    let index = start - 1;
    let containerBalance = -1;
    let listSpread = false;
    let listItem;
    let lineIndex;
    let firstBlankLineIndex;
    let atMarker;
    while (++index <= length) {
      const event = events[index];
      switch (event[1].type) {
        case "listUnordered":
        case "listOrdered":
        case "blockQuote":
          {
            if (event[0] === 'enter') {
              containerBalance++;
            } else {
              containerBalance--;
            }
            atMarker = undefined;
            break;
          }
        case "lineEndingBlank":
          {
            if (event[0] === 'enter') {
              if (listItem && !atMarker && !containerBalance && !firstBlankLineIndex) {
                firstBlankLineIndex = index;
              }
              atMarker = undefined;
            }
            break;
          }
        case "linePrefix":
        case "listItemValue":
        case "listItemMarker":
        case "listItemPrefix":
        case "listItemPrefixWhitespace":
          {
            break;
          }
        default:
          {
            atMarker = undefined;
          }
      }
      if (!containerBalance && event[0] === 'enter' && event[1].type === "listItemPrefix" || containerBalance === -1 && event[0] === 'exit' && (event[1].type === "listUnordered" || event[1].type === "listOrdered")) {
        if (listItem) {
          let tailIndex = index;
          lineIndex = undefined;
          while (tailIndex--) {
            const tailEvent = events[tailIndex];
            if (tailEvent[1].type === "lineEnding" || tailEvent[1].type === "lineEndingBlank") {
              if (tailEvent[0] === 'exit') continue;
              if (lineIndex) {
                events[lineIndex][1].type = "lineEndingBlank";
                listSpread = true;
              }
              tailEvent[1].type = "lineEnding";
              lineIndex = tailIndex;
            } else if (tailEvent[1].type === "linePrefix" || tailEvent[1].type === "blockQuotePrefix" || tailEvent[1].type === "blockQuotePrefixWhitespace" || tailEvent[1].type === "blockQuoteMarker" || tailEvent[1].type === "listItemIndent") ; else {
              break;
            }
          }
          if (firstBlankLineIndex && (!lineIndex || firstBlankLineIndex < lineIndex)) {
            listItem._spread = true;
          }
          listItem.end = Object.assign({}, lineIndex ? events[lineIndex][1].start : event[1].end);
          events.splice(lineIndex || index, 0, ['exit', listItem, event[2]]);
          index++;
          length++;
        }
        if (event[1].type === "listItemPrefix") {
          const item = {
            type: 'listItem',
            _spread: false,
            start: Object.assign({}, event[1].start),
            end: undefined
          };
          listItem = item;
          events.splice(index, 0, ['enter', item, event[2]]);
          index++;
          length++;
          firstBlankLineIndex = undefined;
          atMarker = true;
        }
      }
    }
    events[start][1]._spread = listSpread;
    return length;
  }
  function opener(create, and) {
    return open;
    function open(token) {
      enter.call(this, create(token), token);
      if (and) and.call(this, token);
    }
  }
  function buffer() {
    this.stack.push({
      type: 'fragment',
      children: []
    });
  }
  function enter(node, token, errorHandler) {
    const parent = this.stack[this.stack.length - 1];
    const siblings = parent.children;
    siblings.push(node);
    this.stack.push(node);
    this.tokenStack.push([token, errorHandler]);
    node.position = {
      start: point$1(token.start),
      end: undefined
    };
  }
  function closer(and) {
    return close;
    function close(token) {
      if (and) and.call(this, token);
      exit.call(this, token);
    }
  }
  function exit(token, onExitError) {
    const node = this.stack.pop();
    const open = this.tokenStack.pop();
    if (!open) {
      throw new Error('Cannot close `' + token.type + '` (' + stringifyPosition({
        start: token.start,
        end: token.end
      }) + '): it’s not open');
    } else if (open[0].type !== token.type) {
      if (onExitError) {
        onExitError.call(this, token, open[0]);
      } else {
        const handler = open[1] || defaultOnError;
        handler.call(this, token, open[0]);
      }
    }
    node.position.end = point$1(token.end);
  }
  function resume() {
    return toString(this.stack.pop());
  }
  function onenterlistordered() {
    this.data.expectingFirstListItemValue = true;
  }
  function onenterlistitemvalue(token) {
    if (this.data.expectingFirstListItemValue) {
      const ancestor = this.stack[this.stack.length - 2];
      ancestor.start = Number.parseInt(this.sliceSerialize(token), 10);
      this.data.expectingFirstListItemValue = undefined;
    }
  }
  function onexitcodefencedfenceinfo() {
    const data = this.resume();
    const node = this.stack[this.stack.length - 1];
    node.lang = data;
  }
  function onexitcodefencedfencemeta() {
    const data = this.resume();
    const node = this.stack[this.stack.length - 1];
    node.meta = data;
  }
  function onexitcodefencedfence() {
    if (this.data.flowCodeInside) return;
    this.buffer();
    this.data.flowCodeInside = true;
  }
  function onexitcodefenced() {
    const data = this.resume();
    const node = this.stack[this.stack.length - 1];
    node.value = data.replace(/^(\r?\n|\r)|(\r?\n|\r)$/g, '');
    this.data.flowCodeInside = undefined;
  }
  function onexitcodeindented() {
    const data = this.resume();
    const node = this.stack[this.stack.length - 1];
    node.value = data.replace(/(\r?\n|\r)$/g, '');
  }
  function onexitdefinitionlabelstring(token) {
    const label = this.resume();
    const node = this.stack[this.stack.length - 1];
    node.label = label;
    node.identifier = normalizeIdentifier(this.sliceSerialize(token)).toLowerCase();
  }
  function onexitdefinitiontitlestring() {
    const data = this.resume();
    const node = this.stack[this.stack.length - 1];
    node.title = data;
  }
  function onexitdefinitiondestinationstring() {
    const data = this.resume();
    const node = this.stack[this.stack.length - 1];
    node.url = data;
  }
  function onexitatxheadingsequence(token) {
    const node = this.stack[this.stack.length - 1];
    if (!node.depth) {
      const depth = this.sliceSerialize(token).length;
      node.depth = depth;
    }
  }
  function onexitsetextheadingtext() {
    this.data.setextHeadingSlurpLineEnding = true;
  }
  function onexitsetextheadinglinesequence(token) {
    const node = this.stack[this.stack.length - 1];
    node.depth = this.sliceSerialize(token).codePointAt(0) === 61 ? 1 : 2;
  }
  function onexitsetextheading() {
    this.data.setextHeadingSlurpLineEnding = undefined;
  }
  function onenterdata(token) {
    const node = this.stack[this.stack.length - 1];
    const siblings = node.children;
    let tail = siblings[siblings.length - 1];
    if (!tail || tail.type !== 'text') {
      tail = text();
      tail.position = {
        start: point$1(token.start),
        end: undefined
      };
      siblings.push(tail);
    }
    this.stack.push(tail);
  }
  function onexitdata(token) {
    const tail = this.stack.pop();
    tail.value += this.sliceSerialize(token);
    tail.position.end = point$1(token.end);
  }
  function onexitlineending(token) {
    const context = this.stack[this.stack.length - 1];
    if (this.data.atHardBreak) {
      const tail = context.children[context.children.length - 1];
      tail.position.end = point$1(token.end);
      this.data.atHardBreak = undefined;
      return;
    }
    if (!this.data.setextHeadingSlurpLineEnding && config.canContainEols.includes(context.type)) {
      onenterdata.call(this, token);
      onexitdata.call(this, token);
    }
  }
  function onexithardbreak() {
    this.data.atHardBreak = true;
  }
  function onexithtmlflow() {
    const data = this.resume();
    const node = this.stack[this.stack.length - 1];
    node.value = data;
  }
  function onexithtmltext() {
    const data = this.resume();
    const node = this.stack[this.stack.length - 1];
    node.value = data;
  }
  function onexitcodetext() {
    const data = this.resume();
    const node = this.stack[this.stack.length - 1];
    node.value = data;
  }
  function onexitlink() {
    const node = this.stack[this.stack.length - 1];
    if (this.data.inReference) {
      const referenceType = this.data.referenceType || 'shortcut';
      node.type += 'Reference';
      node.referenceType = referenceType;
      delete node.url;
      delete node.title;
    } else {
      delete node.identifier;
      delete node.label;
    }
    this.data.referenceType = undefined;
  }
  function onexitimage() {
    const node = this.stack[this.stack.length - 1];
    if (this.data.inReference) {
      const referenceType = this.data.referenceType || 'shortcut';
      node.type += 'Reference';
      node.referenceType = referenceType;
      delete node.url;
      delete node.title;
    } else {
      delete node.identifier;
      delete node.label;
    }
    this.data.referenceType = undefined;
  }
  function onexitlabeltext(token) {
    const string = this.sliceSerialize(token);
    const ancestor = this.stack[this.stack.length - 2];
    ancestor.label = decodeString(string);
    ancestor.identifier = normalizeIdentifier(string).toLowerCase();
  }
  function onexitlabel() {
    const fragment = this.stack[this.stack.length - 1];
    const value = this.resume();
    const node = this.stack[this.stack.length - 1];
    this.data.inReference = true;
    if (node.type === 'link') {
      const children = fragment.children;
      node.children = children;
    } else {
      node.alt = value;
    }
  }
  function onexitresourcedestinationstring() {
    const data = this.resume();
    const node = this.stack[this.stack.length - 1];
    node.url = data;
  }
  function onexitresourcetitlestring() {
    const data = this.resume();
    const node = this.stack[this.stack.length - 1];
    node.title = data;
  }
  function onexitresource() {
    this.data.inReference = undefined;
  }
  function onenterreference() {
    this.data.referenceType = 'collapsed';
  }
  function onexitreferencestring(token) {
    const label = this.resume();
    const node = this.stack[this.stack.length - 1];
    node.label = label;
    node.identifier = normalizeIdentifier(this.sliceSerialize(token)).toLowerCase();
    this.data.referenceType = 'full';
  }
  function onexitcharacterreferencemarker(token) {
    this.data.characterReferenceType = token.type;
  }
  function onexitcharacterreferencevalue(token) {
    const data = this.sliceSerialize(token);
    const type = this.data.characterReferenceType;
    let value;
    if (type) {
      value = decodeNumericCharacterReference(data, type === "characterReferenceMarkerNumeric" ? 10 : 16);
      this.data.characterReferenceType = undefined;
    } else {
      const result = decodeNamedCharacterReference(data);
      value = result;
    }
    const tail = this.stack[this.stack.length - 1];
    tail.value += value;
  }
  function onexitcharacterreference(token) {
    const tail = this.stack.pop();
    tail.position.end = point$1(token.end);
  }
  function onexitautolinkprotocol(token) {
    onexitdata.call(this, token);
    const node = this.stack[this.stack.length - 1];
    node.url = this.sliceSerialize(token);
  }
  function onexitautolinkemail(token) {
    onexitdata.call(this, token);
    const node = this.stack[this.stack.length - 1];
    node.url = 'mailto:' + this.sliceSerialize(token);
  }
  function blockQuote() {
    return {
      type: 'blockquote',
      children: []
    };
  }
  function codeFlow() {
    return {
      type: 'code',
      lang: null,
      meta: null,
      value: ''
    };
  }
  function codeText() {
    return {
      type: 'inlineCode',
      value: ''
    };
  }
  function definition() {
    return {
      type: 'definition',
      identifier: '',
      label: null,
      title: null,
      url: ''
    };
  }
  function emphasis() {
    return {
      type: 'emphasis',
      children: []
    };
  }
  function heading() {
    return {
      type: 'heading',
      depth: 0,
      children: []
    };
  }
  function hardBreak() {
    return {
      type: 'break'
    };
  }
  function html() {
    return {
      type: 'html',
      value: ''
    };
  }
  function image() {
    return {
      type: 'image',
      title: null,
      url: '',
      alt: null
    };
  }
  function link() {
    return {
      type: 'link',
      title: null,
      url: '',
      children: []
    };
  }
  function list(token) {
    return {
      type: 'list',
      ordered: token.type === 'listOrdered',
      start: null,
      spread: token._spread,
      children: []
    };
  }
  function listItem(token) {
    return {
      type: 'listItem',
      spread: token._spread,
      checked: null,
      children: []
    };
  }
  function paragraph() {
    return {
      type: 'paragraph',
      children: []
    };
  }
  function strong() {
    return {
      type: 'strong',
      children: []
    };
  }
  function text() {
    return {
      type: 'text',
      value: ''
    };
  }
  function thematicBreak() {
    return {
      type: 'thematicBreak'
    };
  }
}
function point$1(d) {
  return {
    line: d.line,
    column: d.column,
    offset: d.offset
  };
}
function configure$1(combined, extensions) {
  let index = -1;
  while (++index < extensions.length) {
    const value = extensions[index];
    if (Array.isArray(value)) {
      configure$1(combined, value);
    } else {
      extension(combined, value);
    }
  }
}
function extension(combined, extension) {
  let key;
  for (key in extension) {
    if (own$3.call(extension, key)) {
      switch (key) {
        case 'canContainEols':
          {
            const right = extension[key];
            if (right) {
              combined[key].push(...right);
            }
            break;
          }
        case 'transforms':
          {
            const right = extension[key];
            if (right) {
              combined[key].push(...right);
            }
            break;
          }
        case 'enter':
        case 'exit':
          {
            const right = extension[key];
            if (right) {
              Object.assign(combined[key], right);
            }
            break;
          }
      }
    }
  }
}
function defaultOnError(left, right) {
  if (left) {
    throw new Error('Cannot close `' + left.type + '` (' + stringifyPosition({
      start: left.start,
      end: left.end
    }) + '): a different token (`' + right.type + '`, ' + stringifyPosition({
      start: right.start,
      end: right.end
    }) + ') is open');
  } else {
    throw new Error('Cannot close document, a token (`' + right.type + '`, ' + stringifyPosition({
      start: right.start,
      end: right.end
    }) + ') is still open');
  }
}

function remarkParse(options) {
  const self = this;
  self.parser = parser;
  function parser(doc) {
    return fromMarkdown(doc, {
      ...self.data('settings'),
      ...options,
      extensions: self.data('micromarkExtensions') || [],
      mdastExtensions: self.data('fromMarkdownExtensions') || []
    })
  }
}

const own$2 = {}.hasOwnProperty;
function zwitch(key, options) {
  const settings = options || {};
  function one(value, ...parameters) {
    let fn = one.invalid;
    const handlers = one.handlers;
    if (value && own$2.call(value, key)) {
      const id = String(value[key]);
      fn = own$2.call(handlers, id) ? handlers[id] : one.unknown;
    }
    if (fn) {
      return fn.call(this, value, ...parameters)
    }
  }
  one.handlers = settings.handlers || {};
  one.invalid = settings.invalid;
  one.unknown = settings.unknown;
  return one
}

const own$1 = {}.hasOwnProperty;
function configure(base, extension) {
  let index = -1;
  let key;
  if (extension.extensions) {
    while (++index < extension.extensions.length) {
      configure(base, extension.extensions[index]);
    }
  }
  for (key in extension) {
    if (own$1.call(extension, key)) {
      switch (key) {
        case 'extensions': {
          break
        }
        case 'unsafe': {
          list$1(base[key], extension[key]);
          break
        }
        case 'join': {
          list$1(base[key], extension[key]);
          break
        }
        case 'handlers': {
          map$4(base[key], extension[key]);
          break
        }
        default: {
          base.options[key] = extension[key];
        }
      }
    }
  }
  return base
}
function list$1(left, right) {
  if (right) {
    left.push(...right);
  }
}
function map$4(left, right) {
  if (right) {
    Object.assign(left, right);
  }
}

function blockquote(node, _, state, info) {
  const exit = state.enter('blockquote');
  const tracker = state.createTracker(info);
  tracker.move('> ');
  tracker.shift(2);
  const value = state.indentLines(
    state.containerFlow(node, tracker.current()),
    map$3
  );
  exit();
  return value
}
function map$3(line, _, blank) {
  return '>' + (blank ? '' : ' ') + line
}

function patternInScope(stack, pattern) {
  return (
    listInScope(stack, pattern.inConstruct, true) &&
    !listInScope(stack, pattern.notInConstruct, false)
  )
}
function listInScope(stack, list, none) {
  if (typeof list === 'string') {
    list = [list];
  }
  if (!list || list.length === 0) {
    return none
  }
  let index = -1;
  while (++index < list.length) {
    if (stack.includes(list[index])) {
      return true
    }
  }
  return false
}

function hardBreak(_, _1, state, info) {
  let index = -1;
  while (++index < state.unsafe.length) {
    if (
      state.unsafe[index].character === '\n' &&
      patternInScope(state.stack, state.unsafe[index])
    ) {
      return /[ \t]/.test(info.before) ? '' : ' '
    }
  }
  return '\\\n'
}

function longestStreak(value, substring) {
  const source = String(value);
  let index = source.indexOf(substring);
  let expected = index;
  let count = 0;
  let max = 0;
  if (typeof substring !== 'string') {
    throw new TypeError('Expected substring')
  }
  while (index !== -1) {
    if (index === expected) {
      if (++count > max) {
        max = count;
      }
    } else {
      count = 1;
    }
    expected = index + substring.length;
    index = source.indexOf(substring, expected);
  }
  return max
}

function formatCodeAsIndented(node, state) {
  return Boolean(
    state.options.fences === false &&
      node.value &&
      !node.lang &&
      /[^ \r\n]/.test(node.value) &&
      !/^[\t ]*(?:[\r\n]|$)|(?:^|[\r\n])[\t ]*$/.test(node.value)
  )
}

function checkFence(state) {
  const marker = state.options.fence || '`';
  if (marker !== '`' && marker !== '~') {
    throw new Error(
      'Cannot serialize code with `' +
        marker +
        '` for `options.fence`, expected `` ` `` or `~`'
    )
  }
  return marker
}

function code$1(node, _, state, info) {
  const marker = checkFence(state);
  const raw = node.value || '';
  const suffix = marker === '`' ? 'GraveAccent' : 'Tilde';
  if (formatCodeAsIndented(node, state)) {
    const exit = state.enter('codeIndented');
    const value = state.indentLines(raw, map$2);
    exit();
    return value
  }
  const tracker = state.createTracker(info);
  const sequence = marker.repeat(Math.max(longestStreak(raw, marker) + 1, 3));
  const exit = state.enter('codeFenced');
  let value = tracker.move(sequence);
  if (node.lang) {
    const subexit = state.enter(`codeFencedLang${suffix}`);
    value += tracker.move(
      state.safe(node.lang, {
        before: value,
        after: ' ',
        encode: ['`'],
        ...tracker.current()
      })
    );
    subexit();
  }
  if (node.lang && node.meta) {
    const subexit = state.enter(`codeFencedMeta${suffix}`);
    value += tracker.move(' ');
    value += tracker.move(
      state.safe(node.meta, {
        before: value,
        after: '\n',
        encode: ['`'],
        ...tracker.current()
      })
    );
    subexit();
  }
  value += tracker.move('\n');
  if (raw) {
    value += tracker.move(raw + '\n');
  }
  value += tracker.move(sequence);
  exit();
  return value
}
function map$2(line, _, blank) {
  return (blank ? '' : '    ') + line
}

function checkQuote(state) {
  const marker = state.options.quote || '"';
  if (marker !== '"' && marker !== "'") {
    throw new Error(
      'Cannot serialize title with `' +
        marker +
        '` for `options.quote`, expected `"`, or `\'`'
    )
  }
  return marker
}

function definition(node, _, state, info) {
  const quote = checkQuote(state);
  const suffix = quote === '"' ? 'Quote' : 'Apostrophe';
  const exit = state.enter('definition');
  let subexit = state.enter('label');
  const tracker = state.createTracker(info);
  let value = tracker.move('[');
  value += tracker.move(
    state.safe(state.associationId(node), {
      before: value,
      after: ']',
      ...tracker.current()
    })
  );
  value += tracker.move(']: ');
  subexit();
  if (
    !node.url ||
    /[\0- \u007F]/.test(node.url)
  ) {
    subexit = state.enter('destinationLiteral');
    value += tracker.move('<');
    value += tracker.move(
      state.safe(node.url, {before: value, after: '>', ...tracker.current()})
    );
    value += tracker.move('>');
  } else {
    subexit = state.enter('destinationRaw');
    value += tracker.move(
      state.safe(node.url, {
        before: value,
        after: node.title ? ' ' : '\n',
        ...tracker.current()
      })
    );
  }
  subexit();
  if (node.title) {
    subexit = state.enter(`title${suffix}`);
    value += tracker.move(' ' + quote);
    value += tracker.move(
      state.safe(node.title, {
        before: value,
        after: quote,
        ...tracker.current()
      })
    );
    value += tracker.move(quote);
    subexit();
  }
  exit();
  return value
}

function checkEmphasis(state) {
  const marker = state.options.emphasis || '*';
  if (marker !== '*' && marker !== '_') {
    throw new Error(
      'Cannot serialize emphasis with `' +
        marker +
        '` for `options.emphasis`, expected `*`, or `_`'
    )
  }
  return marker
}

emphasis.peek = emphasisPeek;
function emphasis(node, _, state, info) {
  const marker = checkEmphasis(state);
  const exit = state.enter('emphasis');
  const tracker = state.createTracker(info);
  let value = tracker.move(marker);
  value += tracker.move(
    state.containerPhrasing(node, {
      before: value,
      after: marker,
      ...tracker.current()
    })
  );
  value += tracker.move(marker);
  exit();
  return value
}
function emphasisPeek(_, _1, state) {
  return state.options.emphasis || '*'
}

const convert =
  (
    function (test) {
      if (test === null || test === undefined) {
        return ok
      }
      if (typeof test === 'function') {
        return castFactory(test)
      }
      if (typeof test === 'object') {
        return Array.isArray(test) ? anyFactory(test) : propsFactory(test)
      }
      if (typeof test === 'string') {
        return typeFactory(test)
      }
      throw new Error('Expected function, string, or object as test')
    }
  );
function anyFactory(tests) {
  const checks = [];
  let index = -1;
  while (++index < tests.length) {
    checks[index] = convert(tests[index]);
  }
  return castFactory(any)
  function any(...parameters) {
    let index = -1;
    while (++index < checks.length) {
      if (checks[index].apply(this, parameters)) return true
    }
    return false
  }
}
function propsFactory(check) {
  const checkAsRecord =  (check);
  return castFactory(all)
  function all(node) {
    const nodeAsRecord =  (
       (node)
    );
    let key;
    for (key in check) {
      if (nodeAsRecord[key] !== checkAsRecord[key]) return false
    }
    return true
  }
}
function typeFactory(check) {
  return castFactory(type)
  function type(node) {
    return node && node.type === check
  }
}
function castFactory(testFunction) {
  return check
  function check(value, index, parent) {
    return Boolean(
      looksLikeANode(value) &&
        testFunction.call(
          this,
          value,
          typeof index === 'number' ? index : undefined,
          parent || undefined
        )
    )
  }
}
function ok() {
  return true
}
function looksLikeANode(value) {
  return value !== null && typeof value === 'object' && 'type' in value
}

function color$1(d) {
  return '\u001B[33m' + d + '\u001B[39m'
}

const empty$1 = [];
const CONTINUE = true;
const EXIT = false;
const SKIP = 'skip';
function visitParents(tree, test, visitor, reverse) {
  let check;
  if (typeof test === 'function' && typeof visitor !== 'function') {
    reverse = visitor;
    visitor = test;
  } else {
    check = test;
  }
  const is = convert(check);
  const step = reverse ? -1 : 1;
  factory(tree, undefined, [])();
  function factory(node, index, parents) {
    const value =  (
      node && typeof node === 'object' ? node : {}
    );
    if (typeof value.type === 'string') {
      const name =
        typeof value.tagName === 'string'
          ? value.tagName
          :
          typeof value.name === 'string'
          ? value.name
          : undefined;
      Object.defineProperty(visit, 'name', {
        value:
          'node (' + color$1(node.type + (name ? '<' + name + '>' : '')) + ')'
      });
    }
    return visit
    function visit() {
      let result = empty$1;
      let subresult;
      let offset;
      let grandparents;
      if (!test || is(node, index, parents[parents.length - 1] || undefined)) {
        result = toResult(visitor(node, parents));
        if (result[0] === EXIT) {
          return result
        }
      }
      if ('children' in node && node.children) {
        const nodeAsParent =  (node);
        if (nodeAsParent.children && result[0] !== SKIP) {
          offset = (reverse ? nodeAsParent.children.length : -1) + step;
          grandparents = parents.concat(nodeAsParent);
          while (offset > -1 && offset < nodeAsParent.children.length) {
            const child = nodeAsParent.children[offset];
            subresult = factory(child, offset, grandparents)();
            if (subresult[0] === EXIT) {
              return subresult
            }
            offset =
              typeof subresult[1] === 'number' ? subresult[1] : offset + step;
          }
        }
      }
      return result
    }
  }
}
function toResult(value) {
  if (Array.isArray(value)) {
    return value
  }
  if (typeof value === 'number') {
    return [CONTINUE, value]
  }
  return value === null || value === undefined ? empty$1 : [value]
}

function visit(tree, testOrVisitor, visitorOrReverse, maybeReverse) {
  let reverse;
  let test;
  let visitor;
  if (
    typeof testOrVisitor === 'function' &&
    typeof visitorOrReverse !== 'function'
  ) {
    test = undefined;
    visitor = testOrVisitor;
    reverse = visitorOrReverse;
  } else {
    test = testOrVisitor;
    visitor = visitorOrReverse;
    reverse = maybeReverse;
  }
  visitParents(tree, test, overload, reverse);
  function overload(node, parents) {
    const parent = parents[parents.length - 1];
    const index = parent ? parent.children.indexOf(node) : undefined;
    return visitor(node, index, parent)
  }
}

function formatHeadingAsSetext(node, state) {
  let literalWithBreak = false;
  visit(node, function (node) {
    if (
      ('value' in node && /\r?\n|\r/.test(node.value)) ||
      node.type === 'break'
    ) {
      literalWithBreak = true;
      return EXIT
    }
  });
  return Boolean(
    (!node.depth || node.depth < 3) &&
      toString(node) &&
      (state.options.setext || literalWithBreak)
  )
}

function heading(node, _, state, info) {
  const rank = Math.max(Math.min(6, node.depth || 1), 1);
  const tracker = state.createTracker(info);
  if (formatHeadingAsSetext(node, state)) {
    const exit = state.enter('headingSetext');
    const subexit = state.enter('phrasing');
    const value = state.containerPhrasing(node, {
      ...tracker.current(),
      before: '\n',
      after: '\n'
    });
    subexit();
    exit();
    return (
      value +
      '\n' +
      (rank === 1 ? '=' : '-').repeat(
        value.length -
          (Math.max(value.lastIndexOf('\r'), value.lastIndexOf('\n')) + 1)
      )
    )
  }
  const sequence = '#'.repeat(rank);
  const exit = state.enter('headingAtx');
  const subexit = state.enter('phrasing');
  tracker.move(sequence + ' ');
  let value = state.containerPhrasing(node, {
    before: '# ',
    after: '\n',
    ...tracker.current()
  });
  if (/^[\t ]/.test(value)) {
    value =
      '&#x' +
      value.charCodeAt(0).toString(16).toUpperCase() +
      ';' +
      value.slice(1);
  }
  value = value ? sequence + ' ' + value : sequence;
  if (state.options.closeAtx) {
    value += ' ' + sequence;
  }
  subexit();
  exit();
  return value
}

html$1.peek = htmlPeek;
function html$1(node) {
  return node.value || ''
}
function htmlPeek() {
  return '<'
}

image.peek = imagePeek;
function image(node, _, state, info) {
  const quote = checkQuote(state);
  const suffix = quote === '"' ? 'Quote' : 'Apostrophe';
  const exit = state.enter('image');
  let subexit = state.enter('label');
  const tracker = state.createTracker(info);
  let value = tracker.move('![');
  value += tracker.move(
    state.safe(node.alt, {before: value, after: ']', ...tracker.current()})
  );
  value += tracker.move('](');
  subexit();
  if (
    (!node.url && node.title) ||
    /[\0- \u007F]/.test(node.url)
  ) {
    subexit = state.enter('destinationLiteral');
    value += tracker.move('<');
    value += tracker.move(
      state.safe(node.url, {before: value, after: '>', ...tracker.current()})
    );
    value += tracker.move('>');
  } else {
    subexit = state.enter('destinationRaw');
    value += tracker.move(
      state.safe(node.url, {
        before: value,
        after: node.title ? ' ' : ')',
        ...tracker.current()
      })
    );
  }
  subexit();
  if (node.title) {
    subexit = state.enter(`title${suffix}`);
    value += tracker.move(' ' + quote);
    value += tracker.move(
      state.safe(node.title, {
        before: value,
        after: quote,
        ...tracker.current()
      })
    );
    value += tracker.move(quote);
    subexit();
  }
  value += tracker.move(')');
  exit();
  return value
}
function imagePeek() {
  return '!'
}

imageReference.peek = imageReferencePeek;
function imageReference(node, _, state, info) {
  const type = node.referenceType;
  const exit = state.enter('imageReference');
  let subexit = state.enter('label');
  const tracker = state.createTracker(info);
  let value = tracker.move('![');
  const alt = state.safe(node.alt, {
    before: value,
    after: ']',
    ...tracker.current()
  });
  value += tracker.move(alt + '][');
  subexit();
  const stack = state.stack;
  state.stack = [];
  subexit = state.enter('reference');
  const reference = state.safe(state.associationId(node), {
    before: value,
    after: ']',
    ...tracker.current()
  });
  subexit();
  state.stack = stack;
  exit();
  if (type === 'full' || !alt || alt !== reference) {
    value += tracker.move(reference + ']');
  } else if (type === 'shortcut') {
    value = value.slice(0, -1);
  } else {
    value += tracker.move(']');
  }
  return value
}
function imageReferencePeek() {
  return '!'
}

inlineCode.peek = inlineCodePeek;
function inlineCode(node, _, state) {
  let value = node.value || '';
  let sequence = '`';
  let index = -1;
  while (new RegExp('(^|[^`])' + sequence + '([^`]|$)').test(value)) {
    sequence += '`';
  }
  if (
    /[^ \r\n]/.test(value) &&
    ((/^[ \r\n]/.test(value) && /[ \r\n]$/.test(value)) || /^`|`$/.test(value))
  ) {
    value = ' ' + value + ' ';
  }
  while (++index < state.unsafe.length) {
    const pattern = state.unsafe[index];
    const expression = state.compilePattern(pattern);
    let match;
    if (!pattern.atBreak) continue
    while ((match = expression.exec(value))) {
      let position = match.index;
      if (
        value.charCodeAt(position) === 10  &&
        value.charCodeAt(position - 1) === 13
      ) {
        position--;
      }
      value = value.slice(0, position) + ' ' + value.slice(match.index + 1);
    }
  }
  return sequence + value + sequence
}
function inlineCodePeek() {
  return '`'
}

function formatLinkAsAutolink(node, state) {
  const raw = toString(node);
  return Boolean(
    !state.options.resourceLink &&
      node.url &&
      !node.title &&
      node.children &&
      node.children.length === 1 &&
      node.children[0].type === 'text' &&
      (raw === node.url || 'mailto:' + raw === node.url) &&
      /^[a-z][a-z+.-]+:/i.test(node.url) &&
      !/[\0- <>\u007F]/.test(node.url)
  )
}

link.peek = linkPeek;
function link(node, _, state, info) {
  const quote = checkQuote(state);
  const suffix = quote === '"' ? 'Quote' : 'Apostrophe';
  const tracker = state.createTracker(info);
  let exit;
  let subexit;
  if (formatLinkAsAutolink(node, state)) {
    const stack = state.stack;
    state.stack = [];
    exit = state.enter('autolink');
    let value = tracker.move('<');
    value += tracker.move(
      state.containerPhrasing(node, {
        before: value,
        after: '>',
        ...tracker.current()
      })
    );
    value += tracker.move('>');
    exit();
    state.stack = stack;
    return value
  }
  exit = state.enter('link');
  subexit = state.enter('label');
  let value = tracker.move('[');
  value += tracker.move(
    state.containerPhrasing(node, {
      before: value,
      after: '](',
      ...tracker.current()
    })
  );
  value += tracker.move('](');
  subexit();
  if (
    (!node.url && node.title) ||
    /[\0- \u007F]/.test(node.url)
  ) {
    subexit = state.enter('destinationLiteral');
    value += tracker.move('<');
    value += tracker.move(
      state.safe(node.url, {before: value, after: '>', ...tracker.current()})
    );
    value += tracker.move('>');
  } else {
    subexit = state.enter('destinationRaw');
    value += tracker.move(
      state.safe(node.url, {
        before: value,
        after: node.title ? ' ' : ')',
        ...tracker.current()
      })
    );
  }
  subexit();
  if (node.title) {
    subexit = state.enter(`title${suffix}`);
    value += tracker.move(' ' + quote);
    value += tracker.move(
      state.safe(node.title, {
        before: value,
        after: quote,
        ...tracker.current()
      })
    );
    value += tracker.move(quote);
    subexit();
  }
  value += tracker.move(')');
  exit();
  return value
}
function linkPeek(node, _, state) {
  return formatLinkAsAutolink(node, state) ? '<' : '['
}

linkReference.peek = linkReferencePeek;
function linkReference(node, _, state, info) {
  const type = node.referenceType;
  const exit = state.enter('linkReference');
  let subexit = state.enter('label');
  const tracker = state.createTracker(info);
  let value = tracker.move('[');
  const text = state.containerPhrasing(node, {
    before: value,
    after: ']',
    ...tracker.current()
  });
  value += tracker.move(text + '][');
  subexit();
  const stack = state.stack;
  state.stack = [];
  subexit = state.enter('reference');
  const reference = state.safe(state.associationId(node), {
    before: value,
    after: ']',
    ...tracker.current()
  });
  subexit();
  state.stack = stack;
  exit();
  if (type === 'full' || !text || text !== reference) {
    value += tracker.move(reference + ']');
  } else if (type === 'shortcut') {
    value = value.slice(0, -1);
  } else {
    value += tracker.move(']');
  }
  return value
}
function linkReferencePeek() {
  return '['
}

function checkBullet(state) {
  const marker = state.options.bullet || '*';
  if (marker !== '*' && marker !== '+' && marker !== '-') {
    throw new Error(
      'Cannot serialize items with `' +
        marker +
        '` for `options.bullet`, expected `*`, `+`, or `-`'
    )
  }
  return marker
}

function checkBulletOther(state) {
  const bullet = checkBullet(state);
  const bulletOther = state.options.bulletOther;
  if (!bulletOther) {
    return bullet === '*' ? '-' : '*'
  }
  if (bulletOther !== '*' && bulletOther !== '+' && bulletOther !== '-') {
    throw new Error(
      'Cannot serialize items with `' +
        bulletOther +
        '` for `options.bulletOther`, expected `*`, `+`, or `-`'
    )
  }
  if (bulletOther === bullet) {
    throw new Error(
      'Expected `bullet` (`' +
        bullet +
        '`) and `bulletOther` (`' +
        bulletOther +
        '`) to be different'
    )
  }
  return bulletOther
}

function checkBulletOrdered(state) {
  const marker = state.options.bulletOrdered || '.';
  if (marker !== '.' && marker !== ')') {
    throw new Error(
      'Cannot serialize items with `' +
        marker +
        '` for `options.bulletOrdered`, expected `.` or `)`'
    )
  }
  return marker
}

function checkRule(state) {
  const marker = state.options.rule || '*';
  if (marker !== '*' && marker !== '-' && marker !== '_') {
    throw new Error(
      'Cannot serialize rules with `' +
        marker +
        '` for `options.rule`, expected `*`, `-`, or `_`'
    )
  }
  return marker
}

function list(node, parent, state, info) {
  const exit = state.enter('list');
  const bulletCurrent = state.bulletCurrent;
  let bullet = node.ordered ? checkBulletOrdered(state) : checkBullet(state);
  const bulletOther = node.ordered
    ? bullet === '.'
      ? ')'
      : '.'
    : checkBulletOther(state);
  let useDifferentMarker =
    parent && state.bulletLastUsed ? bullet === state.bulletLastUsed : false;
  if (!node.ordered) {
    const firstListItem = node.children ? node.children[0] : undefined;
    if (
      (bullet === '*' || bullet === '-') &&
      firstListItem &&
      (!firstListItem.children || !firstListItem.children[0]) &&
      state.stack[state.stack.length - 1] === 'list' &&
      state.stack[state.stack.length - 2] === 'listItem' &&
      state.stack[state.stack.length - 3] === 'list' &&
      state.stack[state.stack.length - 4] === 'listItem' &&
      state.indexStack[state.indexStack.length - 1] === 0 &&
      state.indexStack[state.indexStack.length - 2] === 0 &&
      state.indexStack[state.indexStack.length - 3] === 0
    ) {
      useDifferentMarker = true;
    }
    if (checkRule(state) === bullet && firstListItem) {
      let index = -1;
      while (++index < node.children.length) {
        const item = node.children[index];
        if (
          item &&
          item.type === 'listItem' &&
          item.children &&
          item.children[0] &&
          item.children[0].type === 'thematicBreak'
        ) {
          useDifferentMarker = true;
          break
        }
      }
    }
  }
  if (useDifferentMarker) {
    bullet = bulletOther;
  }
  state.bulletCurrent = bullet;
  const value = state.containerFlow(node, info);
  state.bulletLastUsed = bullet;
  state.bulletCurrent = bulletCurrent;
  exit();
  return value
}

function checkListItemIndent(state) {
  const style = state.options.listItemIndent || 'one';
  if (style !== 'tab' && style !== 'one' && style !== 'mixed') {
    throw new Error(
      'Cannot serialize items with `' +
        style +
        '` for `options.listItemIndent`, expected `tab`, `one`, or `mixed`'
    )
  }
  return style
}

function listItem(node, parent, state, info) {
  const listItemIndent = checkListItemIndent(state);
  let bullet = state.bulletCurrent || checkBullet(state);
  if (parent && parent.type === 'list' && parent.ordered) {
    bullet =
      (typeof parent.start === 'number' && parent.start > -1
        ? parent.start
        : 1) +
      (state.options.incrementListMarker === false
        ? 0
        : parent.children.indexOf(node)) +
      bullet;
  }
  let size = bullet.length + 1;
  if (
    listItemIndent === 'tab' ||
    (listItemIndent === 'mixed' &&
      ((parent && parent.type === 'list' && parent.spread) || node.spread))
  ) {
    size = Math.ceil(size / 4) * 4;
  }
  const tracker = state.createTracker(info);
  tracker.move(bullet + ' '.repeat(size - bullet.length));
  tracker.shift(size);
  const exit = state.enter('listItem');
  const value = state.indentLines(
    state.containerFlow(node, tracker.current()),
    map
  );
  exit();
  return value
  function map(line, index, blank) {
    if (index) {
      return (blank ? '' : ' '.repeat(size)) + line
    }
    return (blank ? bullet : bullet + ' '.repeat(size - bullet.length)) + line
  }
}

function paragraph(node, _, state, info) {
  const exit = state.enter('paragraph');
  const subexit = state.enter('phrasing');
  const value = state.containerPhrasing(node, info);
  subexit();
  exit();
  return value
}

const phrasing =
  (
    convert([
      'break',
      'delete',
      'emphasis',
      'footnote',
      'footnoteReference',
      'image',
      'imageReference',
      'inlineCode',
      'inlineMath',
      'link',
      'linkReference',
      'mdxJsxTextElement',
      'mdxTextExpression',
      'strong',
      'text',
      'textDirective'
    ])
  );

function root(node, _, state, info) {
  const hasPhrasing = node.children.some(function (d) {
    return phrasing(d)
  });
  const fn = hasPhrasing ? state.containerPhrasing : state.containerFlow;
  return fn.call(state, node, info)
}

function checkStrong(state) {
  const marker = state.options.strong || '*';
  if (marker !== '*' && marker !== '_') {
    throw new Error(
      'Cannot serialize strong with `' +
        marker +
        '` for `options.strong`, expected `*`, or `_`'
    )
  }
  return marker
}

strong.peek = strongPeek;
function strong(node, _, state, info) {
  const marker = checkStrong(state);
  const exit = state.enter('strong');
  const tracker = state.createTracker(info);
  let value = tracker.move(marker + marker);
  value += tracker.move(
    state.containerPhrasing(node, {
      before: value,
      after: marker,
      ...tracker.current()
    })
  );
  value += tracker.move(marker + marker);
  exit();
  return value
}
function strongPeek(_, _1, state) {
  return state.options.strong || '*'
}

function text$1(node, _, state, info) {
  return state.safe(node.value, info)
}

function checkRuleRepetition(state) {
  const repetition = state.options.ruleRepetition || 3;
  if (repetition < 3) {
    throw new Error(
      'Cannot serialize rules with repetition `' +
        repetition +
        '` for `options.ruleRepetition`, expected `3` or more'
    )
  }
  return repetition
}

function thematicBreak(_, _1, state) {
  const value = (
    checkRule(state) + (state.options.ruleSpaces ? ' ' : '')
  ).repeat(checkRuleRepetition(state));
  return state.options.ruleSpaces ? value.slice(0, -1) : value
}

const handle = {
  blockquote,
  break: hardBreak,
  code: code$1,
  definition,
  emphasis,
  hardBreak,
  heading,
  html: html$1,
  image,
  imageReference,
  inlineCode,
  link,
  linkReference,
  list,
  listItem,
  paragraph,
  root,
  strong,
  text: text$1,
  thematicBreak
};

const join = [joinDefaults];
function joinDefaults(left, right, parent, state) {
  if (
    right.type === 'code' &&
    formatCodeAsIndented(right, state) &&
    (left.type === 'list' ||
      (left.type === right.type && formatCodeAsIndented(left, state)))
  ) {
    return false
  }
  if ('spread' in parent && typeof parent.spread === 'boolean') {
    if (
      left.type === 'paragraph' &&
      (left.type === right.type ||
        right.type === 'definition' ||
        (right.type === 'heading' && formatHeadingAsSetext(right, state)))
    ) {
      return
    }
    return parent.spread ? 1 : 0
  }
}

const fullPhrasingSpans = [
  'autolink',
  'destinationLiteral',
  'destinationRaw',
  'reference',
  'titleQuote',
  'titleApostrophe'
];
const unsafe = [
  {character: '\t', after: '[\\r\\n]', inConstruct: 'phrasing'},
  {character: '\t', before: '[\\r\\n]', inConstruct: 'phrasing'},
  {
    character: '\t',
    inConstruct: ['codeFencedLangGraveAccent', 'codeFencedLangTilde']
  },
  {
    character: '\r',
    inConstruct: [
      'codeFencedLangGraveAccent',
      'codeFencedLangTilde',
      'codeFencedMetaGraveAccent',
      'codeFencedMetaTilde',
      'destinationLiteral',
      'headingAtx'
    ]
  },
  {
    character: '\n',
    inConstruct: [
      'codeFencedLangGraveAccent',
      'codeFencedLangTilde',
      'codeFencedMetaGraveAccent',
      'codeFencedMetaTilde',
      'destinationLiteral',
      'headingAtx'
    ]
  },
  {character: ' ', after: '[\\r\\n]', inConstruct: 'phrasing'},
  {character: ' ', before: '[\\r\\n]', inConstruct: 'phrasing'},
  {
    character: ' ',
    inConstruct: ['codeFencedLangGraveAccent', 'codeFencedLangTilde']
  },
  {
    character: '!',
    after: '\\[',
    inConstruct: 'phrasing',
    notInConstruct: fullPhrasingSpans
  },
  {character: '"', inConstruct: 'titleQuote'},
  {atBreak: true, character: '#'},
  {character: '#', inConstruct: 'headingAtx', after: '(?:[\r\n]|$)'},
  {character: '&', after: '[#A-Za-z]', inConstruct: 'phrasing'},
  {character: "'", inConstruct: 'titleApostrophe'},
  {character: '(', inConstruct: 'destinationRaw'},
  {
    before: '\\]',
    character: '(',
    inConstruct: 'phrasing',
    notInConstruct: fullPhrasingSpans
  },
  {atBreak: true, before: '\\d+', character: ')'},
  {character: ')', inConstruct: 'destinationRaw'},
  {atBreak: true, character: '*', after: '(?:[ \t\r\n*])'},
  {character: '*', inConstruct: 'phrasing', notInConstruct: fullPhrasingSpans},
  {atBreak: true, character: '+', after: '(?:[ \t\r\n])'},
  {atBreak: true, character: '-', after: '(?:[ \t\r\n-])'},
  {atBreak: true, before: '\\d+', character: '.', after: '(?:[ \t\r\n]|$)'},
  {atBreak: true, character: '<', after: '[!/?A-Za-z]'},
  {
    character: '<',
    after: '[!/?A-Za-z]',
    inConstruct: 'phrasing',
    notInConstruct: fullPhrasingSpans
  },
  {character: '<', inConstruct: 'destinationLiteral'},
  {atBreak: true, character: '='},
  {atBreak: true, character: '>'},
  {character: '>', inConstruct: 'destinationLiteral'},
  {atBreak: true, character: '['},
  {character: '[', inConstruct: 'phrasing', notInConstruct: fullPhrasingSpans},
  {character: '[', inConstruct: ['label', 'reference']},
  {character: '\\', after: '[\\r\\n]', inConstruct: 'phrasing'},
  {character: ']', inConstruct: ['label', 'reference']},
  {atBreak: true, character: '_'},
  {character: '_', inConstruct: 'phrasing', notInConstruct: fullPhrasingSpans},
  {atBreak: true, character: '`'},
  {
    character: '`',
    inConstruct: ['codeFencedLangGraveAccent', 'codeFencedMetaGraveAccent']
  },
  {character: '`', inConstruct: 'phrasing', notInConstruct: fullPhrasingSpans},
  {atBreak: true, character: '~'}
];

function association(node) {
  if (node.label || !node.identifier) {
    return node.label || ''
  }
  return decodeString(node.identifier)
}

function compilePattern(pattern) {
  if (!pattern._compiled) {
    const before =
      (pattern.atBreak ? '[\\r\\n][\\t ]*' : '') +
      (pattern.before ? '(?:' + pattern.before + ')' : '');
    pattern._compiled = new RegExp(
      (before ? '(' + before + ')' : '') +
        (/[|\\{}()[\]^$+*?.-]/.test(pattern.character) ? '\\' : '') +
        pattern.character +
        (pattern.after ? '(?:' + pattern.after + ')' : ''),
      'g'
    );
  }
  return pattern._compiled
}

function containerPhrasing(parent, state, info) {
  const indexStack = state.indexStack;
  const children = parent.children || [];
  const results = [];
  let index = -1;
  let before = info.before;
  indexStack.push(-1);
  let tracker = state.createTracker(info);
  while (++index < children.length) {
    const child = children[index];
    let after;
    indexStack[indexStack.length - 1] = index;
    if (index + 1 < children.length) {
      let handle = state.handle.handlers[children[index + 1].type];
      if (handle && handle.peek) handle = handle.peek;
      after = handle
        ? handle(children[index + 1], parent, state, {
            before: '',
            after: '',
            ...tracker.current()
          }).charAt(0)
        : '';
    } else {
      after = info.after;
    }
    if (
      results.length > 0 &&
      (before === '\r' || before === '\n') &&
      child.type === 'html'
    ) {
      results[results.length - 1] = results[results.length - 1].replace(
        /(\r?\n|\r)$/,
        ' '
      );
      before = ' ';
      tracker = state.createTracker(info);
      tracker.move(results.join(''));
    }
    results.push(
      tracker.move(
        state.handle(child, parent, state, {
          ...tracker.current(),
          before,
          after
        })
      )
    );
    before = results[results.length - 1].slice(-1);
  }
  indexStack.pop();
  return results.join('')
}

function containerFlow(parent, state, info) {
  const indexStack = state.indexStack;
  const children = parent.children || [];
  const tracker = state.createTracker(info);
  const results = [];
  let index = -1;
  indexStack.push(-1);
  while (++index < children.length) {
    const child = children[index];
    indexStack[indexStack.length - 1] = index;
    results.push(
      tracker.move(
        state.handle(child, parent, state, {
          before: '\n',
          after: '\n',
          ...tracker.current()
        })
      )
    );
    if (child.type !== 'list') {
      state.bulletLastUsed = undefined;
    }
    if (index < children.length - 1) {
      results.push(
        tracker.move(between(child, children[index + 1], parent, state))
      );
    }
  }
  indexStack.pop();
  return results.join('')
}
function between(left, right, parent, state) {
  let index = state.join.length;
  while (index--) {
    const result = state.join[index](left, right, parent, state);
    if (result === true || result === 1) {
      break
    }
    if (typeof result === 'number') {
      return '\n'.repeat(1 + result)
    }
    if (result === false) {
      return '\n\n<!---->\n\n'
    }
  }
  return '\n\n'
}

const eol$1 = /\r?\n|\r/g;
function indentLines(value, map) {
  const result = [];
  let start = 0;
  let line = 0;
  let match;
  while ((match = eol$1.exec(value))) {
    one(value.slice(start, match.index));
    result.push(match[0]);
    start = match.index + match[0].length;
    line++;
  }
  one(value.slice(start));
  return result.join('')
  function one(value) {
    result.push(map(value, line, !value));
  }
}

function safe(state, input, config) {
  const value = (config.before || '') + (input || '') + (config.after || '');
  const positions = [];
  const result = [];
  const infos = {};
  let index = -1;
  while (++index < state.unsafe.length) {
    const pattern = state.unsafe[index];
    if (!patternInScope(state.stack, pattern)) {
      continue
    }
    const expression = state.compilePattern(pattern);
    let match;
    while ((match = expression.exec(value))) {
      const before = 'before' in pattern || Boolean(pattern.atBreak);
      const after = 'after' in pattern;
      const position = match.index + (before ? match[1].length : 0);
      if (positions.includes(position)) {
        if (infos[position].before && !before) {
          infos[position].before = false;
        }
        if (infos[position].after && !after) {
          infos[position].after = false;
        }
      } else {
        positions.push(position);
        infos[position] = {before, after};
      }
    }
  }
  positions.sort(numerical);
  let start = config.before ? config.before.length : 0;
  const end = value.length - (config.after ? config.after.length : 0);
  index = -1;
  while (++index < positions.length) {
    const position = positions[index];
    if (position < start || position >= end) {
      continue
    }
    if (
      (position + 1 < end &&
        positions[index + 1] === position + 1 &&
        infos[position].after &&
        !infos[position + 1].before &&
        !infos[position + 1].after) ||
      (positions[index - 1] === position - 1 &&
        infos[position].before &&
        !infos[position - 1].before &&
        !infos[position - 1].after)
    ) {
      continue
    }
    if (start !== position) {
      result.push(escapeBackslashes(value.slice(start, position), '\\'));
    }
    start = position;
    if (
      /[!-/:-@[-`{-~]/.test(value.charAt(position)) &&
      (!config.encode || !config.encode.includes(value.charAt(position)))
    ) {
      result.push('\\');
    } else {
      result.push(
        '&#x' + value.charCodeAt(position).toString(16).toUpperCase() + ';'
      );
      start++;
    }
  }
  result.push(escapeBackslashes(value.slice(start, end), config.after));
  return result.join('')
}
function numerical(a, b) {
  return a - b
}
function escapeBackslashes(value, after) {
  const expression = /\\(?=[!-/:-@[-`{-~])/g;
  const positions = [];
  const results = [];
  const whole = value + after;
  let index = -1;
  let start = 0;
  let match;
  while ((match = expression.exec(whole))) {
    positions.push(match.index);
  }
  while (++index < positions.length) {
    if (start !== positions[index]) {
      results.push(value.slice(start, positions[index]));
    }
    results.push('\\');
    start = positions[index];
  }
  results.push(value.slice(start));
  return results.join('')
}

function track(config) {
  const options = config || {};
  const now = options.now || {};
  let lineShift = options.lineShift || 0;
  let line = now.line || 1;
  let column = now.column || 1;
  return {move, current, shift}
  function current() {
    return {now: {line, column}, lineShift}
  }
  function shift(value) {
    lineShift += value;
  }
  function move(input) {
    const value = input || '';
    const chunks = value.split(/\r?\n|\r/g);
    const tail = chunks[chunks.length - 1];
    line += chunks.length - 1;
    column =
      chunks.length === 1 ? column + tail.length : 1 + tail.length + lineShift;
    return value
  }
}

function toMarkdown(tree, options = {}) {
  const state = {
    enter,
    indentLines,
    associationId: association,
    containerPhrasing: containerPhrasingBound,
    containerFlow: containerFlowBound,
    createTracker: track,
    compilePattern,
    safe: safeBound,
    stack: [],
    unsafe: [...unsafe],
    join: [...join],
    handlers: {...handle},
    options: {},
    indexStack: [],
    handle: undefined
  };
  configure(state, options);
  if (state.options.tightDefinitions) {
    state.join.push(joinDefinition);
  }
  state.handle = zwitch('type', {
    invalid,
    unknown,
    handlers: state.handlers
  });
  let result = state.handle(tree, undefined, state, {
    before: '\n',
    after: '\n',
    now: {line: 1, column: 1},
    lineShift: 0
  });
  if (
    result &&
    result.charCodeAt(result.length - 1) !== 10 &&
    result.charCodeAt(result.length - 1) !== 13
  ) {
    result += '\n';
  }
  return result
  function enter(name) {
    state.stack.push(name);
    return exit
    function exit() {
      state.stack.pop();
    }
  }
}
function invalid(value) {
  throw new Error('Cannot handle value `' + value + '`, expected node')
}
function unknown(value) {
  const node =  (value);
  throw new Error('Cannot handle unknown node `' + node.type + '`')
}
function joinDefinition(left, right) {
  if (left.type === 'definition' && left.type === right.type) {
    return 0
  }
}
function containerPhrasingBound(parent, info) {
  return containerPhrasing(parent, this, info)
}
function containerFlowBound(parent, info) {
  return containerFlow(parent, this, info)
}
function safeBound(value, config) {
  return safe(this, value, config)
}

function remarkStringify(options) {
  const self = this;
  self.compiler = compiler;
  function compiler(tree) {
    return toMarkdown(tree, {
      ...self.data('settings'),
      ...options,
      extensions: self.data('toMarkdownExtensions') || []
    })
  }
}

function ccount(value, character) {
  const source = String(value);
  if (typeof character !== 'string') {
    throw new TypeError('Expected character')
  }
  let count = 0;
  let index = source.indexOf(character);
  while (index !== -1) {
    count++;
    index = source.indexOf(character, index + character.length);
  }
  return count
}

function escapeStringRegexp(string) {
	if (typeof string !== 'string') {
		throw new TypeError('Expected a string');
	}
	return string
		.replace(/[|\\{}()[\]^$+*?.]/g, '\\$&')
		.replace(/-/g, '\\x2d');
}

function findAndReplace(tree, list, options) {
  const settings = options || {};
  const ignored = convert(settings.ignore || []);
  const pairs = toPairs(list);
  let pairIndex = -1;
  while (++pairIndex < pairs.length) {
    visitParents(tree, 'text', visitor);
  }
  function visitor(node, parents) {
    let index = -1;
    let grandparent;
    while (++index < parents.length) {
      const parent = parents[index];
      const siblings = grandparent ? grandparent.children : undefined;
      if (
        ignored(
          parent,
          siblings ? siblings.indexOf(parent) : undefined,
          grandparent
        )
      ) {
        return
      }
      grandparent = parent;
    }
    if (grandparent) {
      return handler(node, parents)
    }
  }
  function handler(node, parents) {
    const parent = parents[parents.length - 1];
    const find = pairs[pairIndex][0];
    const replace = pairs[pairIndex][1];
    let start = 0;
    const siblings = parent.children;
    const index = siblings.indexOf(node);
    let change = false;
    let nodes = [];
    find.lastIndex = 0;
    let match = find.exec(node.value);
    while (match) {
      const position = match.index;
      const matchObject = {
        index: match.index,
        input: match.input,
        stack: [...parents, node]
      };
      let value = replace(...match, matchObject);
      if (typeof value === 'string') {
        value = value.length > 0 ? {type: 'text', value} : undefined;
      }
      if (value === false) {
        find.lastIndex = position + 1;
      } else {
        if (start !== position) {
          nodes.push({
            type: 'text',
            value: node.value.slice(start, position)
          });
        }
        if (Array.isArray(value)) {
          nodes.push(...value);
        } else if (value) {
          nodes.push(value);
        }
        start = position + match[0].length;
        change = true;
      }
      if (!find.global) {
        break
      }
      match = find.exec(node.value);
    }
    if (change) {
      if (start < node.value.length) {
        nodes.push({type: 'text', value: node.value.slice(start)});
      }
      parent.children.splice(index, 1, ...nodes);
    } else {
      nodes = [node];
    }
    return index + nodes.length
  }
}
function toPairs(tupleOrList) {
  const result = [];
  if (!Array.isArray(tupleOrList)) {
    throw new TypeError('Expected find and replace tuple or list of tuples')
  }
  const list =
    !tupleOrList[0] || Array.isArray(tupleOrList[0])
      ? tupleOrList
      : [tupleOrList];
  let index = -1;
  while (++index < list.length) {
    const tuple = list[index];
    result.push([toExpression(tuple[0]), toFunction(tuple[1])]);
  }
  return result
}
function toExpression(find) {
  return typeof find === 'string' ? new RegExp(escapeStringRegexp(find), 'g') : find
}
function toFunction(replace) {
  return typeof replace === 'function'
    ? replace
    : function () {
        return replace
      }
}

const inConstruct = 'phrasing';
const notInConstruct = ['autolink', 'link', 'image', 'label'];
function gfmAutolinkLiteralFromMarkdown() {
  return {
    transforms: [transformGfmAutolinkLiterals],
    enter: {
      literalAutolink: enterLiteralAutolink,
      literalAutolinkEmail: enterLiteralAutolinkValue,
      literalAutolinkHttp: enterLiteralAutolinkValue,
      literalAutolinkWww: enterLiteralAutolinkValue
    },
    exit: {
      literalAutolink: exitLiteralAutolink,
      literalAutolinkEmail: exitLiteralAutolinkEmail,
      literalAutolinkHttp: exitLiteralAutolinkHttp,
      literalAutolinkWww: exitLiteralAutolinkWww
    }
  }
}
function gfmAutolinkLiteralToMarkdown() {
  return {
    unsafe: [
      {
        character: '@',
        before: '[+\\-.\\w]',
        after: '[\\-.\\w]',
        inConstruct,
        notInConstruct
      },
      {
        character: '.',
        before: '[Ww]',
        after: '[\\-.\\w]',
        inConstruct,
        notInConstruct
      },
      {
        character: ':',
        before: '[ps]',
        after: '\\/',
        inConstruct,
        notInConstruct
      }
    ]
  }
}
function enterLiteralAutolink(token) {
  this.enter({type: 'link', title: null, url: '', children: []}, token);
}
function enterLiteralAutolinkValue(token) {
  this.config.enter.autolinkProtocol.call(this, token);
}
function exitLiteralAutolinkHttp(token) {
  this.config.exit.autolinkProtocol.call(this, token);
}
function exitLiteralAutolinkWww(token) {
  this.config.exit.data.call(this, token);
  const node = this.stack[this.stack.length - 1];
  ok$1(node.type === 'link');
  node.url = 'http://' + this.sliceSerialize(token);
}
function exitLiteralAutolinkEmail(token) {
  this.config.exit.autolinkEmail.call(this, token);
}
function exitLiteralAutolink(token) {
  this.exit(token);
}
function transformGfmAutolinkLiterals(tree) {
  findAndReplace(
    tree,
    [
      [/(https?:\/\/|www(?=\.))([-.\w]+)([^ \t\r\n]*)/gi, findUrl],
      [/([-.\w+]+)@([-\w]+(?:\.[-\w]+)+)/g, findEmail]
    ],
    {ignore: ['link', 'linkReference']}
  );
}
function findUrl(_, protocol, domain, path, match) {
  let prefix = '';
  if (!previous(match)) {
    return false
  }
  if (/^w/i.test(protocol)) {
    domain = protocol + domain;
    protocol = '';
    prefix = 'http://';
  }
  if (!isCorrectDomain(domain)) {
    return false
  }
  const parts = splitUrl(domain + path);
  if (!parts[0]) return false
  const result = {
    type: 'link',
    title: null,
    url: prefix + protocol + parts[0],
    children: [{type: 'text', value: protocol + parts[0]}]
  };
  if (parts[1]) {
    return [result, {type: 'text', value: parts[1]}]
  }
  return result
}
function findEmail(_, atext, label, match) {
  if (
    !previous(match, true) ||
    /[-\d_]$/.test(label)
  ) {
    return false
  }
  return {
    type: 'link',
    title: null,
    url: 'mailto:' + atext + '@' + label,
    children: [{type: 'text', value: atext + '@' + label}]
  }
}
function isCorrectDomain(domain) {
  const parts = domain.split('.');
  if (
    parts.length < 2 ||
    (parts[parts.length - 1] &&
      (/_/.test(parts[parts.length - 1]) ||
        !/[a-zA-Z\d]/.test(parts[parts.length - 1]))) ||
    (parts[parts.length - 2] &&
      (/_/.test(parts[parts.length - 2]) ||
        !/[a-zA-Z\d]/.test(parts[parts.length - 2])))
  ) {
    return false
  }
  return true
}
function splitUrl(url) {
  const trailExec = /[!"&'),.:;<>?\]}]+$/.exec(url);
  if (!trailExec) {
    return [url, undefined]
  }
  url = url.slice(0, trailExec.index);
  let trail = trailExec[0];
  let closingParenIndex = trail.indexOf(')');
  const openingParens = ccount(url, '(');
  let closingParens = ccount(url, ')');
  while (closingParenIndex !== -1 && openingParens > closingParens) {
    url += trail.slice(0, closingParenIndex + 1);
    trail = trail.slice(closingParenIndex + 1);
    closingParenIndex = trail.indexOf(')');
    closingParens++;
  }
  return [url, trail]
}
function previous(match, email) {
  const code = match.input.charCodeAt(match.index - 1);
  return (
    (match.index === 0 ||
      unicodeWhitespace(code) ||
      unicodePunctuation(code)) &&
    (!email || code !== 47)
  )
}

footnoteReference.peek = footnoteReferencePeek;
function gfmFootnoteFromMarkdown() {
  return {
    enter: {
      gfmFootnoteDefinition: enterFootnoteDefinition,
      gfmFootnoteDefinitionLabelString: enterFootnoteDefinitionLabelString,
      gfmFootnoteCall: enterFootnoteCall,
      gfmFootnoteCallString: enterFootnoteCallString
    },
    exit: {
      gfmFootnoteDefinition: exitFootnoteDefinition,
      gfmFootnoteDefinitionLabelString: exitFootnoteDefinitionLabelString,
      gfmFootnoteCall: exitFootnoteCall,
      gfmFootnoteCallString: exitFootnoteCallString
    }
  }
}
function gfmFootnoteToMarkdown() {
  return {
    unsafe: [{character: '[', inConstruct: ['phrasing', 'label', 'reference']}],
    handlers: {footnoteDefinition, footnoteReference}
  }
}
function enterFootnoteDefinition(token) {
  this.enter(
    {type: 'footnoteDefinition', identifier: '', label: '', children: []},
    token
  );
}
function enterFootnoteDefinitionLabelString() {
  this.buffer();
}
function exitFootnoteDefinitionLabelString(token) {
  const label = this.resume();
  const node = this.stack[this.stack.length - 1];
  ok$1(node.type === 'footnoteDefinition');
  node.label = label;
  node.identifier = normalizeIdentifier(
    this.sliceSerialize(token)
  ).toLowerCase();
}
function exitFootnoteDefinition(token) {
  this.exit(token);
}
function enterFootnoteCall(token) {
  this.enter({type: 'footnoteReference', identifier: '', label: ''}, token);
}
function enterFootnoteCallString() {
  this.buffer();
}
function exitFootnoteCallString(token) {
  const label = this.resume();
  const node = this.stack[this.stack.length - 1];
  ok$1(node.type === 'footnoteReference');
  node.label = label;
  node.identifier = normalizeIdentifier(
    this.sliceSerialize(token)
  ).toLowerCase();
}
function exitFootnoteCall(token) {
  this.exit(token);
}
function footnoteReference(node, _, state, info) {
  const tracker = state.createTracker(info);
  let value = tracker.move('[^');
  const exit = state.enter('footnoteReference');
  const subexit = state.enter('reference');
  value += tracker.move(
    state.safe(state.associationId(node), {
      ...tracker.current(),
      before: value,
      after: ']'
    })
  );
  subexit();
  exit();
  value += tracker.move(']');
  return value
}
function footnoteReferencePeek() {
  return '['
}
function footnoteDefinition(node, _, state, info) {
  const tracker = state.createTracker(info);
  let value = tracker.move('[^');
  const exit = state.enter('footnoteDefinition');
  const subexit = state.enter('label');
  value += tracker.move(
    state.safe(state.associationId(node), {
      ...tracker.current(),
      before: value,
      after: ']'
    })
  );
  subexit();
  value += tracker.move(
    ']:' + (node.children && node.children.length > 0 ? ' ' : '')
  );
  tracker.shift(4);
  value += tracker.move(
    state.indentLines(state.containerFlow(node, tracker.current()), map$1)
  );
  exit();
  return value
}
function map$1(line, index, blank) {
  if (index === 0) {
    return line
  }
  return (blank ? '' : '    ') + line
}

const constructsWithoutStrikethrough = [
  'autolink',
  'destinationLiteral',
  'destinationRaw',
  'reference',
  'titleQuote',
  'titleApostrophe'
];
handleDelete.peek = peekDelete;
function gfmStrikethroughFromMarkdown() {
  return {
    canContainEols: ['delete'],
    enter: {strikethrough: enterStrikethrough},
    exit: {strikethrough: exitStrikethrough}
  }
}
function gfmStrikethroughToMarkdown() {
  return {
    unsafe: [
      {
        character: '~',
        inConstruct: 'phrasing',
        notInConstruct: constructsWithoutStrikethrough
      }
    ],
    handlers: {delete: handleDelete}
  }
}
function enterStrikethrough(token) {
  this.enter({type: 'delete', children: []}, token);
}
function exitStrikethrough(token) {
  this.exit(token);
}
function handleDelete(node, _, state, info) {
  const tracker = state.createTracker(info);
  const exit = state.enter('strikethrough');
  let value = tracker.move('~~');
  value += state.containerPhrasing(node, {
    ...tracker.current(),
    before: value,
    after: '~'
  });
  value += tracker.move('~~');
  exit();
  return value
}
function peekDelete() {
  return '~'
}

function markdownTable(table, options = {}) {
  const align = (options.align || []).concat();
  const stringLength = options.stringLength || defaultStringLength;
  const alignments = [];
  const cellMatrix = [];
  const sizeMatrix = [];
  const longestCellByColumn = [];
  let mostCellsPerRow = 0;
  let rowIndex = -1;
  while (++rowIndex < table.length) {
    const row = [];
    const sizes = [];
    let columnIndex = -1;
    if (table[rowIndex].length > mostCellsPerRow) {
      mostCellsPerRow = table[rowIndex].length;
    }
    while (++columnIndex < table[rowIndex].length) {
      const cell = serialize(table[rowIndex][columnIndex]);
      if (options.alignDelimiters !== false) {
        const size = stringLength(cell);
        sizes[columnIndex] = size;
        if (
          longestCellByColumn[columnIndex] === undefined ||
          size > longestCellByColumn[columnIndex]
        ) {
          longestCellByColumn[columnIndex] = size;
        }
      }
      row.push(cell);
    }
    cellMatrix[rowIndex] = row;
    sizeMatrix[rowIndex] = sizes;
  }
  let columnIndex = -1;
  if (typeof align === 'object' && 'length' in align) {
    while (++columnIndex < mostCellsPerRow) {
      alignments[columnIndex] = toAlignment(align[columnIndex]);
    }
  } else {
    const code = toAlignment(align);
    while (++columnIndex < mostCellsPerRow) {
      alignments[columnIndex] = code;
    }
  }
  columnIndex = -1;
  const row = [];
  const sizes = [];
  while (++columnIndex < mostCellsPerRow) {
    const code = alignments[columnIndex];
    let before = '';
    let after = '';
    if (code === 99 ) {
      before = ':';
      after = ':';
    } else if (code === 108 ) {
      before = ':';
    } else if (code === 114 ) {
      after = ':';
    }
    let size =
      options.alignDelimiters === false
        ? 1
        : Math.max(
            1,
            longestCellByColumn[columnIndex] - before.length - after.length
          );
    const cell = before + '-'.repeat(size) + after;
    if (options.alignDelimiters !== false) {
      size = before.length + size + after.length;
      if (size > longestCellByColumn[columnIndex]) {
        longestCellByColumn[columnIndex] = size;
      }
      sizes[columnIndex] = size;
    }
    row[columnIndex] = cell;
  }
  cellMatrix.splice(1, 0, row);
  sizeMatrix.splice(1, 0, sizes);
  rowIndex = -1;
  const lines = [];
  while (++rowIndex < cellMatrix.length) {
    const row = cellMatrix[rowIndex];
    const sizes = sizeMatrix[rowIndex];
    columnIndex = -1;
    const line = [];
    while (++columnIndex < mostCellsPerRow) {
      const cell = row[columnIndex] || '';
      let before = '';
      let after = '';
      if (options.alignDelimiters !== false) {
        const size =
          longestCellByColumn[columnIndex] - (sizes[columnIndex] || 0);
        const code = alignments[columnIndex];
        if (code === 114 ) {
          before = ' '.repeat(size);
        } else if (code === 99 ) {
          if (size % 2) {
            before = ' '.repeat(size / 2 + 0.5);
            after = ' '.repeat(size / 2 - 0.5);
          } else {
            before = ' '.repeat(size / 2);
            after = before;
          }
        } else {
          after = ' '.repeat(size);
        }
      }
      if (options.delimiterStart !== false && !columnIndex) {
        line.push('|');
      }
      if (
        options.padding !== false &&
        !(options.alignDelimiters === false && cell === '') &&
        (options.delimiterStart !== false || columnIndex)
      ) {
        line.push(' ');
      }
      if (options.alignDelimiters !== false) {
        line.push(before);
      }
      line.push(cell);
      if (options.alignDelimiters !== false) {
        line.push(after);
      }
      if (options.padding !== false) {
        line.push(' ');
      }
      if (
        options.delimiterEnd !== false ||
        columnIndex !== mostCellsPerRow - 1
      ) {
        line.push('|');
      }
    }
    lines.push(
      options.delimiterEnd === false
        ? line.join('').replace(/ +$/, '')
        : line.join('')
    );
  }
  return lines.join('\n')
}
function serialize(value) {
  return value === null || value === undefined ? '' : String(value)
}
function defaultStringLength(value) {
  return value.length
}
function toAlignment(value) {
  const code = typeof value === 'string' ? value.codePointAt(0) : 0;
  return code === 67  || code === 99
    ? 99
    : code === 76  || code === 108
    ? 108
    : code === 82  || code === 114
    ? 114
    : 0
}

function gfmTableFromMarkdown() {
  return {
    enter: {
      table: enterTable,
      tableData: enterCell,
      tableHeader: enterCell,
      tableRow: enterRow
    },
    exit: {
      codeText: exitCodeText,
      table: exitTable,
      tableData: exit,
      tableHeader: exit,
      tableRow: exit
    }
  }
}
function enterTable(token) {
  const align = token._align;
  this.enter(
    {
      type: 'table',
      align: align.map(function (d) {
        return d === 'none' ? null : d
      }),
      children: []
    },
    token
  );
  this.data.inTable = true;
}
function exitTable(token) {
  this.exit(token);
  this.data.inTable = undefined;
}
function enterRow(token) {
  this.enter({type: 'tableRow', children: []}, token);
}
function exit(token) {
  this.exit(token);
}
function enterCell(token) {
  this.enter({type: 'tableCell', children: []}, token);
}
function exitCodeText(token) {
  let value = this.resume();
  if (this.data.inTable) {
    value = value.replace(/\\([\\|])/g, replace);
  }
  const node = this.stack[this.stack.length - 1];
  ok$1(node.type === 'inlineCode');
  node.value = value;
  this.exit(token);
}
function replace($0, $1) {
  return $1 === '|' ? $1 : $0
}
function gfmTableToMarkdown(options) {
  const settings = options || {};
  const padding = settings.tableCellPadding;
  const alignDelimiters = settings.tablePipeAlign;
  const stringLength = settings.stringLength;
  const around = padding ? ' ' : '|';
  return {
    unsafe: [
      {character: '\r', inConstruct: 'tableCell'},
      {character: '\n', inConstruct: 'tableCell'},
      {atBreak: true, character: '|', after: '[\t :-]'},
      {character: '|', inConstruct: 'tableCell'},
      {atBreak: true, character: ':', after: '-'},
      {atBreak: true, character: '-', after: '[:|-]'}
    ],
    handlers: {
      inlineCode: inlineCodeWithTable,
      table: handleTable,
      tableCell: handleTableCell,
      tableRow: handleTableRow
    }
  }
  function handleTable(node, _, state, info) {
    return serializeData(handleTableAsData(node, state, info), node.align)
  }
  function handleTableRow(node, _, state, info) {
    const row = handleTableRowAsData(node, state, info);
    const value = serializeData([row]);
    return value.slice(0, value.indexOf('\n'))
  }
  function handleTableCell(node, _, state, info) {
    const exit = state.enter('tableCell');
    const subexit = state.enter('phrasing');
    const value = state.containerPhrasing(node, {
      ...info,
      before: around,
      after: around
    });
    subexit();
    exit();
    return value
  }
  function serializeData(matrix, align) {
    return markdownTable(matrix, {
      align,
      alignDelimiters,
      padding,
      stringLength
    })
  }
  function handleTableAsData(node, state, info) {
    const children = node.children;
    let index = -1;
    const result = [];
    const subexit = state.enter('table');
    while (++index < children.length) {
      result[index] = handleTableRowAsData(children[index], state, info);
    }
    subexit();
    return result
  }
  function handleTableRowAsData(node, state, info) {
    const children = node.children;
    let index = -1;
    const result = [];
    const subexit = state.enter('tableRow');
    while (++index < children.length) {
      result[index] = handleTableCell(children[index], node, state, info);
    }
    subexit();
    return result
  }
  function inlineCodeWithTable(node, parent, state) {
    let value = handle.inlineCode(node, parent, state);
    if (state.stack.includes('tableCell')) {
      value = value.replace(/\|/g, '\\$&');
    }
    return value
  }
}

function gfmTaskListItemFromMarkdown() {
  return {
    exit: {
      taskListCheckValueChecked: exitCheck,
      taskListCheckValueUnchecked: exitCheck,
      paragraph: exitParagraphWithTaskListItem
    }
  }
}
function gfmTaskListItemToMarkdown() {
  return {
    unsafe: [{atBreak: true, character: '-', after: '[:|-]'}],
    handlers: {listItem: listItemWithTaskListItem}
  }
}
function exitCheck(token) {
  const node = this.stack[this.stack.length - 2];
  ok$1(node.type === 'listItem');
  node.checked = token.type === 'taskListCheckValueChecked';
}
function exitParagraphWithTaskListItem(token) {
  const parent = this.stack[this.stack.length - 2];
  if (
    parent &&
    parent.type === 'listItem' &&
    typeof parent.checked === 'boolean'
  ) {
    const node = this.stack[this.stack.length - 1];
    ok$1(node.type === 'paragraph');
    const head = node.children[0];
    if (head && head.type === 'text') {
      const siblings = parent.children;
      let index = -1;
      let firstParaghraph;
      while (++index < siblings.length) {
        const sibling = siblings[index];
        if (sibling.type === 'paragraph') {
          firstParaghraph = sibling;
          break
        }
      }
      if (firstParaghraph === node) {
        head.value = head.value.slice(1);
        if (head.value.length === 0) {
          node.children.shift();
        } else if (
          node.position &&
          head.position &&
          typeof head.position.start.offset === 'number'
        ) {
          head.position.start.column++;
          head.position.start.offset++;
          node.position.start = Object.assign({}, head.position.start);
        }
      }
    }
  }
  this.exit(token);
}
function listItemWithTaskListItem(node, parent, state, info) {
  const head = node.children[0];
  const checkable =
    typeof node.checked === 'boolean' && head && head.type === 'paragraph';
  const checkbox = '[' + (node.checked ? 'x' : ' ') + '] ';
  const tracker = state.createTracker(info);
  if (checkable) {
    tracker.move(checkbox);
  }
  let value = handle.listItem(node, parent, state, {
    ...info,
    ...tracker.current()
  });
  if (checkable) {
    value = value.replace(/^(?:[*+-]|\d+\.)([\r\n]| {1,3})/, check);
  }
  return value
  function check($0) {
    return $0 + checkbox
  }
}

function gfmFromMarkdown() {
  return [
    gfmAutolinkLiteralFromMarkdown(),
    gfmFootnoteFromMarkdown(),
    gfmStrikethroughFromMarkdown(),
    gfmTableFromMarkdown(),
    gfmTaskListItemFromMarkdown()
  ]
}
function gfmToMarkdown(options) {
  return {
    extensions: [
      gfmAutolinkLiteralToMarkdown(),
      gfmFootnoteToMarkdown(),
      gfmStrikethroughToMarkdown(),
      gfmTableToMarkdown(options),
      gfmTaskListItemToMarkdown()
    ]
  }
}

const wwwPrefix = {
  tokenize: tokenizeWwwPrefix,
  partial: true
};
const domain = {
  tokenize: tokenizeDomain,
  partial: true
};
const path = {
  tokenize: tokenizePath,
  partial: true
};
const trail = {
  tokenize: tokenizeTrail,
  partial: true
};
const emailDomainDotTrail = {
  tokenize: tokenizeEmailDomainDotTrail,
  partial: true
};
const wwwAutolink = {
  name: 'wwwAutolink',
  tokenize: tokenizeWwwAutolink,
  previous: previousWww
};
const protocolAutolink = {
  name: 'protocolAutolink',
  tokenize: tokenizeProtocolAutolink,
  previous: previousProtocol
};
const emailAutolink = {
  name: 'emailAutolink',
  tokenize: tokenizeEmailAutolink,
  previous: previousEmail
};
const text = {};
function gfmAutolinkLiteral() {
  return {
    text
  };
}
let code = 48;
while (code < 123) {
  text[code] = emailAutolink;
  code++;
  if (code === 58) code = 65;else if (code === 91) code = 97;
}
text[43] = emailAutolink;
text[45] = emailAutolink;
text[46] = emailAutolink;
text[95] = emailAutolink;
text[72] = [emailAutolink, protocolAutolink];
text[104] = [emailAutolink, protocolAutolink];
text[87] = [emailAutolink, wwwAutolink];
text[119] = [emailAutolink, wwwAutolink];
function tokenizeEmailAutolink(effects, ok, nok) {
  const self = this;
  let dot;
  let data;
  return start;
  function start(code) {
    if (!gfmAtext(code) || !previousEmail.call(self, self.previous) || previousUnbalanced(self.events)) {
      return nok(code);
    }
    effects.enter('literalAutolink');
    effects.enter('literalAutolinkEmail');
    return atext(code);
  }
  function atext(code) {
    if (gfmAtext(code)) {
      effects.consume(code);
      return atext;
    }
    if (code === 64) {
      effects.consume(code);
      return emailDomain;
    }
    return nok(code);
  }
  function emailDomain(code) {
    if (code === 46) {
      return effects.check(emailDomainDotTrail, emailDomainAfter, emailDomainDot)(code);
    }
    if (code === 45 || code === 95 || asciiAlphanumeric(code)) {
      data = true;
      effects.consume(code);
      return emailDomain;
    }
    return emailDomainAfter(code);
  }
  function emailDomainDot(code) {
    effects.consume(code);
    dot = true;
    return emailDomain;
  }
  function emailDomainAfter(code) {
    if (data && dot && asciiAlpha(self.previous)) {
      effects.exit('literalAutolinkEmail');
      effects.exit('literalAutolink');
      return ok(code);
    }
    return nok(code);
  }
}
function tokenizeWwwAutolink(effects, ok, nok) {
  const self = this;
  return wwwStart;
  function wwwStart(code) {
    if (code !== 87 && code !== 119 || !previousWww.call(self, self.previous) || previousUnbalanced(self.events)) {
      return nok(code);
    }
    effects.enter('literalAutolink');
    effects.enter('literalAutolinkWww');
    return effects.check(wwwPrefix, effects.attempt(domain, effects.attempt(path, wwwAfter), nok), nok)(code);
  }
  function wwwAfter(code) {
    effects.exit('literalAutolinkWww');
    effects.exit('literalAutolink');
    return ok(code);
  }
}
function tokenizeProtocolAutolink(effects, ok, nok) {
  const self = this;
  let buffer = '';
  let seen = false;
  return protocolStart;
  function protocolStart(code) {
    if ((code === 72 || code === 104) && previousProtocol.call(self, self.previous) && !previousUnbalanced(self.events)) {
      effects.enter('literalAutolink');
      effects.enter('literalAutolinkHttp');
      buffer += String.fromCodePoint(code);
      effects.consume(code);
      return protocolPrefixInside;
    }
    return nok(code);
  }
  function protocolPrefixInside(code) {
    if (asciiAlpha(code) && buffer.length < 5) {
      buffer += String.fromCodePoint(code);
      effects.consume(code);
      return protocolPrefixInside;
    }
    if (code === 58) {
      const protocol = buffer.toLowerCase();
      if (protocol === 'http' || protocol === 'https') {
        effects.consume(code);
        return protocolSlashesInside;
      }
    }
    return nok(code);
  }
  function protocolSlashesInside(code) {
    if (code === 47) {
      effects.consume(code);
      if (seen) {
        return afterProtocol;
      }
      seen = true;
      return protocolSlashesInside;
    }
    return nok(code);
  }
  function afterProtocol(code) {
    return code === null || asciiControl(code) || markdownLineEndingOrSpace(code) || unicodeWhitespace(code) || unicodePunctuation(code) ? nok(code) : effects.attempt(domain, effects.attempt(path, protocolAfter), nok)(code);
  }
  function protocolAfter(code) {
    effects.exit('literalAutolinkHttp');
    effects.exit('literalAutolink');
    return ok(code);
  }
}
function tokenizeWwwPrefix(effects, ok, nok) {
  let size = 0;
  return wwwPrefixInside;
  function wwwPrefixInside(code) {
    if ((code === 87 || code === 119) && size < 3) {
      size++;
      effects.consume(code);
      return wwwPrefixInside;
    }
    if (code === 46 && size === 3) {
      effects.consume(code);
      return wwwPrefixAfter;
    }
    return nok(code);
  }
  function wwwPrefixAfter(code) {
    return code === null ? nok(code) : ok(code);
  }
}
function tokenizeDomain(effects, ok, nok) {
  let underscoreInLastSegment;
  let underscoreInLastLastSegment;
  let seen;
  return domainInside;
  function domainInside(code) {
    if (code === 46 || code === 95) {
      return effects.check(trail, domainAfter, domainAtPunctuation)(code);
    }
    if (code === null || markdownLineEndingOrSpace(code) || unicodeWhitespace(code) || code !== 45 && unicodePunctuation(code)) {
      return domainAfter(code);
    }
    seen = true;
    effects.consume(code);
    return domainInside;
  }
  function domainAtPunctuation(code) {
    if (code === 95) {
      underscoreInLastSegment = true;
    }
    else {
      underscoreInLastLastSegment = underscoreInLastSegment;
      underscoreInLastSegment = undefined;
    }
    effects.consume(code);
    return domainInside;
  }
  function domainAfter(code) {
    if (underscoreInLastLastSegment || underscoreInLastSegment || !seen) {
      return nok(code);
    }
    return ok(code);
  }
}
function tokenizePath(effects, ok) {
  let sizeOpen = 0;
  let sizeClose = 0;
  return pathInside;
  function pathInside(code) {
    if (code === 40) {
      sizeOpen++;
      effects.consume(code);
      return pathInside;
    }
    if (code === 41 && sizeClose < sizeOpen) {
      return pathAtPunctuation(code);
    }
    if (code === 33 || code === 34 || code === 38 || code === 39 || code === 41 || code === 42 || code === 44 || code === 46 || code === 58 || code === 59 || code === 60 || code === 63 || code === 93 || code === 95 || code === 126) {
      return effects.check(trail, ok, pathAtPunctuation)(code);
    }
    if (code === null || markdownLineEndingOrSpace(code) || unicodeWhitespace(code)) {
      return ok(code);
    }
    effects.consume(code);
    return pathInside;
  }
  function pathAtPunctuation(code) {
    if (code === 41) {
      sizeClose++;
    }
    effects.consume(code);
    return pathInside;
  }
}
function tokenizeTrail(effects, ok, nok) {
  return trail;
  function trail(code) {
    if (code === 33 || code === 34 || code === 39 || code === 41 || code === 42 || code === 44 || code === 46 || code === 58 || code === 59 || code === 63 || code === 95 || code === 126) {
      effects.consume(code);
      return trail;
    }
    if (code === 38) {
      effects.consume(code);
      return trailCharacterReferenceStart;
    }
    if (code === 93) {
      effects.consume(code);
      return trailBracketAfter;
    }
    if (
    code === 60 ||
    code === null || markdownLineEndingOrSpace(code) || unicodeWhitespace(code)) {
      return ok(code);
    }
    return nok(code);
  }
  function trailBracketAfter(code) {
    if (code === null || code === 40 || code === 91 || markdownLineEndingOrSpace(code) || unicodeWhitespace(code)) {
      return ok(code);
    }
    return trail(code);
  }
  function trailCharacterReferenceStart(code) {
    return asciiAlpha(code) ? trailCharacterReferenceInside(code) : nok(code);
  }
  function trailCharacterReferenceInside(code) {
    if (code === 59) {
      effects.consume(code);
      return trail;
    }
    if (asciiAlpha(code)) {
      effects.consume(code);
      return trailCharacterReferenceInside;
    }
    return nok(code);
  }
}
function tokenizeEmailDomainDotTrail(effects, ok, nok) {
  return start;
  function start(code) {
    effects.consume(code);
    return after;
  }
  function after(code) {
    return asciiAlphanumeric(code) ? nok(code) : ok(code);
  }
}
function previousWww(code) {
  return code === null || code === 40 || code === 42 || code === 95 || code === 91 || code === 93 || code === 126 || markdownLineEndingOrSpace(code);
}
function previousProtocol(code) {
  return !asciiAlpha(code);
}
function previousEmail(code) {
  return !(code === 47 || gfmAtext(code));
}
function gfmAtext(code) {
  return code === 43 || code === 45 || code === 46 || code === 95 || asciiAlphanumeric(code);
}
function previousUnbalanced(events) {
  let index = events.length;
  let result = false;
  while (index--) {
    const token = events[index][1];
    if ((token.type === 'labelLink' || token.type === 'labelImage') && !token._balanced) {
      result = true;
      break;
    }
    if (token._gfmAutolinkLiteralWalkedInto) {
      result = false;
      break;
    }
  }
  if (events.length > 0 && !result) {
    events[events.length - 1][1]._gfmAutolinkLiteralWalkedInto = true;
  }
  return result;
}

const indent = {
  tokenize: tokenizeIndent,
  partial: true
};
function gfmFootnote() {
  return {
    document: {
      [91]: {
        name: 'gfmFootnoteDefinition',
        tokenize: tokenizeDefinitionStart,
        continuation: {
          tokenize: tokenizeDefinitionContinuation
        },
        exit: gfmFootnoteDefinitionEnd
      }
    },
    text: {
      [91]: {
        name: 'gfmFootnoteCall',
        tokenize: tokenizeGfmFootnoteCall
      },
      [93]: {
        name: 'gfmPotentialFootnoteCall',
        add: 'after',
        tokenize: tokenizePotentialGfmFootnoteCall,
        resolveTo: resolveToPotentialGfmFootnoteCall
      }
    }
  };
}
function tokenizePotentialGfmFootnoteCall(effects, ok, nok) {
  const self = this;
  let index = self.events.length;
  const defined = self.parser.gfmFootnotes || (self.parser.gfmFootnotes = []);
  let labelStart;
  while (index--) {
    const token = self.events[index][1];
    if (token.type === "labelImage") {
      labelStart = token;
      break;
    }
    if (token.type === 'gfmFootnoteCall' || token.type === "labelLink" || token.type === "label" || token.type === "image" || token.type === "link") {
      break;
    }
  }
  return start;
  function start(code) {
    if (!labelStart || !labelStart._balanced) {
      return nok(code);
    }
    const id = normalizeIdentifier(self.sliceSerialize({
      start: labelStart.end,
      end: self.now()
    }));
    if (id.codePointAt(0) !== 94 || !defined.includes(id.slice(1))) {
      return nok(code);
    }
    effects.enter('gfmFootnoteCallLabelMarker');
    effects.consume(code);
    effects.exit('gfmFootnoteCallLabelMarker');
    return ok(code);
  }
}
function resolveToPotentialGfmFootnoteCall(events, context) {
  let index = events.length;
  while (index--) {
    if (events[index][1].type === "labelImage" && events[index][0] === 'enter') {
      events[index][1];
      break;
    }
  }
  events[index + 1][1].type = "data";
  events[index + 3][1].type = 'gfmFootnoteCallLabelMarker';
  const call = {
    type: 'gfmFootnoteCall',
    start: Object.assign({}, events[index + 3][1].start),
    end: Object.assign({}, events[events.length - 1][1].end)
  };
  const marker = {
    type: 'gfmFootnoteCallMarker',
    start: Object.assign({}, events[index + 3][1].end),
    end: Object.assign({}, events[index + 3][1].end)
  };
  marker.end.column++;
  marker.end.offset++;
  marker.end._bufferIndex++;
  const string = {
    type: 'gfmFootnoteCallString',
    start: Object.assign({}, marker.end),
    end: Object.assign({}, events[events.length - 1][1].start)
  };
  const chunk = {
    type: "chunkString",
    contentType: 'string',
    start: Object.assign({}, string.start),
    end: Object.assign({}, string.end)
  };
  const replacement = [
  events[index + 1], events[index + 2], ['enter', call, context],
  events[index + 3], events[index + 4],
  ['enter', marker, context], ['exit', marker, context],
  ['enter', string, context], ['enter', chunk, context], ['exit', chunk, context], ['exit', string, context],
  events[events.length - 2], events[events.length - 1], ['exit', call, context]];
  events.splice(index, events.length - index + 1, ...replacement);
  return events;
}
function tokenizeGfmFootnoteCall(effects, ok, nok) {
  const self = this;
  const defined = self.parser.gfmFootnotes || (self.parser.gfmFootnotes = []);
  let size = 0;
  let data;
  return start;
  function start(code) {
    effects.enter('gfmFootnoteCall');
    effects.enter('gfmFootnoteCallLabelMarker');
    effects.consume(code);
    effects.exit('gfmFootnoteCallLabelMarker');
    return callStart;
  }
  function callStart(code) {
    if (code !== 94) return nok(code);
    effects.enter('gfmFootnoteCallMarker');
    effects.consume(code);
    effects.exit('gfmFootnoteCallMarker');
    effects.enter('gfmFootnoteCallString');
    effects.enter('chunkString').contentType = 'string';
    return callData;
  }
  function callData(code) {
    if (
    size > 999 ||
    code === 93 && !data ||
    code === null || code === 91 || markdownLineEndingOrSpace(code)) {
      return nok(code);
    }
    if (code === 93) {
      effects.exit('chunkString');
      const token = effects.exit('gfmFootnoteCallString');
      if (!defined.includes(normalizeIdentifier(self.sliceSerialize(token)))) {
        return nok(code);
      }
      effects.enter('gfmFootnoteCallLabelMarker');
      effects.consume(code);
      effects.exit('gfmFootnoteCallLabelMarker');
      effects.exit('gfmFootnoteCall');
      return ok;
    }
    if (!markdownLineEndingOrSpace(code)) {
      data = true;
    }
    size++;
    effects.consume(code);
    return code === 92 ? callEscape : callData;
  }
  function callEscape(code) {
    if (code === 91 || code === 92 || code === 93) {
      effects.consume(code);
      size++;
      return callData;
    }
    return callData(code);
  }
}
function tokenizeDefinitionStart(effects, ok, nok) {
  const self = this;
  const defined = self.parser.gfmFootnotes || (self.parser.gfmFootnotes = []);
  let identifier;
  let size = 0;
  let data;
  return start;
  function start(code) {
    effects.enter('gfmFootnoteDefinition')._container = true;
    effects.enter('gfmFootnoteDefinitionLabel');
    effects.enter('gfmFootnoteDefinitionLabelMarker');
    effects.consume(code);
    effects.exit('gfmFootnoteDefinitionLabelMarker');
    return labelAtMarker;
  }
  function labelAtMarker(code) {
    if (code === 94) {
      effects.enter('gfmFootnoteDefinitionMarker');
      effects.consume(code);
      effects.exit('gfmFootnoteDefinitionMarker');
      effects.enter('gfmFootnoteDefinitionLabelString');
      effects.enter('chunkString').contentType = 'string';
      return labelInside;
    }
    return nok(code);
  }
  function labelInside(code) {
    if (
    size > 999 ||
    code === 93 && !data ||
    code === null || code === 91 || markdownLineEndingOrSpace(code)) {
      return nok(code);
    }
    if (code === 93) {
      effects.exit('chunkString');
      const token = effects.exit('gfmFootnoteDefinitionLabelString');
      identifier = normalizeIdentifier(self.sliceSerialize(token));
      effects.enter('gfmFootnoteDefinitionLabelMarker');
      effects.consume(code);
      effects.exit('gfmFootnoteDefinitionLabelMarker');
      effects.exit('gfmFootnoteDefinitionLabel');
      return labelAfter;
    }
    if (!markdownLineEndingOrSpace(code)) {
      data = true;
    }
    size++;
    effects.consume(code);
    return code === 92 ? labelEscape : labelInside;
  }
  function labelEscape(code) {
    if (code === 91 || code === 92 || code === 93) {
      effects.consume(code);
      size++;
      return labelInside;
    }
    return labelInside(code);
  }
  function labelAfter(code) {
    if (code === 58) {
      effects.enter('definitionMarker');
      effects.consume(code);
      effects.exit('definitionMarker');
      if (!defined.includes(identifier)) {
        defined.push(identifier);
      }
      return factorySpace(effects, whitespaceAfter, 'gfmFootnoteDefinitionWhitespace');
    }
    return nok(code);
  }
  function whitespaceAfter(code) {
    return ok(code);
  }
}
function tokenizeDefinitionContinuation(effects, ok, nok) {
  return effects.check(blankLine, ok, effects.attempt(indent, ok, nok));
}
function gfmFootnoteDefinitionEnd(effects) {
  effects.exit('gfmFootnoteDefinition');
}
function tokenizeIndent(effects, ok, nok) {
  const self = this;
  return factorySpace(effects, afterPrefix, 'gfmFootnoteDefinitionIndent', 4 + 1);
  function afterPrefix(code) {
    const tail = self.events[self.events.length - 1];
    return tail && tail[1].type === 'gfmFootnoteDefinitionIndent' && tail[2].sliceSerialize(tail[1], true).length === 4 ? ok(code) : nok(code);
  }
}

function gfmStrikethrough(options) {
  const options_ = options || {};
  let single = options_.singleTilde;
  const tokenizer = {
    name: 'strikethrough',
    tokenize: tokenizeStrikethrough,
    resolveAll: resolveAllStrikethrough
  };
  if (single === null || single === undefined) {
    single = true;
  }
  return {
    text: {
      [126]: tokenizer
    },
    insideSpan: {
      null: [tokenizer]
    },
    attentionMarkers: {
      null: [126]
    }
  };
  function resolveAllStrikethrough(events, context) {
    let index = -1;
    while (++index < events.length) {
      if (events[index][0] === 'enter' && events[index][1].type === 'strikethroughSequenceTemporary' && events[index][1]._close) {
        let open = index;
        while (open--) {
          if (events[open][0] === 'exit' && events[open][1].type === 'strikethroughSequenceTemporary' && events[open][1]._open &&
          events[index][1].end.offset - events[index][1].start.offset === events[open][1].end.offset - events[open][1].start.offset) {
            events[index][1].type = 'strikethroughSequence';
            events[open][1].type = 'strikethroughSequence';
            const strikethrough = {
              type: 'strikethrough',
              start: Object.assign({}, events[open][1].start),
              end: Object.assign({}, events[index][1].end)
            };
            const text = {
              type: 'strikethroughText',
              start: Object.assign({}, events[open][1].end),
              end: Object.assign({}, events[index][1].start)
            };
            const nextEvents = [['enter', strikethrough, context], ['enter', events[open][1], context], ['exit', events[open][1], context], ['enter', text, context]];
            const insideSpan = context.parser.constructs.insideSpan.null;
            if (insideSpan) {
              splice(nextEvents, nextEvents.length, 0, resolveAll(insideSpan, events.slice(open + 1, index), context));
            }
            splice(nextEvents, nextEvents.length, 0, [['exit', text, context], ['enter', events[index][1], context], ['exit', events[index][1], context], ['exit', strikethrough, context]]);
            splice(events, open - 1, index - open + 3, nextEvents);
            index = open + nextEvents.length - 2;
            break;
          }
        }
      }
    }
    index = -1;
    while (++index < events.length) {
      if (events[index][1].type === 'strikethroughSequenceTemporary') {
        events[index][1].type = "data";
      }
    }
    return events;
  }
  function tokenizeStrikethrough(effects, ok, nok) {
    const previous = this.previous;
    const events = this.events;
    let size = 0;
    return start;
    function start(code) {
      if (previous === 126 && events[events.length - 1][1].type !== "characterEscape") {
        return nok(code);
      }
      effects.enter('strikethroughSequenceTemporary');
      return more(code);
    }
    function more(code) {
      const before = classifyCharacter(previous);
      if (code === 126) {
        if (size > 1) return nok(code);
        effects.consume(code);
        size++;
        return more;
      }
      if (size < 2 && !single) return nok(code);
      const token = effects.exit('strikethroughSequenceTemporary');
      const after = classifyCharacter(code);
      token._open = !after || after === 2 && Boolean(before);
      token._close = !before || before === 2 && Boolean(after);
      return ok(code);
    }
  }
}

class EditMap {
  constructor() {
    this.map = [];
  }
  add(index, remove, add) {
    addImplementation(this, index, remove, add);
  }
  consume(events) {
    this.map.sort(function (a, b) {
      return a[0] - b[0];
    });
    if (this.map.length === 0) {
      return;
    }
    let index = this.map.length;
    const vecs = [];
    while (index > 0) {
      index -= 1;
      vecs.push(events.slice(this.map[index][0] + this.map[index][1]), this.map[index][2]);
      events.length = this.map[index][0];
    }
    vecs.push([...events]);
    events.length = 0;
    let slice = vecs.pop();
    while (slice) {
      events.push(...slice);
      slice = vecs.pop();
    }
    this.map.length = 0;
  }
}
function addImplementation(editMap, at, remove, add) {
  let index = 0;
  if (remove === 0 && add.length === 0) {
    return;
  }
  while (index < editMap.map.length) {
    if (editMap.map[index][0] === at) {
      editMap.map[index][1] += remove;
      editMap.map[index][2].push(...add);
      return;
    }
    index += 1;
  }
  editMap.map.push([at, remove, add]);
}

function gfmTableAlign(events, index) {
  let inDelimiterRow = false;
  const align = [];
  while (index < events.length) {
    const event = events[index];
    if (inDelimiterRow) {
      if (event[0] === 'enter') {
        if (event[1].type === 'tableContent') {
          align.push(events[index + 1][1].type === 'tableDelimiterMarker' ? 'left' : 'none');
        }
      }
      else if (event[1].type === 'tableContent') {
        if (events[index - 1][1].type === 'tableDelimiterMarker') {
          const alignIndex = align.length - 1;
          align[alignIndex] = align[alignIndex] === 'left' ? 'center' : 'right';
        }
      }
      else if (event[1].type === 'tableDelimiterRow') {
        break;
      }
    } else if (event[0] === 'enter' && event[1].type === 'tableDelimiterRow') {
      inDelimiterRow = true;
    }
    index += 1;
  }
  return align;
}

function gfmTable() {
  return {
    flow: {
      null: {
        name: 'table',
        tokenize: tokenizeTable,
        resolveAll: resolveTable
      }
    }
  };
}
function tokenizeTable(effects, ok, nok) {
  const self = this;
  let size = 0;
  let sizeB = 0;
  let seen;
  return start;
  function start(code) {
    let index = self.events.length - 1;
    while (index > -1) {
      const type = self.events[index][1].type;
      if (type === "lineEnding" ||
      type === "linePrefix") index--;else break;
    }
    const tail = index > -1 ? self.events[index][1].type : null;
    const next = tail === 'tableHead' || tail === 'tableRow' ? bodyRowStart : headRowBefore;
    if (next === bodyRowStart && self.parser.lazy[self.now().line]) {
      return nok(code);
    }
    return next(code);
  }
  function headRowBefore(code) {
    effects.enter('tableHead');
    effects.enter('tableRow');
    return headRowStart(code);
  }
  function headRowStart(code) {
    if (code === 124) {
      return headRowBreak(code);
    }
    seen = true;
    sizeB += 1;
    return headRowBreak(code);
  }
  function headRowBreak(code) {
    if (code === null) {
      return nok(code);
    }
    if (markdownLineEnding(code)) {
      if (sizeB > 1) {
        sizeB = 0;
        self.interrupt = true;
        effects.exit('tableRow');
        effects.enter("lineEnding");
        effects.consume(code);
        effects.exit("lineEnding");
        return headDelimiterStart;
      }
      return nok(code);
    }
    if (markdownSpace(code)) {
      return factorySpace(effects, headRowBreak, "whitespace")(code);
    }
    sizeB += 1;
    if (seen) {
      seen = false;
      size += 1;
    }
    if (code === 124) {
      effects.enter('tableCellDivider');
      effects.consume(code);
      effects.exit('tableCellDivider');
      seen = true;
      return headRowBreak;
    }
    effects.enter("data");
    return headRowData(code);
  }
  function headRowData(code) {
    if (code === null || code === 124 || markdownLineEndingOrSpace(code)) {
      effects.exit("data");
      return headRowBreak(code);
    }
    effects.consume(code);
    return code === 92 ? headRowEscape : headRowData;
  }
  function headRowEscape(code) {
    if (code === 92 || code === 124) {
      effects.consume(code);
      return headRowData;
    }
    return headRowData(code);
  }
  function headDelimiterStart(code) {
    self.interrupt = false;
    if (self.parser.lazy[self.now().line]) {
      return nok(code);
    }
    effects.enter('tableDelimiterRow');
    seen = false;
    if (markdownSpace(code)) {
      return factorySpace(effects, headDelimiterBefore, "linePrefix", self.parser.constructs.disable.null.includes('codeIndented') ? undefined : 4)(code);
    }
    return headDelimiterBefore(code);
  }
  function headDelimiterBefore(code) {
    if (code === 45 || code === 58) {
      return headDelimiterValueBefore(code);
    }
    if (code === 124) {
      seen = true;
      effects.enter('tableCellDivider');
      effects.consume(code);
      effects.exit('tableCellDivider');
      return headDelimiterCellBefore;
    }
    return headDelimiterNok(code);
  }
  function headDelimiterCellBefore(code) {
    if (markdownSpace(code)) {
      return factorySpace(effects, headDelimiterValueBefore, "whitespace")(code);
    }
    return headDelimiterValueBefore(code);
  }
  function headDelimiterValueBefore(code) {
    if (code === 58) {
      sizeB += 1;
      seen = true;
      effects.enter('tableDelimiterMarker');
      effects.consume(code);
      effects.exit('tableDelimiterMarker');
      return headDelimiterLeftAlignmentAfter;
    }
    if (code === 45) {
      sizeB += 1;
      return headDelimiterLeftAlignmentAfter(code);
    }
    if (code === null || markdownLineEnding(code)) {
      return headDelimiterCellAfter(code);
    }
    return headDelimiterNok(code);
  }
  function headDelimiterLeftAlignmentAfter(code) {
    if (code === 45) {
      effects.enter('tableDelimiterFiller');
      return headDelimiterFiller(code);
    }
    return headDelimiterNok(code);
  }
  function headDelimiterFiller(code) {
    if (code === 45) {
      effects.consume(code);
      return headDelimiterFiller;
    }
    if (code === 58) {
      seen = true;
      effects.exit('tableDelimiterFiller');
      effects.enter('tableDelimiterMarker');
      effects.consume(code);
      effects.exit('tableDelimiterMarker');
      return headDelimiterRightAlignmentAfter;
    }
    effects.exit('tableDelimiterFiller');
    return headDelimiterRightAlignmentAfter(code);
  }
  function headDelimiterRightAlignmentAfter(code) {
    if (markdownSpace(code)) {
      return factorySpace(effects, headDelimiterCellAfter, "whitespace")(code);
    }
    return headDelimiterCellAfter(code);
  }
  function headDelimiterCellAfter(code) {
    if (code === 124) {
      return headDelimiterBefore(code);
    }
    if (code === null || markdownLineEnding(code)) {
      if (!seen || size !== sizeB) {
        return headDelimiterNok(code);
      }
      effects.exit('tableDelimiterRow');
      effects.exit('tableHead');
      return ok(code);
    }
    return headDelimiterNok(code);
  }
  function headDelimiterNok(code) {
    return nok(code);
  }
  function bodyRowStart(code) {
    effects.enter('tableRow');
    return bodyRowBreak(code);
  }
  function bodyRowBreak(code) {
    if (code === 124) {
      effects.enter('tableCellDivider');
      effects.consume(code);
      effects.exit('tableCellDivider');
      return bodyRowBreak;
    }
    if (code === null || markdownLineEnding(code)) {
      effects.exit('tableRow');
      return ok(code);
    }
    if (markdownSpace(code)) {
      return factorySpace(effects, bodyRowBreak, "whitespace")(code);
    }
    effects.enter("data");
    return bodyRowData(code);
  }
  function bodyRowData(code) {
    if (code === null || code === 124 || markdownLineEndingOrSpace(code)) {
      effects.exit("data");
      return bodyRowBreak(code);
    }
    effects.consume(code);
    return code === 92 ? bodyRowEscape : bodyRowData;
  }
  function bodyRowEscape(code) {
    if (code === 92 || code === 124) {
      effects.consume(code);
      return bodyRowData;
    }
    return bodyRowData(code);
  }
}
function resolveTable(events, context) {
  let index = -1;
  let inFirstCellAwaitingPipe = true;
  let rowKind = 0;
  let lastCell = [0, 0, 0, 0];
  let cell = [0, 0, 0, 0];
  let afterHeadAwaitingFirstBodyRow = false;
  let lastTableEnd = 0;
  let currentTable;
  let currentBody;
  let currentCell;
  const map = new EditMap();
  while (++index < events.length) {
    const event = events[index];
    const token = event[1];
    if (event[0] === 'enter') {
      if (token.type === 'tableHead') {
        afterHeadAwaitingFirstBodyRow = false;
        if (lastTableEnd !== 0) {
          flushTableEnd(map, context, lastTableEnd, currentTable, currentBody);
          currentBody = undefined;
          lastTableEnd = 0;
        }
        currentTable = {
          type: 'table',
          start: Object.assign({}, token.start),
          end: Object.assign({}, token.end)
        };
        map.add(index, 0, [['enter', currentTable, context]]);
      } else if (token.type === 'tableRow' || token.type === 'tableDelimiterRow') {
        inFirstCellAwaitingPipe = true;
        currentCell = undefined;
        lastCell = [0, 0, 0, 0];
        cell = [0, index + 1, 0, 0];
        if (afterHeadAwaitingFirstBodyRow) {
          afterHeadAwaitingFirstBodyRow = false;
          currentBody = {
            type: 'tableBody',
            start: Object.assign({}, token.start),
            end: Object.assign({}, token.end)
          };
          map.add(index, 0, [['enter', currentBody, context]]);
        }
        rowKind = token.type === 'tableDelimiterRow' ? 2 : currentBody ? 3 : 1;
      }
      else if (rowKind && (token.type === "data" || token.type === 'tableDelimiterMarker' || token.type === 'tableDelimiterFiller')) {
        inFirstCellAwaitingPipe = false;
        if (cell[2] === 0) {
          if (lastCell[1] !== 0) {
            cell[0] = cell[1];
            currentCell = flushCell(map, context, lastCell, rowKind, undefined, currentCell);
            lastCell = [0, 0, 0, 0];
          }
          cell[2] = index;
        }
      } else if (token.type === 'tableCellDivider') {
        if (inFirstCellAwaitingPipe) {
          inFirstCellAwaitingPipe = false;
        } else {
          if (lastCell[1] !== 0) {
            cell[0] = cell[1];
            currentCell = flushCell(map, context, lastCell, rowKind, undefined, currentCell);
          }
          lastCell = cell;
          cell = [lastCell[1], index, 0, 0];
        }
      }
    }
    else if (token.type === 'tableHead') {
      afterHeadAwaitingFirstBodyRow = true;
      lastTableEnd = index;
    } else if (token.type === 'tableRow' || token.type === 'tableDelimiterRow') {
      lastTableEnd = index;
      if (lastCell[1] !== 0) {
        cell[0] = cell[1];
        currentCell = flushCell(map, context, lastCell, rowKind, index, currentCell);
      } else if (cell[1] !== 0) {
        currentCell = flushCell(map, context, cell, rowKind, index, currentCell);
      }
      rowKind = 0;
    } else if (rowKind && (token.type === "data" || token.type === 'tableDelimiterMarker' || token.type === 'tableDelimiterFiller')) {
      cell[3] = index;
    }
  }
  if (lastTableEnd !== 0) {
    flushTableEnd(map, context, lastTableEnd, currentTable, currentBody);
  }
  map.consume(context.events);
  index = -1;
  while (++index < context.events.length) {
    const event = context.events[index];
    if (event[0] === 'enter' && event[1].type === 'table') {
      event[1]._align = gfmTableAlign(context.events, index);
    }
  }
  return events;
}
function flushCell(map, context, range, rowKind, rowEnd, previousCell) {
  const groupName = rowKind === 1 ? 'tableHeader' : rowKind === 2 ? 'tableDelimiter' : 'tableData';
  const valueName = 'tableContent';
  if (range[0] !== 0) {
    previousCell.end = Object.assign({}, getPoint(context.events, range[0]));
    map.add(range[0], 0, [['exit', previousCell, context]]);
  }
  const now = getPoint(context.events, range[1]);
  previousCell = {
    type: groupName,
    start: Object.assign({}, now),
    end: Object.assign({}, now)
  };
  map.add(range[1], 0, [['enter', previousCell, context]]);
  if (range[2] !== 0) {
    const relatedStart = getPoint(context.events, range[2]);
    const relatedEnd = getPoint(context.events, range[3]);
    const valueToken = {
      type: valueName,
      start: Object.assign({}, relatedStart),
      end: Object.assign({}, relatedEnd)
    };
    map.add(range[2], 0, [['enter', valueToken, context]]);
    if (rowKind !== 2) {
      const start = context.events[range[2]];
      const end = context.events[range[3]];
      start[1].end = Object.assign({}, end[1].end);
      start[1].type = "chunkText";
      start[1].contentType = "text";
      if (range[3] > range[2] + 1) {
        const a = range[2] + 1;
        const b = range[3] - range[2] - 1;
        map.add(a, b, []);
      }
    }
    map.add(range[3] + 1, 0, [['exit', valueToken, context]]);
  }
  if (rowEnd !== undefined) {
    previousCell.end = Object.assign({}, getPoint(context.events, rowEnd));
    map.add(rowEnd, 0, [['exit', previousCell, context]]);
    previousCell = undefined;
  }
  return previousCell;
}
function flushTableEnd(map, context, index, table, tableBody) {
  const exits = [];
  const related = getPoint(context.events, index);
  if (tableBody) {
    tableBody.end = Object.assign({}, related);
    exits.push(['exit', tableBody, context]);
  }
  table.end = Object.assign({}, related);
  exits.push(['exit', table, context]);
  map.add(index + 1, 0, exits);
}
function getPoint(events, index) {
  const event = events[index];
  const side = event[0] === 'enter' ? 'start' : 'end';
  return event[1][side];
}

const tasklistCheck = {
  name: 'tasklistCheck',
  tokenize: tokenizeTasklistCheck
};
function gfmTaskListItem() {
  return {
    text: {
      [91]: tasklistCheck
    }
  };
}
function tokenizeTasklistCheck(effects, ok, nok) {
  const self = this;
  return open;
  function open(code) {
    if (
    self.previous !== null ||
    !self._gfmTasklistFirstContentOfListItem) {
      return nok(code);
    }
    effects.enter('taskListCheck');
    effects.enter('taskListCheckMarker');
    effects.consume(code);
    effects.exit('taskListCheckMarker');
    return inside;
  }
  function inside(code) {
    if (markdownLineEndingOrSpace(code)) {
      effects.enter('taskListCheckValueUnchecked');
      effects.consume(code);
      effects.exit('taskListCheckValueUnchecked');
      return close;
    }
    if (code === 88 || code === 120) {
      effects.enter('taskListCheckValueChecked');
      effects.consume(code);
      effects.exit('taskListCheckValueChecked');
      return close;
    }
    return nok(code);
  }
  function close(code) {
    if (code === 93) {
      effects.enter('taskListCheckMarker');
      effects.consume(code);
      effects.exit('taskListCheckMarker');
      effects.exit('taskListCheck');
      return after;
    }
    return nok(code);
  }
  function after(code) {
    if (markdownLineEnding(code)) {
      return ok(code);
    }
    if (markdownSpace(code)) {
      return effects.check({
        tokenize: spaceThenNonSpace
      }, ok, nok)(code);
    }
    return nok(code);
  }
}
function spaceThenNonSpace(effects, ok, nok) {
  return factorySpace(effects, after, "whitespace");
  function after(code) {
    return code === null ? nok(code) : ok(code);
  }
}

function gfm(options) {
  return combineExtensions([
    gfmAutolinkLiteral(),
    gfmFootnote(),
    gfmStrikethrough(options),
    gfmTable(),
    gfmTaskListItem()
  ])
}

const emptyOptions$1 = {};
function remarkGfm(options) {
  const self =  (this);
  const settings = options || emptyOptions$1;
  const data = self.data();
  const micromarkExtensions =
    data.micromarkExtensions || (data.micromarkExtensions = []);
  const fromMarkdownExtensions =
    data.fromMarkdownExtensions || (data.fromMarkdownExtensions = []);
  const toMarkdownExtensions =
    data.toMarkdownExtensions || (data.toMarkdownExtensions = []);
  micromarkExtensions.push(gfm(settings));
  fromMarkdownExtensions.push(gfmFromMarkdown());
  toMarkdownExtensions.push(gfmToMarkdown(settings));
}

const commentExpression = /\s*([a-zA-Z\d-]+)(\s+([\s\S]*))?\s*/;
const esCommentExpression = new RegExp(
  '(\\s*\\/\\*' + commentExpression.source + '\\*\\/\\s*)'
);
const markerExpression = new RegExp(
  '(\\s*<!--' + commentExpression.source + '-->\\s*)'
);
function commentMarker(value) {
  if (
    isNode(value) &&
    (value.type === 'html' ||
      value.type === 'mdxFlowExpression' ||
      value.type === 'mdxTextExpression')
  ) {
    const match = value.value.match(
      value.type === 'html' ? markerExpression : esCommentExpression
    );
    if (match && match[0].length === value.value.length) {
      const parameters = parseParameters(match[3] || '');
      if (parameters) {
        return {
          name: match[2],
          attributes: (match[4] || '').trim(),
          parameters,
          node: value
        }
      }
    }
  }
}
function parseParameters(value) {
  const parameters = {};
  return value
    .replace(
      /\s+([-\w]+)(?:=(?:"((?:\\[\s\S]|[^"])*)"|'((?:\\[\s\S]|[^'])*)'|((?:\\[\s\S]|[^"'\s])+)))?/gi,
      replacer
    )
    .replace(/\s+/g, '')
    ? undefined
    : parameters
  function replacer(_, $1, $2, $3, $4) {
    let value = $2 === undefined ? ($3 === undefined ? $4 : $3) : $2;
    const number = Number(value);
    if (value === 'true' || value === undefined) {
      value = true;
    } else if (value === 'false') {
      value = false;
    } else if (value.trim() && !Number.isNaN(number)) {
      value = number;
    }
    parameters[$1] = value;
    return ''
  }
}
function isNode(value) {
  return Boolean(value && typeof value === 'object' && 'type' in value)
}

function parse$1(value) {
  const input = String(value || '').trim();
  return input ? input.split(/[ \t\n\r\f]+/g) : []
}

const search = /\r?\n|\r/g;
function location(file) {
  const value = String(file);
  const indices = [];
  search.lastIndex = 0;
  while (search.test(value)) {
    indices.push(search.lastIndex);
  }
  indices.push(value.length + 1);
  return {toPoint, toOffset}
  function toPoint(offset) {
    let index = -1;
    if (
      typeof offset === 'number' &&
      offset > -1 &&
      offset < indices[indices.length - 1]
    ) {
      while (++index < indices.length) {
        if (indices[index] > offset) {
          return {
            line: index + 1,
            column: offset - (index > 0 ? indices[index - 1] : 0) + 1,
            offset
          }
        }
      }
    }
  }
  function toOffset(point) {
    const line = point && point.line;
    const column = point && point.column;
    if (
      typeof line === 'number' &&
      typeof column === 'number' &&
      !Number.isNaN(line) &&
      !Number.isNaN(column) &&
      line - 1 in indices
    ) {
      const offset = (indices[line - 2] || 0) + column - 1 || 0;
      if (offset > -1 && offset < indices[indices.length - 1]) {
        return offset
      }
    }
  }
}

const own = {}.hasOwnProperty;
function messageControl(tree, options) {
  if (!options || typeof options !== 'object') {
    throw new Error('Expected `options`')
  }
  const {file, marker, name, test} = options;
  let {enable, disable, known, reset, source} = options;
  if (!enable) enable = [];
  if (!disable) disable = [];
  if (!file) {
    throw new Error('Expected `file` in `options`')
  }
  if (!marker) {
    throw new Error('Expected `marker` in `options`')
  }
  if (!name) {
    throw new Error('Expected `name` in `options`')
  }
  const sources = typeof source === 'string' ? [source] : source || [name];
  const toOffset = location(file).toOffset;
  const initial = !reset;
  const gaps = detectGaps(tree);
  const scope = {};
  const globals = [];
  visit(tree, test, visitor);
  file.messages = file.messages.filter(function (m) {
    return filter(m)
  });
  function visitor(node, position, parent) {
    const point = node.position && node.position.start;
    const mark = marker(node);
    if (!point || !mark || mark.name !== name) {
      return
    }
    const ruleIds = parse$1(mark.attributes);
    const verb = ruleIds.shift();
    const fn =
      verb === 'enable'
        ? doEnable
        : verb === 'disable'
        ? doDisable
        : verb === 'ignore'
        ? doIgnore
        : undefined;
    if (!fn) {
      file.fail(
        'Unknown keyword `' +
          verb +
          '`: expected ' +
          "`'enable'`, `'disable'`, or `'ignore'`",
        node
      );
    }
    const next =
      (parent && position !== undefined && parent.children[position + 1]) ||
      undefined;
    const tail = next && next.position && next.position.end;
    if (ruleIds.length === 0) {
      fn(point, undefined, tail);
    } else {
      let index = -1;
      while (++index < ruleIds.length) {
        const ruleId = ruleIds[index];
        if (isKnown(ruleId, verb, node)) {
          fn(point, ruleId, tail);
        }
      }
    }
  }
  function doIgnore(point, ruleId, tail) {
    if (tail) {
      toggle(point, false, ruleId);
      toggle(tail, true, ruleId);
    }
  }
  function doDisable(point, ruleId) {
    toggle(point, false, ruleId);
    if (!ruleId) reset = true;
  }
  function doEnable(point, ruleId) {
    toggle(point, true, ruleId);
    if (!ruleId) reset = false;
  }
  function filter(message) {
    let gapIndex = gaps.length;
    if (!message.source || !sources.includes(message.source)) {
      return true
    }
    if (!message.line) message.line = 1;
    if (!message.column) message.column = 1;
    const offset = toOffset(message);
    while (gapIndex--) {
      if (gaps[gapIndex][0] <= offset && gaps[gapIndex][1] > offset) {
        return false
      }
    }
    return (
      (!message.ruleId || check(message, scope[message.ruleId], true)) &&
      check(message, globals, false)
    )
  }
  function isKnown(ruleId, verb, node) {
    const result = known ? known.includes(ruleId) : true;
    if (!result) {
      file.message('Cannot ' + verb + " `'" + ruleId + "'`, it’s not known", {
        ancestors: [node],
        place: node.position,
        ruleId: 'known',
        source: 'unified-message-control'
      });
    }
    return result
  }
  function getState(ruleId) {
    const ranges = ruleId ? scope[ruleId] : globals;
    if (ranges && ranges.length > 0) {
      return ranges[ranges.length - 1].state
    }
    return ruleId
      ? reset
        ? enable.includes(ruleId)
        : !disable.includes(ruleId)
      : !reset
  }
  function toggle(point, state, ruleId) {
    const markers = ruleId ? scope[ruleId] || (scope[ruleId] = []) : globals;
    const current = getState(ruleId);
    if (current !== state) {
      markers.push({state, point});
    }
    if (!ruleId) {
      for (ruleId in scope) {
        if (own.call(scope, ruleId)) {
          toggle(point, state, ruleId);
        }
      }
    }
  }
  function check(message, marks, local) {
    if (message.line && message.column && marks && marks.length > 0) {
      let index = marks.length;
      while (index--) {
        const mark = marks[index];
        if (
          mark.point &&
          (mark.point.line < message.line ||
            (mark.point.line === message.line &&
              mark.point.column <= message.column))
        ) {
          return mark.state === true
        }
      }
    }
    if (local) {
      ok$1(message.ruleId);
      return reset
        ? enable.includes(message.ruleId)
        : !disable.includes(message.ruleId)
    }
    return Boolean(initial || reset)
  }
}
function detectGaps(tree) {
  const end =
    tree && tree.position && tree.position.end && tree.position.end.offset;
  let offset = 0;
  let gap = false;
  const gaps = [];
  visit(tree, one);
  if (typeof end === 'number' && offset !== end) {
    update();
    update(end);
  }
  return gaps
  function one(node) {
    update(node.position && node.position.start && node.position.start.offset);
    if (!('children' in node)) {
      update(node.position && node.position.end && node.position.end.offset);
    }
  }
  function update(latest) {
    if (latest === null || latest === undefined) {
      gap = true;
    } else if (offset < latest) {
      if (gap) {
        gaps.push([offset, latest]);
        gap = false;
      }
      offset = latest;
    }
  }
}

const test = [
  'comment',
  'html',
  'mdxFlowExpression',
  'mdxTextExpression'
];
function remarkMessageControl(options) {
  return function (tree, file) {
    messageControl(tree, {...options, file, marker: commentMarker, test});
  }
}

function remarkLint() {
  this.use(lintMessageControl);
}
function lintMessageControl() {
  return remarkMessageControl({name: 'lint', source: 'remark-lint'})
}

function lintRule$1(meta, rule) {
  const id = typeof meta === 'string' ? meta : meta.origin;
  const url = typeof meta === 'string' ? undefined : meta.url;
  const parts = id.split(':');
  const source = parts[1] ? parts[0] : undefined;
  const ruleId = parts[1];
  Object.defineProperty(plugin, 'name', {value: id});
  return plugin
  function plugin(config) {
    const [severity, options] = coerce$2(ruleId, config);
    const fatal = severity === 2;
    if (!severity) return
    return function (tree, file, next) {
      let index = file.messages.length - 1;
      wrap(rule, function (error) {
        const messages = file.messages;
        if (error && !messages.includes(error)) {
          try {
            file.fail(error);
          } catch {}
        }
        while (++index < messages.length) {
          Object.assign(messages[index], {fatal, ruleId, source, url});
        }
        next();
      })(tree, file, options);
    }
  }
}
function coerce$2(name, config) {
  if (!Array.isArray(config)) {
    return [1, config]
  }
  const [severity, ...options] = config;
  switch (severity) {
    case false:
    case 0:
    case 'off': {
      return [0, ...options]
    }
    case true:
    case 1:
    case 'on':
    case 'warn': {
      return [1, ...options]
    }
    case 2:
    case 'error': {
      return [2, ...options]
    }
    default: {
      if (typeof severity !== 'number') {
        return [1, config]
      }
      throw new Error(
        'Incorrect severity `' +
          severity +
          '` for `' +
          name +
          '`, ' +
          'expected 0, 1, or 2'
      )
    }
  }
}

/**
 * remark-lint rule to warn when a final line ending is missing.
 *
 * ## What is this?
 *
 * This package checks the final line ending.
 *
 * ## When should I use this?
 *
 * You can use this package to check final line endings.
 *
 * ## API
 *
 * ### `unified().use(remarkLintFinalNewline)`
 *
 * Warn when a final line ending is missing.
 *
 * ###### Parameters
 *
 * There are no options.
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ## Recommendation
 *
 * Turn this rule on.
 * See [StackExchange][] for more info.
 *
 * ## Fix
 *
 * [`remark-stringify`](https://github.com/remarkjs/remark/tree/main/packages/remark-stringify)
 * always adds final line endings.
 *
 * [api-remark-lint-final-newline]: #unifieduseremarklintfinalnewline
 * [github-remark-stringify]: https://github.com/remarkjs/remark/tree/main/packages/remark-stringify
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 * [stackexchange]: https://unix.stackexchange.com/questions/18743
 *
 * ## Examples
 *
 * ##### `ok.md`
 *
 * ###### In
 *
 * ```markdown
 * Mercury␊
 * ```
 *
 * ###### Out
 *
 * No messages.
 *
 * ##### `not-ok.md`
 *
 * ###### In
 *
 * ```markdown
 * Mercury␀
 * ```
 *
 * ###### Out
 *
 * ```text
 * 1:8: Unexpected missing final newline character, expected line feed (`\n`) at end of file
 * ```
 *
 * @module final-newline
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 */
const remarkLintFinalNewline = lintRule$1(
  {
    origin: 'remark-lint:final-newline',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-final-newline#readme'
  },
  function (_, file) {
    const value = String(file);
    const end = location(file).toPoint(value.length);
    const last = value.length - 1;
    if (
      last !== -1 &&
      value.charAt(last) !== '\n'
    ) {
      file.message(
        'Unexpected missing final newline character, expected line feed (`\\n`) at end of file',
        end
      );
    }
  }
);

const pointEnd = point('end');
const pointStart = point('start');
function point(type) {
  return point
  function point(node) {
    const point = (node && node.position && node.position[type]) || {};
    if (
      typeof point.line === 'number' &&
      point.line > 0 &&
      typeof point.column === 'number' &&
      point.column > 0
    ) {
      return {
        line: point.line,
        column: point.column,
        offset:
          typeof point.offset === 'number' && point.offset > -1
            ? point.offset
            : undefined
      }
    }
  }
}
function position(node) {
  const start = pointStart(node);
  const end = pointEnd(node);
  if (start && end) {
    return {start, end}
  }
}

/**
 * remark-lint rule to warn when more spaces are used than needed
 * for hard breaks.
 *
 * ## What is this?
 *
 * This package checks the whitespace of hard breaks.
 *
 * ## When should I use this?
 *
 * You can use this package to check that the number of spaces in hard breaks
 * are consistent.
 *
 * ## API
 *
 * ### `unified().use(remarkLintHardBreakSpaces)`
 *
 * Warn when more spaces are used than needed for hard breaks.
 *
 * ###### Parameters
 *
 * There are no options.
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ## Recommendation
 *
 * Less than two spaces do not create a hard breaks and more than two spaces
 * have no effect.
 * Due to this, it’s recommended to turn this rule on.
 *
 * [api-remark-lint-hard-break-spaces]: #unifieduseremarklinthardbreakspaces
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module hard-break-spaces
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 * @example
 *   {"name": "ok.md"}
 *
 *   **Mercury** is the first planet from the Sun␠␠
 *   and the smallest in the Solar System.
 *
 * @example
 *   {"label": "input", "name": "not-ok.md"}
 *
 *   **Mercury** is the first planet from the Sun␠␠␠
 *   and the smallest in the Solar System.
 * @example
 *   {"label": "output", "name": "not-ok.md"}
 *
 *   1:45-2:1: Unexpected `3` spaces for hard break, expected `2` spaces
 *
 * @example
 *   {"gfm": true, "label": "input", "name": "containers.md"}
 *
 *   [^mercury]:
 *       > * > * **Mercury** is the first planet from the Sun␠␠␠
 *       >   >   and the smallest in the Solar System.
 * @example
 *   {"gfm": true, "label": "output", "name": "containers.md"}
 *
 *   2:57-3:1: Unexpected `3` spaces for hard break, expected `2` spaces
 */
const remarkLintHardBreakSpaces = lintRule$1(
  {
    origin: 'remark-lint:hard-break-spaces',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-hard-break-spaces#readme'
  },
  function (tree, file) {
    const value = String(file);
    visit(tree, 'break', function (node) {
      const end = pointEnd(node);
      const start = pointStart(node);
      if (
        end &&
        start &&
        typeof end.offset === 'number' &&
        typeof start.offset === 'number'
      ) {
        const slice = value.slice(start.offset, end.offset);
        let actual = 0;
        while (slice.charCodeAt(actual) === 32) actual++;
        if (actual > 2) {
          file.message(
            'Unexpected `' +
              actual +
              '` spaces for hard break, expected `2` spaces',
            node
          );
        }
      }
    });
  }
);

function commonjsRequire(path) {
	throw new Error('Could not dynamically require "' + path + '". Please configure the dynamicRequireTargets or/and ignoreDynamicRequires option of @rollup/plugin-commonjs appropriately for this require call to work.');
}

var pluralize$1 = {exports: {}};

(function (module, exports) {
	(function (root, pluralize) {
	  if (typeof commonjsRequire === 'function' && 'object' === 'object' && 'object' === 'object') {
	    module.exports = pluralize();
	  } else {
	    root.pluralize = pluralize();
	  }
	})(commonjsGlobal, function () {
	  var pluralRules = [];
	  var singularRules = [];
	  var uncountables = {};
	  var irregularPlurals = {};
	  var irregularSingles = {};
	  function sanitizeRule (rule) {
	    if (typeof rule === 'string') {
	      return new RegExp('^' + rule + '$', 'i');
	    }
	    return rule;
	  }
	  function restoreCase (word, token) {
	    if (word === token) return token;
	    if (word === word.toLowerCase()) return token.toLowerCase();
	    if (word === word.toUpperCase()) return token.toUpperCase();
	    if (word[0] === word[0].toUpperCase()) {
	      return token.charAt(0).toUpperCase() + token.substr(1).toLowerCase();
	    }
	    return token.toLowerCase();
	  }
	  function interpolate (str, args) {
	    return str.replace(/\$(\d{1,2})/g, function (match, index) {
	      return args[index] || '';
	    });
	  }
	  function replace (word, rule) {
	    return word.replace(rule[0], function (match, index) {
	      var result = interpolate(rule[1], arguments);
	      if (match === '') {
	        return restoreCase(word[index - 1], result);
	      }
	      return restoreCase(match, result);
	    });
	  }
	  function sanitizeWord (token, word, rules) {
	    if (!token.length || uncountables.hasOwnProperty(token)) {
	      return word;
	    }
	    var len = rules.length;
	    while (len--) {
	      var rule = rules[len];
	      if (rule[0].test(word)) return replace(word, rule);
	    }
	    return word;
	  }
	  function replaceWord (replaceMap, keepMap, rules) {
	    return function (word) {
	      var token = word.toLowerCase();
	      if (keepMap.hasOwnProperty(token)) {
	        return restoreCase(word, token);
	      }
	      if (replaceMap.hasOwnProperty(token)) {
	        return restoreCase(word, replaceMap[token]);
	      }
	      return sanitizeWord(token, word, rules);
	    };
	  }
	  function checkWord (replaceMap, keepMap, rules, bool) {
	    return function (word) {
	      var token = word.toLowerCase();
	      if (keepMap.hasOwnProperty(token)) return true;
	      if (replaceMap.hasOwnProperty(token)) return false;
	      return sanitizeWord(token, token, rules) === token;
	    };
	  }
	  function pluralize (word, count, inclusive) {
	    var pluralized = count === 1
	      ? pluralize.singular(word) : pluralize.plural(word);
	    return (inclusive ? count + ' ' : '') + pluralized;
	  }
	  pluralize.plural = replaceWord(
	    irregularSingles, irregularPlurals, pluralRules
	  );
	  pluralize.isPlural = checkWord(
	    irregularSingles, irregularPlurals, pluralRules
	  );
	  pluralize.singular = replaceWord(
	    irregularPlurals, irregularSingles, singularRules
	  );
	  pluralize.isSingular = checkWord(
	    irregularPlurals, irregularSingles, singularRules
	  );
	  pluralize.addPluralRule = function (rule, replacement) {
	    pluralRules.push([sanitizeRule(rule), replacement]);
	  };
	  pluralize.addSingularRule = function (rule, replacement) {
	    singularRules.push([sanitizeRule(rule), replacement]);
	  };
	  pluralize.addUncountableRule = function (word) {
	    if (typeof word === 'string') {
	      uncountables[word.toLowerCase()] = true;
	      return;
	    }
	    pluralize.addPluralRule(word, '$0');
	    pluralize.addSingularRule(word, '$0');
	  };
	  pluralize.addIrregularRule = function (single, plural) {
	    plural = plural.toLowerCase();
	    single = single.toLowerCase();
	    irregularSingles[single] = plural;
	    irregularPlurals[plural] = single;
	  };
	  [
	    ['I', 'we'],
	    ['me', 'us'],
	    ['he', 'they'],
	    ['she', 'they'],
	    ['them', 'them'],
	    ['myself', 'ourselves'],
	    ['yourself', 'yourselves'],
	    ['itself', 'themselves'],
	    ['herself', 'themselves'],
	    ['himself', 'themselves'],
	    ['themself', 'themselves'],
	    ['is', 'are'],
	    ['was', 'were'],
	    ['has', 'have'],
	    ['this', 'these'],
	    ['that', 'those'],
	    ['echo', 'echoes'],
	    ['dingo', 'dingoes'],
	    ['volcano', 'volcanoes'],
	    ['tornado', 'tornadoes'],
	    ['torpedo', 'torpedoes'],
	    ['genus', 'genera'],
	    ['viscus', 'viscera'],
	    ['stigma', 'stigmata'],
	    ['stoma', 'stomata'],
	    ['dogma', 'dogmata'],
	    ['lemma', 'lemmata'],
	    ['schema', 'schemata'],
	    ['anathema', 'anathemata'],
	    ['ox', 'oxen'],
	    ['axe', 'axes'],
	    ['die', 'dice'],
	    ['yes', 'yeses'],
	    ['foot', 'feet'],
	    ['eave', 'eaves'],
	    ['goose', 'geese'],
	    ['tooth', 'teeth'],
	    ['quiz', 'quizzes'],
	    ['human', 'humans'],
	    ['proof', 'proofs'],
	    ['carve', 'carves'],
	    ['valve', 'valves'],
	    ['looey', 'looies'],
	    ['thief', 'thieves'],
	    ['groove', 'grooves'],
	    ['pickaxe', 'pickaxes'],
	    ['passerby', 'passersby']
	  ].forEach(function (rule) {
	    return pluralize.addIrregularRule(rule[0], rule[1]);
	  });
	  [
	    [/s?$/i, 's'],
	    [/[^\u0000-\u007F]$/i, '$0'],
	    [/([^aeiou]ese)$/i, '$1'],
	    [/(ax|test)is$/i, '$1es'],
	    [/(alias|[^aou]us|t[lm]as|gas|ris)$/i, '$1es'],
	    [/(e[mn]u)s?$/i, '$1s'],
	    [/([^l]ias|[aeiou]las|[ejzr]as|[iu]am)$/i, '$1'],
	    [/(alumn|syllab|vir|radi|nucle|fung|cact|stimul|termin|bacill|foc|uter|loc|strat)(?:us|i)$/i, '$1i'],
	    [/(alumn|alg|vertebr)(?:a|ae)$/i, '$1ae'],
	    [/(seraph|cherub)(?:im)?$/i, '$1im'],
	    [/(her|at|gr)o$/i, '$1oes'],
	    [/(agend|addend|millenni|dat|extrem|bacteri|desiderat|strat|candelabr|errat|ov|symposi|curricul|automat|quor)(?:a|um)$/i, '$1a'],
	    [/(apheli|hyperbat|periheli|asyndet|noumen|phenomen|criteri|organ|prolegomen|hedr|automat)(?:a|on)$/i, '$1a'],
	    [/sis$/i, 'ses'],
	    [/(?:(kni|wi|li)fe|(ar|l|ea|eo|oa|hoo)f)$/i, '$1$2ves'],
	    [/([^aeiouy]|qu)y$/i, '$1ies'],
	    [/([^ch][ieo][ln])ey$/i, '$1ies'],
	    [/(x|ch|ss|sh|zz)$/i, '$1es'],
	    [/(matr|cod|mur|sil|vert|ind|append)(?:ix|ex)$/i, '$1ices'],
	    [/\b((?:tit)?m|l)(?:ice|ouse)$/i, '$1ice'],
	    [/(pe)(?:rson|ople)$/i, '$1ople'],
	    [/(child)(?:ren)?$/i, '$1ren'],
	    [/eaux$/i, '$0'],
	    [/m[ae]n$/i, 'men'],
	    ['thou', 'you']
	  ].forEach(function (rule) {
	    return pluralize.addPluralRule(rule[0], rule[1]);
	  });
	  [
	    [/s$/i, ''],
	    [/(ss)$/i, '$1'],
	    [/(wi|kni|(?:after|half|high|low|mid|non|night|[^\w]|^)li)ves$/i, '$1fe'],
	    [/(ar|(?:wo|[ae])l|[eo][ao])ves$/i, '$1f'],
	    [/ies$/i, 'y'],
	    [/\b([pl]|zomb|(?:neck|cross)?t|coll|faer|food|gen|goon|group|lass|talk|goal|cut)ies$/i, '$1ie'],
	    [/\b(mon|smil)ies$/i, '$1ey'],
	    [/\b((?:tit)?m|l)ice$/i, '$1ouse'],
	    [/(seraph|cherub)im$/i, '$1'],
	    [/(x|ch|ss|sh|zz|tto|go|cho|alias|[^aou]us|t[lm]as|gas|(?:her|at|gr)o|[aeiou]ris)(?:es)?$/i, '$1'],
	    [/(analy|diagno|parenthe|progno|synop|the|empha|cri|ne)(?:sis|ses)$/i, '$1sis'],
	    [/(movie|twelve|abuse|e[mn]u)s$/i, '$1'],
	    [/(test)(?:is|es)$/i, '$1is'],
	    [/(alumn|syllab|vir|radi|nucle|fung|cact|stimul|termin|bacill|foc|uter|loc|strat)(?:us|i)$/i, '$1us'],
	    [/(agend|addend|millenni|dat|extrem|bacteri|desiderat|strat|candelabr|errat|ov|symposi|curricul|quor)a$/i, '$1um'],
	    [/(apheli|hyperbat|periheli|asyndet|noumen|phenomen|criteri|organ|prolegomen|hedr|automat)a$/i, '$1on'],
	    [/(alumn|alg|vertebr)ae$/i, '$1a'],
	    [/(cod|mur|sil|vert|ind)ices$/i, '$1ex'],
	    [/(matr|append)ices$/i, '$1ix'],
	    [/(pe)(rson|ople)$/i, '$1rson'],
	    [/(child)ren$/i, '$1'],
	    [/(eau)x?$/i, '$1'],
	    [/men$/i, 'man']
	  ].forEach(function (rule) {
	    return pluralize.addSingularRule(rule[0], rule[1]);
	  });
	  [
	    'adulthood',
	    'advice',
	    'agenda',
	    'aid',
	    'aircraft',
	    'alcohol',
	    'ammo',
	    'analytics',
	    'anime',
	    'athletics',
	    'audio',
	    'bison',
	    'blood',
	    'bream',
	    'buffalo',
	    'butter',
	    'carp',
	    'cash',
	    'chassis',
	    'chess',
	    'clothing',
	    'cod',
	    'commerce',
	    'cooperation',
	    'corps',
	    'debris',
	    'diabetes',
	    'digestion',
	    'elk',
	    'energy',
	    'equipment',
	    'excretion',
	    'expertise',
	    'firmware',
	    'flounder',
	    'fun',
	    'gallows',
	    'garbage',
	    'graffiti',
	    'hardware',
	    'headquarters',
	    'health',
	    'herpes',
	    'highjinks',
	    'homework',
	    'housework',
	    'information',
	    'jeans',
	    'justice',
	    'kudos',
	    'labour',
	    'literature',
	    'machinery',
	    'mackerel',
	    'mail',
	    'media',
	    'mews',
	    'moose',
	    'music',
	    'mud',
	    'manga',
	    'news',
	    'only',
	    'personnel',
	    'pike',
	    'plankton',
	    'pliers',
	    'police',
	    'pollution',
	    'premises',
	    'rain',
	    'research',
	    'rice',
	    'salmon',
	    'scissors',
	    'series',
	    'sewage',
	    'shambles',
	    'shrimp',
	    'software',
	    'species',
	    'staff',
	    'swine',
	    'tennis',
	    'traffic',
	    'transportation',
	    'trout',
	    'tuna',
	    'wealth',
	    'welfare',
	    'whiting',
	    'wildebeest',
	    'wildlife',
	    'you',
	    /pok[eé]mon$/i,
	    /[^aeiou]ese$/i,
	    /deer$/i,
	    /fish$/i,
	    /measles$/i,
	    /o[iu]s$/i,
	    /pox$/i,
	    /sheep$/i
	  ].forEach(pluralize.addUncountableRule);
	  return pluralize;
	});
} (pluralize$1));
var pluralizeExports = pluralize$1.exports;
var pluralize = getDefaultExportFromCjs(pluralizeExports);

/**
 * remark-lint rule to warn when list item markers are indented.
 *
 * ## What is this?
 *
 * This package checks indentation before list item markers.
 *
 * ## When should I use this?
 *
 * You can use this package to check that the style of list items is
 * consistent.
 *
 * ## API
 *
 * ### `unified().use(remarkLintListItemBulletIndent)`
 *
 * Warn when list item markers are indented.
 *
 * ###### Parameters
 *
 * There are no options.
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ## Recommendation
 *
 * There is no specific handling of indented list items in markdown.
 * While it is possible to use an indent to align ordered lists on their marker:
 *
 * ```markdown
 *   1. Mercury
 *  10. Venus
 * 100. Earth
 * ```
 *
 * …such a style is uncommon and hard to maintain as adding a 10th item
 * means 9 other items have to change (more arduous while unlikely would be
 * the 100th item).
 * So it is recommended to not indent items and to turn this rule on.
 *
 * ## Fix
 *
 * [`remark-stringify`][github-remark-stringify] formats all items without
 * indent.
 *
 * [api-remark-lint-list-item-bullet-indent]: #unifieduseremarklintlistitembulletindent
 * [github-remark-stringify]: https://github.com/remarkjs/remark/tree/main/packages/remark-stringify
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module list-item-bullet-indent
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 * @example
 *   {"name": "ok.md"}
 *
 *   Mercury.
 *
 *   * Venus.
 *   * Earth.
 *
 * @example
 *   {"label": "input", "name": "not-ok.md"}
 *
 *   Mercury.
 *
 *   ␠* Venus.
 *   ␠* Earth.
 *
 * @example
 *   {"label": "output", "name": "not-ok.md"}
 *
 *   3:2: Unexpected `1` space before list item, expected `0` spaces, remove them
 *   4:2: Unexpected `1` space before list item, expected `0` spaces, remove them
 */
const remarkLintListItemBulletIndent = lintRule$1(
  {
    origin: 'remark-lint:list-item-bullet-indent',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-list-item-bullet-indent#readme'
  },
  function (tree, file) {
    const treeStart = pointStart(tree);
    if (!tree || tree.type !== 'root' || !treeStart) return
    for (const child of tree.children) {
      if (child.type !== 'list') continue
      const list = child;
      for (const item of list.children) {
        const place = pointStart(item);
        if (!place) continue
        const actual = place.column - treeStart.column;
        if (actual) {
          file.message(
            'Unexpected `' +
              actual +
              '` ' +
              pluralize('space', actual) +
              ' before list item, expected `0` spaces, remove them',
            {ancestors: [tree, list, item], place}
          );
        }
      }
    }
  }
);

/**
 * remark-lint rule to warn when the whitespace after list item markers violate
 * a given style.
 *
 * ## What is this?
 *
 * This package checks the style of whitespace after list item markers.
 *
 * ## When should I use this?
 *
 * You can use this package to check that the style of whitespace after list
 * item markers and before content is consistent.
 *
 * ## API
 *
 * ### `unified().use(remarkLintListItemIndent[, options])`
 *
 * Warn when the whitespace after list item markers violate a given style.
 *
 * ###### Parameters
 *
 * * `options` ([`Options`][api-options], default: `'one'`)
 *   — preferred style
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ### `Options`
 *
 * Configuration (TypeScript type).
 *
 * * `'mixed'`
 *   — prefer `'one'` for tight lists and `'tab'` for loose lists
 * * `'one'`
 *   — prefer the size of the bullet and a single space
 * * `'tab'`
 *   — prefer the size of the bullet and a single space to the next tab stop
 *
 * ###### Type
 *
 * ```ts
 * type Options = 'mixed' | 'one' | 'tab'
 * ```
 *
 * ## Recommendation
 *
 * First some background.
 * The number of spaces that occur after list markers (`*`, `-`, and `+` for
 * unordered lists and `.` and `)` for unordered lists) and before the content
 * on the first line,
 * defines how much indentation can be used for further lines.
 * At least one space is required and up to 4 spaces are allowed.
 * If there is no further content after the marker then it’s a blank line which
 * is handled as if there was one space.
 * If there are 5 or more spaces and then content then it’s also seen as one
 * space and the rest is seen as indented code.
 *
 * Regardless of ordered and unordered,
 * there are two kinds of lists in markdown,
 * tight and loose.
 * Lists are tight by default but if there is a blank line between two list
 * items or between two blocks inside an item,
 * that turns the whole list into a loose list.
 * When turning markdown into HTML,
 * paragraphs in tight lists are not wrapped in `<p>` tags.
 *
 * How indentation of lists works in markdown has historically been a mess,
 * especially with how they interact with indented code.
 * CommonMark made that a *lot* better,
 * but there remain (documented but complex) edge cases and some behavior
 * intuitive.
 * Due to this, `'tab'` works the best in most markdown parsers *and* in
 * CommonMark.
 * Currently the situation between markdown parsers is better,
 * so the default `'one'`,
 * which seems to be the most common style used by authors,
 * is okay.
 *
 * ## Fix
 *
 * [`remark-stringify`][github-remark-stringify] uses `listItemIndent: 'one'`
 * by default.
 * `listItemIndent: 'mixed'` or `listItemIndent: 'tab'` is also supported.
 *
 * [api-options]: #options
 * [api-remark-lint-list-item-indent]: #unifieduseremarklintlistitemindent-options
 * [github-remark-stringify]: https://github.com/remarkjs/remark/tree/main/packages/remark-stringify
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module list-item-indent
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"name": "ok.md"}
 *
 *   *␠Mercury.
 *   *␠Venus.
 *
 *   111.␠Earth
 *   ␠␠␠␠␠and Mars.
 *
 *   *␠**Jupiter**.
 *
 *   ␠␠Jupiter is the fifth planet from the Sun and the largest in the Solar
 *   ␠␠System.
 *
 *   *␠Saturn.
 *
 *   ␠␠Saturn is the sixth planet from the Sun and the second-largest in the Solar System, after Jupiter.
 *
 * @example
 *   {"config": "mixed", "name": "ok.md"}
 *
 *   *␠Mercury.
 *   *␠Venus.
 *
 *   111.␠Earth
 *   ␠␠␠␠␠and Mars.
 *
 *   *␠␠␠**Jupiter**.
 *
 *   ␠␠␠␠Jupiter is the fifth planet from the Sun and the largest in the Solar
 *   ␠␠␠␠System.
 *
 *   *␠␠␠Saturn.
 *
 *   ␠␠␠␠Saturn is the sixth planet from the Sun and the second-largest in the Solar System, after Jupiter.
 *
 * @example
 *   {"config": "mixed", "label": "input", "name": "not-ok.md"}
 *
 *   *␠␠␠Mercury.
 *   *␠␠␠Venus.
 *
 *   111.␠␠␠␠Earth
 *   ␠␠␠␠␠␠␠␠and Mars.
 *
 *   *␠**Jupiter**.
 *
 *   ␠␠Jupiter is the fifth planet from the Sun and the largest in the Solar
 *   ␠␠System.
 *
 *   *␠Saturn.
 *
 *   ␠␠Saturn is the sixth planet from the Sun and the second-largest in the Solar System, after Jupiter.
 * @example
 *   {"config": "mixed", "label": "output", "name": "not-ok.md"}
 *
 *   1:5: Unexpected `3` spaces between list item marker and content in tight list, expected `1` space, remove `2` spaces
 *   2:5: Unexpected `3` spaces between list item marker and content in tight list, expected `1` space, remove `2` spaces
 *   4:9: Unexpected `4` spaces between list item marker and content in tight list, expected `1` space, remove `3` spaces
 *   7:3: Unexpected `1` space between list item marker and content in loose list, expected `3` spaces, add `2` spaces
 *   12:3: Unexpected `1` space between list item marker and content in loose list, expected `3` spaces, add `2` spaces
 *
 * @example
 *   {"config": "one", "name": "ok.md"}
 *
 *   *␠Mercury.
 *   *␠Venus.
 *
 *   111.␠Earth
 *   ␠␠␠␠␠and Mars.
 *
 *   *␠**Jupiter**.
 *
 *   ␠␠Jupiter is the fifth planet from the Sun and the largest in the Solar
 *   ␠␠System.
 *
 *   *␠Saturn.
 *
 *   ␠␠Saturn is the sixth planet from the Sun and the second-largest in the Solar System, after Jupiter.
 *
 * @example
 *   {"config": "one", "label": "input", "name": "not-ok.md"}
 *
 *   *␠␠␠Mercury.
 *   *␠␠␠Venus.
 *
 *   111.␠␠␠␠Earth
 *   ␠␠␠␠␠␠␠␠and Mars.
 *
 *   *␠␠␠**Jupiter**.
 *
 *   ␠␠␠␠Jupiter is the fifth planet from the Sun and the largest in the Solar
 *   ␠␠␠␠System.
 *
 *   *␠␠␠Saturn.
 *
 *   ␠␠␠␠Saturn is the sixth planet from the Sun and the second-largest in the Solar System, after Jupiter.
 * @example
 *   {"config": "one", "label": "output", "name": "not-ok.md"}
 *
 *   1:5: Unexpected `3` spaces between list item marker and content, expected `1` space, remove `2` spaces
 *   2:5: Unexpected `3` spaces between list item marker and content, expected `1` space, remove `2` spaces
 *   4:9: Unexpected `4` spaces between list item marker and content, expected `1` space, remove `3` spaces
 *   7:5: Unexpected `3` spaces between list item marker and content, expected `1` space, remove `2` spaces
 *   12:5: Unexpected `3` spaces between list item marker and content, expected `1` space, remove `2` spaces
 *
 * @example
 *   {"config": "tab", "name": "ok.md"}
 *
 *   *␠␠␠Mercury.
 *   *␠␠␠Venus.
 *
 *   111.␠␠␠␠Earth
 *   ␠␠␠␠␠␠␠␠and Mars.
 *
 *   *␠␠␠**Jupiter**.
 *
 *   ␠␠␠␠Jupiter is the fifth planet from the Sun and the largest in the Solar
 *   ␠␠␠␠System.
 *
 *   *␠␠␠Saturn.
 *
 *   ␠␠␠␠Saturn is the sixth planet from the Sun and the second-largest in the Solar System, after Jupiter.
 *
 * @example
 *   {"config": "tab", "label": "input", "name": "not-ok.md"}
 *
 *   *␠Mercury.
 *   *␠Venus.
 *
 *   111.␠Earth
 *   ␠␠␠␠␠and Mars.
 *
 *   *␠**Jupiter**.
 *
 *   ␠␠Jupiter is the fifth planet from the Sun and the largest in the Solar
 *   ␠␠System.
 *
 *   *␠Saturn.
 *
 *   ␠␠Saturn is the sixth planet from the Sun and the second-largest in the Solar System, after Jupiter.
 * @example
 *   {"config": "tab", "label": "output", "name": "not-ok.md"}
 *
 *   1:3: Unexpected `1` space between list item marker and content, expected `3` spaces, add `2` spaces
 *   2:3: Unexpected `1` space between list item marker and content, expected `3` spaces, add `2` spaces
 *   4:6: Unexpected `1` space between list item marker and content, expected `4` spaces, add `3` spaces
 *   7:3: Unexpected `1` space between list item marker and content, expected `3` spaces, add `2` spaces
 *   12:3: Unexpected `1` space between list item marker and content, expected `3` spaces, add `2` spaces
 *
 * @example
 *   {"config": "🌍", "label": "output", "name": "not-ok.md", "positionless": true}
 *
 *   1:1: Unexpected value `🌍` for `options`, expected `'mixed'`, `'one'`, or `'tab'`
 *
 * @example
 *   {"config": "mixed", "gfm": true, "label": "input", "name": "gfm.md"}
 *
 *   *␠[x] Mercury.
 *
 *   1.␠␠[ ] Venus.
 *
 *   2.␠␠[ ] Earth.
 *
 * @example
 *   {"config": "one", "gfm": true, "name": "gfm.md"}
 *
 *   *␠[x] Mercury.
 *
 *   1.␠[ ] Venus.
 *
 *   2.␠[ ] Earth.
 *
 * @example
 *   {"config": "tab", "gfm": true, "name": "gfm.md"}
 *
 *   *␠␠␠[x] Mercury.
 *
 *   1.␠␠[ ] Venus.
 *
 *   2.␠␠[ ] Earth.
 *
 * @example
 *   {"config": "mixed", "name": "loose-tight.md"}
 *
 *   Loose lists have blank lines between items:
 *
 *   *␠␠␠Mercury.
 *
 *   *␠␠␠Venus.
 *
 *   …or between children of items:
 *
 *   1.␠␠Earth.
 *
 *   ␠␠␠␠Earth is the third planet from the Sun and the only astronomical
 *   ␠␠␠␠object known to harbor life.
 */
const remarkLintListItemIndent = lintRule$1(
  {
    origin: 'remark-lint:list-item-indent',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-list-item-indent#readme'
  },
  function (tree, file, options) {
    const value = String(file);
    let expected;
    if (options === null || options === undefined) {
      expected = 'one';
    } else if (options === 'space') {
      file.fail(
        'Unexpected value `' + options + "` for `options`, expected `'one'`"
      );
    } else if (options === 'tab-size') {
      file.fail(
        'Unexpected value `' + options + "` for `options`, expected `'tab'`"
      );
    } else if (options === 'mixed' || options === 'one' || options === 'tab') {
      expected = options;
    } else {
      file.fail(
        'Unexpected value `' +
          options +
          "` for `options`, expected `'mixed'`, `'one'`, or `'tab'`"
      );
    }
    visitParents(tree, function (node, parents) {
      if (phrasing(node)) {
        return SKIP
      }
      if (node.type !== 'list') return
      let loose = node.spread;
      if (!loose) {
        for (const child of node.children) {
          if (child.spread) {
            loose = true;
            break
          }
        }
      }
      for (const child of node.children) {
        const head = child.children[0];
        const itemStart = pointStart(child);
        const headStart = pointStart(head);
        if (
          itemStart &&
          headStart &&
          typeof itemStart.offset === 'number' &&
          typeof headStart.offset === 'number'
        ) {
          let slice = value.slice(itemStart.offset, headStart.offset);
          const checkboxIndex = slice.indexOf('[');
          if (checkboxIndex !== -1) slice = slice.slice(0, checkboxIndex);
          const actualIndent = slice.length;
          let end = actualIndent;
          let previous = slice.charCodeAt(end - 1);
          while (previous === 9 || previous === 32) {
            end--;
            previous = slice.charCodeAt(end - 1);
          }
          let expectedIndent = end + 1;
          if (expected === 'tab' || (expected === 'mixed' && loose)) {
            expectedIndent = Math.ceil(expectedIndent / 4) * 4;
          }
          const expectedSpaces = expectedIndent - end;
          const actualSpaces = actualIndent - end;
          if (actualSpaces !== expectedSpaces) {
            const difference = expectedSpaces - actualSpaces;
            const differenceAbsolute = Math.abs(difference);
            file.message(
              'Unexpected `' +
                actualSpaces +
                '` ' +
                pluralize('space', actualSpaces) +
                ' between list item marker and content' +
                (expected === 'mixed'
                  ? ' in ' + (loose ? 'loose' : 'tight') + ' list'
                  : '') +
                ', expected `' +
                expectedSpaces +
                '` ' +
                pluralize('space', expectedSpaces) +
                ', ' +
                (difference > 0 ? 'add' : 'remove') +
                ' `' +
                differenceAbsolute +
                '` ' +
                pluralize('space', differenceAbsolute),
              {ancestors: [...parents, node, child], place: headStart}
            );
          }
        }
      }
    });
  }
);

/**
 * remark-lint rule to warn for lazy lines in block quotes.
 *
 * ## What is this?
 *
 * This package checks the style of block quotes.
 *
 * ## When should I use this?
 *
 * You can use this package to check that the style of block quotes is
 * consistent.
 *
 * ## API
 *
 * ### `unified().use(remarkLintNoBlockquoteWithoutMarker)`
 *
 * Warn for lazy lines in block quotes.
 *
 * ###### Parameters
 *
 * There are no options.
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ## Recommendation
 *
 * Rules around lazy lines are not straightforward and visually confusing,
 * so it’s recommended to start each line with a `>`.
 *
 * ## Fix
 *
 * [`remark-stringify`][github-remark-stringify] adds `>` markers to every line
 * in a block quote.
 *
 * [api-remark-lint-no-blockquote-without-marker]: #unifieduseremarklintnoblockquotewithoutmarker
 * [github-remark-stringify]: https://github.com/remarkjs/remark/tree/main/packages/remark-stringify
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module no-blockquote-without-marker
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 * @example
 *   {"name": "ok.md"}
 *
 *   > Mercury,
 *   > Venus,
 *   > and Earth.
 *
 *   Mars.
 *
 * @example
 *   {"name": "ok-tabs.md"}
 *
 *   >␉Mercury,
 *   >␉Venus,
 *   >␉and Earth.
 *
 * @example
 *   {"label": "input", "name": "not-ok.md"}
 *
 *   > Mercury,
 *   Venus,
 *   > and Earth.
 * @example
 *   {"label": "output", "name": "not-ok.md"}
 *
 *   2:1: Unexpected `0` block quote markers before paragraph line, expected `1` marker, add `1` marker
 *
 * @example
 *   {"label": "input", "name": "not-ok-tabs.md"}
 *
 *   >␉Mercury,
 *   ␉Venus,
 *   and Earth.
 * @example
 *   {"label": "output", "name": "not-ok-tabs.md"}
 *
 *   2:2: Unexpected `0` block quote markers before paragraph line, expected `1` marker, add `1` marker
 *   3:1: Unexpected `0` block quote markers before paragraph line, expected `1` marker, add `1` marker
 *
 * @example
 *   {"label": "input", "name": "containers.md"}
 *
 *   * > Mercury and
 *   Venus.
 *
 *   > * Mercury and
 *     Venus.
 *
 *   * > * Mercury and
 *       Venus.
 *
 *   > * > Mercury and
 *         Venus.
 *
 *   ***
 *
 *   > * > Mercury and
 *   >     Venus.
 * @example
 *   {"label": "output", "name": "containers.md"}
 *
 *   2:1: Unexpected `0` block quote markers before paragraph line, expected `1` marker, add `1` marker
 *   5:3: Unexpected `0` block quote markers before paragraph line, expected `1` marker, add `1` marker
 *   8:5: Unexpected `0` block quote markers before paragraph line, expected `1` marker, add `1` marker
 *   11:7: Unexpected `0` block quote markers before paragraph line, expected `2` markers, add `2` markers
 *   16:7: Unexpected `1` block quote marker before paragraph line, expected `2` markers, add `1` marker
 */
const remarkLintNoBlockquoteWithoutMarker = lintRule$1(
  {
    origin: 'remark-lint:no-blockquote-without-marker',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-no-blockquote-without-marker#readme'
  },
  function (tree, file) {
    const value = String(file);
    const loc = location(file);
    visitParents(tree, function (node, parents) {
      if (phrasing(node)) {
        return SKIP
      }
      if (node.type !== 'paragraph') return
      let expected = 0;
      for (const parent of parents) {
        if (parent.type === 'blockquote') {
          expected++;
        }
        else if (
          parent.type === 'containerDirective' ||
          parent.type === 'footnoteDefinition' ||
          parent.type === 'list' ||
          parent.type === 'listItem' ||
          parent.type === 'root'
        ) ; else {
          return SKIP
        }
      }
      if (!expected) return SKIP
      const end = pointEnd(node);
      const start = pointStart(node);
      if (!end || !start) return SKIP
      let line = start.line;
      while (++line <= end.line) {
        const lineStart = loc.toOffset({line, column: 1});
        let actual = 0;
        let index = lineStart;
        while (index < value.length) {
          const code = value.charCodeAt(index);
          if (code === 9 || code === 32) ; else if (code === 62 ) {
            actual++;
          } else {
            break
          }
          index++;
        }
        const point = loc.toPoint(index);
        const difference = expected - actual;
        if (difference) {
          file.message(
            'Unexpected `' +
              actual +
              '` block quote ' +
              pluralize('marker', actual) +
              ' before paragraph line, expected `' +
              expected +
              '` ' +
              pluralize('marker', expected) +
              ', add `' +
              difference +
              '` ' +
              pluralize('marker', difference),
            {ancestors: [...parents, node], place: point}
          );
        }
      }
    });
  }
);

/**
 * remark-lint rule to warn when identifiers are defined multiple times.
 *
 * ## What is this?
 *
 * This package checks that defined identifiers are unique.
 *
 * ## When should I use this?
 *
 * You can use this package to check that definitions are useful.
 *
 * ## API
 *
 * ### `unified().use(remarkLintNoDuplicateDefinitions)`
 *
 * Warn when identifiers are defined multiple times.
 *
 * ###### Parameters
 *
 * There are no options.
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ## Recommendation
 *
 * It’s a mistake when the same identifier is defined multiple times.
 *
 * [api-remark-lint-no-duplicate-definitions]: #unifieduseremarklintnoduplicatedefinitions
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module no-duplicate-definitions
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"name": "ok.md"}
 *
 *   [mercury]: https://example.com/mercury/
 *   [venus]: https://example.com/venus/
 *
 * @example
 *   {"label": "input", "name": "not-ok.md"}
 *
 *   [mercury]: https://example.com/mercury/
 *   [mercury]: https://example.com/venus/
 * @example
 *   {"label": "output", "name": "not-ok.md"}
 *
 *   2:1-2:38: Unexpected definition with an already defined identifier (`mercury`), expected unique identifiers
 *
 * @example
 *   {"gfm": true, "label": "input", "name": "gfm.md"}
 *
 *   Mercury[^mercury].
 *
 *   [^mercury]:
 *     Mercury is the first planet from the Sun and the smallest in the Solar
 *     System.
 *
 *   [^mercury]:
 *     Venus is the second planet from the Sun.
 *
 * @example
 *   {"gfm": true, "label": "output", "name": "gfm.md"}
 *
 *   7:1-7:12: Unexpected footnote definition with an already defined identifier (`mercury`), expected unique identifiers
 */
const empty = [];
const remarkLintNoDuplicateDefinitions = lintRule$1(
  {
    origin: 'remark-lint:no-duplicate-definitions',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-no-duplicate-definitions#readme'
  },
  function (tree, file) {
    const definitions = new Map();
    const footnoteDefinitions = new Map();
    visitParents(tree, function (node, parents) {
      if (phrasing(node)) {
        return SKIP
      }
      const [map, identifier] =
        node.type === 'definition'
          ? [definitions, node.identifier]
          : node.type === 'footnoteDefinition'
            ? [footnoteDefinitions, node.identifier]
            : empty;
      if (map && identifier && node.position) {
        const ancestors = [...parents, node];
        const duplicateAncestors = map.get(identifier);
        if (duplicateAncestors) {
          const duplicate = duplicateAncestors.at(-1);
          file.message(
            'Unexpected ' +
              (node.type === 'footnoteDefinition' ? 'footnote ' : '') +
              'definition with an already defined identifier (`' +
              identifier +
              '`), expected unique identifiers',
            {
              ancestors,
              cause: new VFileMessage('Identifier already defined here', {
                ancestors: duplicateAncestors,
                place: duplicate.position,
                source: 'remark-lint',
                ruleId: 'no-duplicate-definitions'
              }),
              place: node.position
            }
          );
        }
        map.set(identifier, ancestors);
      }
    });
  }
);

/**
 * remark-lint rule to warn when extra whitespace is used between hashes and
 * content in headings.
 *
 * ## What is this?
 *
 * This package checks whitespace between hashes and content.
 *
 * ## When should I use this?
 *
 * You can use this package to check that headings are consistent.
 *
 * ## API
 *
 * ### `unified().use(remarkLintNoHeadingContentIndent)`
 *
 * Warn when extra whitespace is used between hashes and content in headings.
 *
 * ###### Parameters
 *
 * There are no options.
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ## Recommendation
 *
 * One space is required and more than one space has no effect.
 * Due to this, it’s recommended to turn this rule on.
 *
 * ## Fix
 *
 * [`remark-stringify`][github-remark-stringify] formats headings with one space.
 *
 * [api-remark-lint-no-heading-content-indent]: #unifieduseremarklintnoheadingcontentindent
 * [github-remark-stringify]: https://github.com/remarkjs/remark/tree/main/packages/remark-stringify
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module no-heading-content-indent
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"name": "ok.md"}
 *
 *   #␠Mercury
 *
 *   ##␠Venus␠##
 *
 *   ␠␠##␠Earth
 *
 *   Setext headings are not affected:
 *
 *   ␠Mars
 *   =====
 *
 *   ␠Jupiter
 *   --------
 *
 * @example
 *   {"label": "input", "name": "not-ok.md"}
 *
 *   #␠␠Mercury
 *
 *   ##␠Venus␠␠##
 *
 *   ␠␠##␠␠␠Earth
 * @example
 *   {"label": "output", "name": "not-ok.md"}
 *
 *   1:4: Unexpected `2` spaces between hashes and content, expected `1` space, remove `1` space
 *   3:11: Unexpected `2` spaces between content and hashes, expected `1` space, remove `1` space
 *   5:8: Unexpected `3` spaces between hashes and content, expected `1` space, remove `2` spaces
 *
 * @example
 *   {"label": "input", "name": "empty-heading.md"}
 *
 *   #␠␠
 * @example
 *   {"label": "output", "name": "empty-heading.md"}
 *
 *   1:4: Unexpected `2` spaces between hashes and content, expected `1` space, remove `1` space
 */
const remarkLintNoHeadingContentIndent = lintRule$1(
  {
    origin: 'remark-lint:no-heading-content-indent',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-no-heading-content-indent#readme'
  },
  function (tree, file) {
    const value = String(file);
    visitParents(tree, function (node, parents) {
      if (phrasing(node)) {
        return SKIP
      }
      if (node.type !== 'heading') return
      const start = pointStart(node);
      const end = pointEnd(node);
      if (
        !end ||
        !start ||
        typeof end.offset !== 'number' ||
        typeof start.offset !== 'number'
      ) {
        return
      }
      let index = start.offset;
      let code = value.charCodeAt(index);
      let found = false;
      while (value.charCodeAt(index) === 35 ) {
        index++;
        found = true;
        continue
      }
      const from = index;
      code = value.charCodeAt(index);
      while (code === 9  || code === 32 ) {
        code = value.charCodeAt(++index);
        continue
      }
      const size = index - from;
      if (found && size > 1) {
        file.message(
          'Unexpected `' +
            size +
            '` ' +
            pluralize('space', size) +
            ' between hashes and content, expected `1` space, remove `' +
            (size - 1) +
            '` ' +
            pluralize('space', size - 1),
          {
            ancestors: [...parents, node],
            place: {
              line: start.line,
              column: start.column + (index - start.offset),
              offset: start.offset + (index - start.offset)
            }
          }
        );
      }
      const contentStart = index;
      index = end.offset;
      code = value.charCodeAt(index - 1);
      while (code === 9  || code === 32 ) {
        index--;
        code = value.charCodeAt(index - 1);
        continue
      }
      let endFound = false;
      while (value.charCodeAt(index - 1) === 35 ) {
        index--;
        endFound = true;
        continue
      }
      const endFrom = index;
      code = value.charCodeAt(index - 1);
      while (code === 9  || code === 32 ) {
        index--;
        code = value.charCodeAt(index - 1);
        continue
      }
      const endSize = endFrom - index;
      if (endFound && index > contentStart && endSize > 1) {
        file.message(
          'Unexpected `' +
            endSize +
            '` ' +
            pluralize('space', endSize) +
            ' between content and hashes, expected `1` space, remove `' +
            (endSize - 1) +
            '` ' +
            pluralize('space', endSize - 1),
          {
            ancestors: [...parents, node],
            place: {
              line: end.line,
              column: end.column - (end.offset - endFrom),
              offset: end.offset - (end.offset - endFrom)
            }
          }
        );
      }
    });
  }
);

/**
 * remark-lint rule to warn when GFM autolink literals are used.
 *
 * ## What is this?
 *
 * This package checks that regular autolinks or full links are used.
 * Literal autolinks is a GFM feature enabled with
 * [`remark-gfm`][github-remark-gfm].
 *
 * ## When should I use this?
 *
 * You can use this package to check that links are consistent.
 *
 * ## API
 *
 * ### `unified().use(remarkLintNoLiteralUrls)`
 *
 * Warn when GFM autolink literals are used.
 *
 * ###### Parameters
 *
 * There are no options.
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ## Recommendation
 *
 * GFM autolink literals (just a raw URL) are a feature enabled by GFM.
 * They don’t work everywhere.
 * So,
 * it’s recommended to instead use regular autolinks (`<https://url>`) or full
 * links (`[text](url)`).
 *
 * ## Fix
 *
 * [`remark-stringify`][github-remark-stringify] never generates GFM autolink
 * literals.
 * It always generates regular autolinks or full links.
 *
 * [api-remark-lint-no-literal-urls]: #unifieduseremarklintnoliteralurls
 * [github-remark-gfm]: https://github.com/remarkjs/remark-gfm
 * [github-remark-stringify]: https://github.com/remarkjs/remark/tree/main/packages/remark-stringify
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module no-literal-urls
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"name": "ok.md", "gfm": true}
 *
 *   <https://example.com/mercury/>
 *
 *   ![Venus](http://example.com/venus/).
 *
 * @example
 *   {"name": "not-ok.md", "label": "input", "gfm": true}
 *
 *   https://example.com/mercury/
 *
 *   www.example.com/venus/
 *
 *   earth@mars.planets
 *
 * @example
 *   {"name": "not-ok.md", "label": "output", "gfm": true}
 *
 *   1:1-1:29: Unexpected GFM autolink literal, expected regular autolink, add `<` before and `>` after
 *   3:1-3:23: Unexpected GFM autolink literal, expected regular autolink, add `<http://` before and `>` after
 *   5:1-5:19: Unexpected GFM autolink literal, expected regular autolink, add `<mailto:` before and `>` after
 */
const defaultHttp = 'http://';
const defaultMailto = 'mailto:';
const remarkLintNoLiteralUrls = lintRule$1(
  {
    origin: 'remark-lint:no-literal-urls',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-no-literal-urls#readme'
  },
  function (tree, file) {
    const value = String(file);
    visitParents(tree, 'link', function (node, parents) {
      const start = pointStart(node);
      if (!start || typeof start.offset !== 'number') return
      const raw = toString(node);
      let protocol;
      let otherwiseFine = false;
      if (raw === node.url) {
        otherwiseFine = true;
      } else if (defaultHttp + raw === node.url) {
        protocol = defaultHttp;
      } else if (defaultMailto + raw === node.url) {
        protocol = defaultMailto;
      }
      if (
        (protocol || otherwiseFine) &&
        !asciiPunctuation(value.charCodeAt(start.offset))
      ) {
        file.message(
          'Unexpected GFM autolink literal, expected regular autolink, add ' +
            (protocol ? '`<' + protocol + '`' : '`<`') +
            ' before and `>` after',
          {ancestors: [...parents, node], place: node.position}
        );
      }
    });
  }
);

/**
 * remark-lint rule to warn when shortcut reference images are used.
 *
 * ## What is this?
 *
 * This package checks that collapsed or full reference images are used.
 *
 * ## When should I use this?
 *
 * You can use this package to check that references are consistent.
 *
 * ## API
 *
 * ### `unified().use(remarkLintNoShortcutReferenceImage)`
 *
 * Warn when shortcut reference images are used.
 *
 * ###### Parameters
 *
 * There are no options.
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ## Recommendation
 *
 * Shortcut references use an implicit style that looks a lot like something
 * that could occur as plain text instead of syntax.
 * In some cases,
 * plain text is intended instead of an image.
 * So it’s recommended to use collapsed or full references instead.
 *
 * [api-remark-lint-no-shortcut-reference-image]: #unifieduseremarklintnoshortcutreferenceimage
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module no-shortcut-reference-image
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"name": "ok.md"}
 *
 *   ![Mercury][]
 *
 *   [mercury]: /mercury.png
 *
 * @example
 *   {"label": "input", "name": "not-ok.md"}
 *
 *   ![Mercury]
 *
 *   [mercury]: /mercury.png
 * @example
 *   {"label": "output", "name": "not-ok.md"}
 *
 *   1:1-1:11: Unexpected shortcut reference image (`![text]`), expected collapsed reference (`![text][]`)
 */
const remarkLintNoShortcutReferenceImage = lintRule$1(
  {
    origin: 'remark-lint:no-shortcut-reference-image',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-no-shortcut-reference-image#readme'
  },
  function (tree, file) {
    visitParents(tree, 'imageReference', function (node, parents) {
      if (node.position && node.referenceType === 'shortcut') {
        file.message(
          'Unexpected shortcut reference image (`![text]`), expected collapsed reference (`![text][]`)',
          {ancestors: [...parents, node], place: node.position}
        );
      }
    });
  }
);

/**
 * remark-lint rule to warn when shortcut reference links are used.
 *
 * ## What is this?
 *
 * This package checks that collapsed or full reference links are used.
 *
 * ## When should I use this?
 *
 * You can use this package to check that references are consistent.
 *
 * ## API
 *
 * ### `unified().use(remarkLintNoShortcutReferenceLink)`
 *
 * Warn when shortcut reference links are used.
 *
 * ###### Parameters
 *
 * There are no options.
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ## Recommendation
 *
 * Shortcut references use an implicit style that looks a lot like something
 * that could occur as plain text instead of syntax.
 * In some cases,
 * plain text is intended instead of a link.
 * So it’s recommended to use collapsed or full references instead.
 *
 * [api-remark-lint-no-shortcut-reference-link]: #unifieduseremarklintnoshortcutreferencelink
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module no-shortcut-reference-link
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"name": "ok.md"}
 *
 *   [Mercury][]
 *
 *   [mercury]: http://example.com/mercury/
 *
 * @example
 *   {"label": "input", "name": "not-ok.md"}
 *
 *   [Mercury]
 *
 *   [mercury]: http://example.com/mercury/
 * @example
 *   {"label": "output", "name": "not-ok.md"}
 *
 *   1:1-1:10: Unexpected shortcut reference link (`[text]`), expected collapsed reference (`[text][]`)
 */
const remarkLintNoShortcutReferenceLink = lintRule$1(
  {
    origin: 'remark-lint:no-shortcut-reference-link',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-no-shortcut-reference-link#readme'
  },
  function (tree, file) {
    visitParents(tree, 'linkReference', function (node, parents) {
      if (node.position && node.referenceType === 'shortcut') {
        file.message(
          'Unexpected shortcut reference link (`[text]`), expected collapsed reference (`[text][]`)',
          {ancestors: [...parents, node], place: node.position}
        );
      }
    });
  }
);

const js = /\s+/g;
const html = /[\t\n\v\f\r ]+/g;
function collapseWhiteSpace(value, options) {
  if (!options) {
    options = {};
  } else if (typeof options === 'string') {
    options = {style: options};
  }
  const replace = options.preserveLineEndings ? replaceLineEnding : replaceSpace;
  return String(value).replace(
    options.style === 'html' ? html : js,
    options.trim ? trimFactory(replace) : replace
  )
}
function replaceLineEnding(value) {
  const match = /\r?\n|\r/.exec(value);
  return match ? match[0] : ' '
}
function replaceSpace() {
  return ' '
}
function trimFactory(replace) {
  return dropOrReplace
  function dropOrReplace(value, index, all) {
    return index === 0 || index + value.length === all.length
      ? ''
      : replace(value)
  }
}

/**
 * remark-lint rule to warn when undefined definitions are referenced.
 *
 * ## What is this?
 *
 * This package checks that referenced definitions are defined.
 *
 * ## When should I use this?
 *
 * You can use this package to check for broken references.
 *
 * ## API
 *
 * ### `unified().use(remarkLintNoUndefinedReferences[, options])`
 *
 * Warn when undefined definitions are referenced.
 *
 * ###### Parameters
 *
 * * `options` ([`Options`][api-options], optional)
 *   — configuration
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ### `Options`
 *
 * Configuration (TypeScript type).
 *
 * ###### Fields
 *
 * * `allow` (`Array<RegExp | string>`, optional)
 *   — list of values to allow between `[` and `]`
 * * `allowShortcutLink` (`boolean`, default: `false`)
 *   — allow shortcut references, which are just brackets such as `[text]`
 *
 * ## Recommendation
 *
 * Shortcut references use an implicit syntax that could also occur as plain
 * text.
 * To illustrate,
 * it is reasonable to expect an author adding `[…]` to abbreviate some text
 * somewhere in a document:
 *
 * ```markdown
 * > Some […] quote.
 * ```
 *
 * This isn’t a problem,
 * but it might become one when an author later adds a definition:
 *
 * ```markdown
 * Some new text […][].
 *
 * […]: #read-more
 * ```
 *
 * The second author might expect only their newly added text to form a link,
 * but their changes also result in a link for the text by the first author.
 *
 * [api-options]: #options
 * [api-remark-lint-no-undefined-references]: #unifieduseremarklintnoundefinedreferences-options
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module no-undefined-references
 * @author Titus Wormer
 * @copyright 2016 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"name": "ok.md"}
 *
 *   [Mercury][] is the first planet from the Sun and the smallest in the Solar
 *   System.
 *
 *   Venus is the second planet from the [Sun.
 *
 *   Earth is the third planet from the \[Sun] and the only astronomical object
 *   known to harbor life\.
 *
 *   Mars is the fourth planet from the Sun: [].
 *
 *   [mercury]: https://example.com/mercury/
 *
 * @example
 *   {"label": "input", "name": "not-ok.md"}
 *
 *   [Mercury] is the first planet from the Sun and the smallest in the Solar
 *   System.
 *
 *   [Venus][] is the second planet from the Sun.
 *
 *   [Earth][earth] is the third planet from the Sun and the only astronomical
 *   object known to harbor life.
 *
 *   ![Mars] is the fourth planet from the Sun in the [Solar
 *   System].
 *
 *   > Jupiter is the fifth planet from the Sun and the largest in the [Solar
 *   > System][].
 *
 *   [Saturn][ is the sixth planet from the Sun and the second-largest
 *   in the Solar System, after Jupiter.
 *
 *   [*Uranus*][] is the seventh planet from the Sun.
 *
 *   [Neptune][neptune][more] is the eighth and farthest planet from the Sun.
 * @example
 *   {"label": "output", "name": "not-ok.md"}
 *
 *   1:1-1:10: Unexpected reference to undefined definition, expected corresponding definition (`mercury`) for a link or escaped opening bracket (`\[`) for regular text
 *   4:1-4:10: Unexpected reference to undefined definition, expected corresponding definition (`venus`) for a link or escaped opening bracket (`\[`) for regular text
 *   6:1-6:15: Unexpected reference to undefined definition, expected corresponding definition (`earth`) for a link or escaped opening bracket (`\[`) for regular text
 *   9:2-9:8: Unexpected reference to undefined definition, expected corresponding definition (`mars`) for an image or escaped opening bracket (`\[`) for regular text
 *   9:50-10:8: Unexpected reference to undefined definition, expected corresponding definition (`solar system`) for a link or escaped opening bracket (`\[`) for regular text
 *   12:67-13:12: Unexpected reference to undefined definition, expected corresponding definition (`solar > system`) for a link or escaped opening bracket (`\[`) for regular text
 *   15:1-15:9: Unexpected reference to undefined definition, expected corresponding definition (`saturn`) for a link or escaped opening bracket (`\[`) for regular text
 *   18:1-18:13: Unexpected reference to undefined definition, expected corresponding definition (`*uranus*`) for a link or escaped opening bracket (`\[`) for regular text
 *   20:1-20:19: Unexpected reference to undefined definition, expected corresponding definition (`neptune`) for a link or escaped opening bracket (`\[`) for regular text
 *   20:19-20:25: Unexpected reference to undefined definition, expected corresponding definition (`more`) for a link or escaped opening bracket (`\[`) for regular text
 *
 * @example
 *   {"config": {"allow": ["…"]}, "name": "ok-allow.md"}
 *
 *   Mercury is the first planet from the Sun and the smallest in the Solar
 *   System. […]
 *
 * @example
 *   {"config": {"allow": [{"source": "^mer"}, "venus"]}, "name": "source.md"}
 *
 *   [Mercury][] is the first planet from the Sun and the smallest in the Solar
 *   System.
 *
 *   [Venus][] is the second planet from the Sun.
 *
 * @example
 *   {"gfm": true, "label": "input", "name": "gfm.md"}
 *
 *   Mercury[^mercury] is the first planet from the Sun and the smallest in the
 *   Solar System.
 *
 *   [^venus]:
 *       **Venus** is the second planet from the Sun.
 * @example
 *   {"gfm": true, "label": "output", "name": "gfm.md"}
 *
 *   1:8-1:18: Unexpected reference to undefined definition, expected corresponding definition (`mercury`) for a footnote or escaped opening bracket (`\[`) for regular text
 *
 * @example
 *   {"config": {"allowShortcutLink": true}, "label": "input", "name": "allow-shortcut-link.md"}
 *
 *   [Mercury] is the first planet from the Sun and the smallest in the Solar
 *   System.
 *
 *   [Venus][] is the second planet from the Sun.
 *
 *   [Earth][earth] is the third planet from the Sun and the only astronomical object
 *   known to harbor life.
 * @example
 *   {"config": {"allowShortcutLink": true}, "label": "output", "name": "allow-shortcut-link.md"}
 *
 *   4:1-4:10: Unexpected reference to undefined definition, expected corresponding definition (`venus`) for a link or escaped opening bracket (`\[`) for regular text
 *   6:1-6:15: Unexpected reference to undefined definition, expected corresponding definition (`earth`) for a link or escaped opening bracket (`\[`) for regular text
 */
const emptyOptions = {};
const emptyAllow = [];
const lineEndingExpression = /(\r?\n|\r)[\t ]*(>[\t ]*)*/g;
const remarkLintNoUndefinedReferences = lintRule$1(
  {
    origin: 'remark-lint:no-undefined-references',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-no-undefined-references#readme'
  },
  function (tree, file, options) {
    const settings = options || emptyOptions;
    const allow = settings.allow || emptyAllow;
    const allowShortcutLink = settings.allowShortcutLink || false;
    const value = String(file);
    const toPoint = location(file).toPoint;
    const definitionIdentifiers = new Set();
    const footnoteDefinitionIdentifiers = new Set();
    const regexes = [];
    const strings = new Set();
    const phrasingStacks = [];
    let index = -1;
    while (++index < allow.length) {
      const value = allow[index];
      if (typeof value === 'string') {
        strings.add(normalizeIdentifier(value));
      } else if (typeof value === 'object' && 'source' in value) {
        regexes.push(new RegExp(value.source, value.flags ?? 'i'));
      }
    }
    visitParents(tree, function (node, parents) {
      if (node.type === 'definition') {
        definitionIdentifiers.add(normalizeIdentifier(node.identifier));
      }
      if (node.type === 'footnoteDefinition') {
        footnoteDefinitionIdentifiers.add(normalizeIdentifier(node.identifier));
      }
      if (node.type === 'heading' || node.type === 'paragraph') {
        phrasingStacks.push([...parents, node]);
      }
    });
    for (const ancestors of phrasingStacks) {
      findInPhrasingContainer(ancestors);
    }
    function findInPhrasingContainer(ancestors) {
      const bracketRanges = [];
      const node = ancestors.at(-1);
      for (const child of node.children) {
        if (child.type === 'text') {
          findRangesInText(bracketRanges, [...ancestors, child]);
        } else if ('children' in child) {
          findInPhrasingContainer([...ancestors, child]);
        }
      }
      for (const range of bracketRanges) {
        handleRange(range);
      }
    }
    function findRangesInText(ranges, ancestors) {
      const node = ancestors.at(-1);
      const end = pointEnd(node);
      const start = pointStart(node);
      if (
        !end ||
        !start ||
        typeof start.offset !== 'number' ||
        typeof end.offset !== 'number'
      ) {
        return
      }
      const source = value.slice(start.offset, end.offset);
      const lines = [[start.offset, '']];
      let last = 0;
      lineEndingExpression.lastIndex = 0;
      let match = lineEndingExpression.exec(source);
      while (match) {
        const index = match.index;
        const lineTuple = lines.at(-1);
        lineTuple[1] = source.slice(last, index);
        last = index + match[0].length;
        lines.push([start.offset + last, '']);
        match = lineEndingExpression.exec(source);
      }
      const lineTuple = lines.at(-1);
      lineTuple[1] = source.slice(last);
      for (const lineTuple of lines) {
        const [lineStart, line] = lineTuple;
        let index = 0;
        while (index < line.length) {
          const code = line.charCodeAt(index);
          if (code === 91 ) {
            ranges.push([ancestors, [lineStart + index]]);
            index++;
          }
          else if (code === 92 ) {
            const next = line.charCodeAt(index + 1);
            index++;
            if (next === 91  || next === 93 ) {
              index++;
            }
          }
          else if (code === 93 ) {
            const bracketInfo = ranges.at(-1);
            if (!bracketInfo) {
              index++;
            }
            else if (
              line.charCodeAt(index + 1) === 91  &&
              bracketInfo[1].length !== 3
            ) {
              index++;
              bracketInfo[1].push(lineStart + index, lineStart + index);
              index++;
            }
            else {
              index++;
              bracketInfo[1].push(lineStart + index);
              handleRange(bracketInfo);
              ranges.pop();
            }
          }
          else {
            index++;
          }
        }
      }
    }
    function handleRange(bracketRange) {
      const [ancestors, range] = bracketRange;
      if (range.length === 1) return
      if (range.length === 3) range.length = 2;
      if (range.length === 2 && range[0] + 2 === range[1]) return
      const label =
        value.charCodeAt(range[0] - 1) === 33
          ? 'image'
          : value.charCodeAt(range[0] + 1) === 94
            ? 'footnote'
            : 'link';
      const offset = range.length === 4 && range[2] + 2 !== range[3] ? 2 : 0;
      let id = normalizeIdentifier(
        collapseWhiteSpace(
          value.slice(range[0 + offset] + 1, range[1 + offset] - 1),
          {style: 'html', trim: true}
        )
      );
      let defined = definitionIdentifiers;
      if (label === 'footnote') {
        if (id.includes(' ')) return
        defined = footnoteDefinitionIdentifiers;
        id = id.slice(1);
      }
      if (
        (allowShortcutLink && range.length === 2) ||
        defined.has(id) ||
        strings.has(id) ||
        regexes.some(function (regex) {
          return regex.test(id)
        })
      ) {
        return
      }
      const start = toPoint(range[0]);
      const end = toPoint(range[range.length - 1]);
      if (end && start) {
        file.message(
          'Unexpected reference to undefined definition, expected corresponding definition (`' +
            id.toLowerCase() +
            '`) for ' +
            (label === 'image' ? 'an' : 'a') +
            ' ' +
            label +
            ' or escaped opening bracket (`\\[`) for regular text',
          {
            ancestors,
            place: {start, end}
          }
        );
      }
    }
  }
);

/**
 * remark-lint rule to warn when unreferenced definitions are used.
 *
 * ## What is this?
 *
 * This package checks that definitions are referenced.
 *
 * ## When should I use this?
 *
 * You can use this package to check definitions.
 *
 * ## API
 *
 * ### `unified().use(remarkLintNoUnusedDefinitions)`
 *
 * Warn when unreferenced definitions are used.
 *
 * ###### Parameters
 *
 * There are no options.
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ## Recommendation
 *
 * Unused definitions do not contribute anything, so they can be removed.
 *
 * [api-remark-lint-no-unused-definitions]: #unifieduseremarklintnounuseddefinitions
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module no-unused-definitions
 * @author Titus Wormer
 * @copyright 2016 Titus Wormer
 * @license MIT
 * @example
 *   {"name": "ok.md"}
 *
 *   [Mercury][]
 *
 *   [mercury]: https://example.com/mercury/
 *
 * @example
 *   {"label": "input", "name": "not-ok.md"}
 *
 *   [mercury]: https://example.com/mercury/
 *
 * @example
 *   {"label": "output", "name": "not-ok.md"}
 *
 *   1:1-1:40: Unexpected unused definition, expected no definition or one or more references to `mercury`
 *
 * @example
 *   {"gfm": true, "label": "input", "name": "gfm.md"}
 *
 *   Mercury[^mercury] is a planet.
 *
 *   [^Mercury]:
 *       **Mercury** is the first planet from the Sun and the smallest
 *       in the Solar System.
 *   [^Venus]:
 *       **Venus** is the second planet from
 *       the Sun.
 * @example
 *   {"gfm": true, "label": "output", "name": "gfm.md"}
 *
 *   6:1-8:13: Unexpected unused footnote definition, expected no definition or one or more footnote references to `venus`
 */
const remarkLintNoUnusedDefinitions = lintRule$1(
  {
    origin: 'remark-lint:no-unused-definitions',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-no-unused-definitions#readme'
  },
  function (tree, file) {
    const footnoteDefinitions = new Map();
    const definitions = new Map();
    visitParents(tree, function (node, parents) {
      if ('identifier' in node) {
        const map =
          node.type === 'footnoteDefinition' ||
          node.type === 'footnoteReference'
            ? footnoteDefinitions
            : definitions;
        let entry = map.get(node.identifier);
        if (!entry) {
          entry = {ancestors: undefined, used: false};
          map.set(node.identifier, entry);
        }
        if (node.type === 'definition' || node.type === 'footnoteDefinition') {
          entry.ancestors = [...parents, node];
        } else if (
          node.type === 'imageReference' ||
          node.type === 'linkReference' ||
          node.type === 'footnoteReference'
        ) {
          entry.used = true;
        }
      }
    });
    const entries = [...footnoteDefinitions.values(), ...definitions.values()];
    for (const entry of entries) {
      if (!entry.used) {
        ok$1(entry.ancestors);
        const node = entry.ancestors.at(-1);
        ok$1(node.type === 'footnoteDefinition' || node.type === 'definition');
        if (node.position) {
          const prefix = node.type === 'footnoteDefinition' ? 'footnote ' : '';
          file.message(
            'Unexpected unused ' +
              prefix +
              'definition, expected no definition or one or more ' +
              prefix +
              'references to `' +
              node.identifier +
              '`',
            {ancestors: entry.ancestors, place: node.position}
          );
        }
      }
    }
  }
);

/**
 * remark-lint rule to warn when ordered list markers are inconsistent.
 *
 * ## What is this?
 *
 * This package checks ordered list markers.
 *
 * ## When should I use this?
 *
 * You can use this package to check ordered lists.
 *
 * ## API
 *
 * ### `unified().use(remarkLintOrderedListMarkerStyle[, options])`
 *
 * Warn when ordered list markers are inconsistent.
 *
 * ###### Parameters
 *
 * * `options` ([`Options`][api-options], default: `'consistent'`)
 *   — preferred style or whether to detect the first style and warn for
 *   further differences
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ### `Options`
 *
 * Configuration (TypeScript type).
 *
 * ###### Type
 *
 * ```ts
 * type Options = Style | 'consistent'
 * ```
 *
 * ### `Style`
 *
 * Style (TypeScript type).
 *
 * ###### Type
 *
 * ```ts
 * type Style = '.' | ')'
 * ```
 *
 * ## Recommendation
 *
 * Parens for list markers were not supported in markdown before CommonMark.
 * While they should work in most places now,
 * not all markdown parsers follow CommonMark.
 * So it’s recommended to prefer dots.
 *
 * ## Fix
 *
 * [`remark-stringify`][github-remark-stringify] formats ordered lists with
 * dots by default.
 * Pass `bulletOrdered: ')'` to always use parens.
 *
 * [api-style]: #style
 * [api-options]: #options
 * [api-remark-lint-ordered-list-marker-style]: #unifieduseremarklintorderedlistmarkerstyle-options
 * [github-remark-stringify]: https://github.com/remarkjs/remark/tree/main/packages/remark-stringify
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module ordered-list-marker-style
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"name": "ok.md"}
 *
 *   1. Mercury
 *
 *   * Venus
 *
 *   1. Earth
 *
 * @example
 *   {"name": "ok.md", "config": "."}
 *
 *   1. Mercury
 *
 * @example
 *   {"name": "ok.md", "config": ")"}
 *
 *   1) Mercury
 *
 * @example
 *   {"label": "input", "name": "not-ok.md"}
 *
 *   1. Mercury
 *
 *   1) Venus
 *
 * @example
 *   {"label": "output", "name": "not-ok.md"}
 *
 *   3:2: Unexpected ordered list marker `)`, expected `.`
 *
 * @example
 *   {"name": "not-ok.md", "label": "output", "config": "🌍", "positionless": true}
 *
 *   1:1: Unexpected value `🌍` for `options`, expected `'.'`, `')'`, or `'consistent'`
 */
const remarkLintOrderedListMarkerStyle = lintRule$1(
  {
    origin: 'remark-lint:ordered-list-marker-style',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-ordered-list-marker-style#readme'
  },
  function (tree, file, options) {
    const value = String(file);
    let expected;
    let cause;
    if (options === null || options === undefined || options === 'consistent') ; else if (options === '.' || options === ')') {
      expected = options;
    } else {
      file.fail(
        'Unexpected value `' +
          options +
          "` for `options`, expected `'.'`, `')'`, or `'consistent'`"
      );
    }
    visitParents(tree, function (node, parents) {
      if (phrasing(node)) {
        return SKIP
      }
      if (node.type !== 'listItem') return
      const parent = parents.at(-1);
      if (!parent || parent.type !== 'list' || !parent.ordered) return
      const start = pointStart(node);
      if (start && typeof start.offset === 'number') {
        let index = start.offset;
        let code = value.charCodeAt(index);
        while (asciiDigit(code)) {
          index++;
          code = value.charCodeAt(index);
        }
        const actual =
          code === 41  ? ')' : code === 46  ? '.' : undefined;
        if (!actual) return
        const place = {
          line: start.line,
          column: start.column + (index - start.offset),
          offset: start.offset + (index - start.offset)
        };
        if (expected) {
          if (actual !== expected) {
            file.message(
              'Unexpected ordered list marker `' +
                actual +
                '`, expected `' +
                expected +
                '`',
              {ancestors: [...parents, node], cause, place}
            );
          }
        } else {
          expected = actual;
          cause = new VFileMessage(
            'Ordered list marker style `' +
              expected +
              "` first defined for `'consistent'` here",
            {
              ancestors: [...parents, node],
              place,
              ruleId: 'ordered-list-marker-style',
              source: 'remark-lint'
            }
          );
        }
      }
    });
  }
);

const remarkPresetLintRecommended = {
  plugins: [
    remarkLint,
    remarkLintFinalNewline,
    remarkLintListItemBulletIndent,
    [remarkLintListItemIndent, 'one'],
    remarkLintNoBlockquoteWithoutMarker,
    remarkLintNoLiteralUrls,
    [remarkLintOrderedListMarkerStyle, '.'],
    remarkLintHardBreakSpaces,
    remarkLintNoDuplicateDefinitions,
    remarkLintNoHeadingContentIndent,
    remarkLintNoShortcutReferenceImage,
    remarkLintNoShortcutReferenceLink,
    remarkLintNoUndefinedReferences,
    remarkLintNoUnusedDefinitions
  ]
};

/**
 * remark-lint rule to warn when block quotes are indented too much or
 * too little.
 *
 * ## What is this?
 *
 * This package checks the “indent” of block quotes: the `>` (greater than)
 * marker *and* the spaces before content.
 *
 * ## When should I use this?
 *
 * You can use this rule to check markdown code style.
 *
 * ## API
 *
 * ### `unified().use(remarkLintBlockquoteIndentation[, options])`
 *
 * Warn when block quotes are indented too much or too little.
 *
 * ###### Parameters
 *
 * * `options` ([`Options`][api-options], default: `'consistent'`)
 *   — either a preferred indent or whether to detect the first style
 *   and warn for further differences
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ### `Options`
 *
 * Configuration (TypeScript type).
 *
 * ###### Type
 *
 * ```ts
 * type Options = number | 'consistent'
 * ```
 *
 * ## Recommendation
 *
 * CommonMark specifies that when block quotes are used the `>` markers can be
 * followed by an optional space.
 * No space at all arguably looks rather ugly:
 *
 * ```markdown
 * >Mars and
 * >Venus.
 * ```
 *
 * There is no specific handling of more that one space, so if 5 spaces were
 * used after `>`, then indented code kicks in:
 *
 * ```markdown
 * >     neptune()
 * ```
 *
 * Due to this, it’s recommended to configure this rule with `2`.
 *
 * [api-options]: #options
 * [api-remark-lint-blockquote-indentation]: #unifieduseremarklintblockquoteindentation-options
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module blockquote-indentation
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"config": 2, "name": "ok-2.md"}
 *
 *   > Mercury.
 *
 *   Venus.
 *
 *   > Earth.
 *
 * @example
 *   {"config": 4, "name": "ok-4.md"}
 *
 *   >   Mercury.
 *
 *   Venus.
 *
 *   >   Earth.
 *
 * @example
 *   { "name": "ok-tab.md"}
 *
 *   >␉Mercury.
 *
 * @example
 *   {"label": "input", "name": "not-ok.md"}
 *
 *   >  Mercury.
 *
 *   Venus.
 *
 *   >   Earth.
 *
 *   Mars.
 *
 *   > Jupiter
 * @example
 *   {"label": "output", "name": "not-ok.md"}
 *
 *   5:5: Unexpected `4` spaces between block quote marker and content, expected `3` spaces, remove `1` space
 *   9:3: Unexpected `2` spaces between block quote marker and content, expected `3` spaces, add `1` space
 *
 * @example
 *   {"config": "🌍", "label": "output", "name": "not-ok-options.md", "positionless": true}
 *
 *   1:1: Unexpected value `🌍` for `options`, expected `number` or `'consistent'`
 */
const remarkLintBlockquoteIndentation = lintRule$1(
  {
    origin: 'remark-lint:blockquote-indentation',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-blockquote-indentation#readme'
  },
  function (tree, file, options) {
    let expected;
    if (options === null || options === undefined || options === 'consistent') ; else if (typeof options === 'number') {
      expected = options;
    } else {
      file.fail(
        'Unexpected value `' +
          options +
          "` for `options`, expected `number` or `'consistent'`"
      );
    }
    visitParents(tree, function (node, parents) {
      if (phrasing(node)) {
        return SKIP
      }
      if (node.type !== 'blockquote') return
      const start = pointStart(node);
      const headStart = pointStart(node.children[0]);
      if (headStart && start) {
        const actual = headStart.column - start.column;
        if (expected) {
          const difference = expected - actual;
          const differenceAbsolute = Math.abs(difference);
          if (difference !== 0) {
            file.message(
              'Unexpected `' +
                actual +
                '` ' +
                pluralize('space', actual) +
                ' between block quote marker and content, expected `' +
                expected +
                '` ' +
                pluralize('space', expected) +
                ', ' +
                (difference > 0 ? 'add' : 'remove') +
                ' `' +
                differenceAbsolute +
                '` ' +
                pluralize('space', differenceAbsolute),
              {ancestors: [...parents, node], place: headStart}
            );
          }
        } else {
          expected = actual;
        }
      }
    });
  }
);

/**
 * remark-lint rule to warn when list item checkboxes violate a given
 * style.
 *
 * ## What is this?
 *
 * This package checks the character used in checkboxes.
 *
 * ## When should I use this?
 *
 * You can use this package to check that the style of GFM tasklists is
 * consistent.
 * Task lists are a GFM feature enabled with
 * [`remark-gfm`][github-remark-gfm].
 *
 * ## API
 *
 * ### `unified().use(remarkLintCheckboxCharacterStyle[, options])`
 *
 * Warn when list item checkboxes violate a given style.
 *
 * ###### Parameters
 *
 * * `options` ([`Options`][api-options], default: `'consistent'`)
 *   — either preferred values or whether to detect the first styles
 *   and warn for further differences
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ### `Options`
 *
 * Configuration (TypeScript type).
 *
 * ###### Type
 *
 * ```ts
 * type Options = Styles | 'consistent'
 * ```
 *
 * ### `Styles`
 *
 * Styles (TypeScript type).
 *
 * ###### Fields
 *
 * * `checked` (`'X'`, `'x'`, or `'consistent'`, default: `'consistent'`)
 *   — preferred style to use for checked checkboxes
 * * `unchecked` (`'␉'` (a tab), `'␠'` (a space), or `'consistent'`, default:
 *   `'consistent'`)
 *   — preferred style to use for unchecked checkboxes
 *
 * ## Recommendation
 *
 * It’s recommended to set `options.checked` to `'x'` (a lowercase X) as it
 * prevents an extra keyboard press and `options.unchecked` to `'␠'` (a space)
 * to make all checkboxes align.
 *
 * ## Fix
 *
 * [`remark-stringify`][github-remark-stringify] formats checked checkboxes
 * using `'x'` (lowercase X) and unchecked checkboxes using `'␠'` (a space).
 *
 * [api-options]: #options
 * [api-remark-lint-checkbox-character-style]: #unifieduseremarklintcheckboxcharacterstyle-options
 * [api-styles]: #styles
 * [github-remark-gfm]: https://github.com/remarkjs/remark-gfm
 * [github-remark-stringify]: https://github.com/remarkjs/remark/tree/main/packages/remark-stringify
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module checkbox-character-style
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"config": {"checked": "x"}, "gfm": true, "name": "ok-x.md"}
 *
 *   - [x] Mercury.
 *   - [x] Venus.
 *
 * @example
 *   {"config": {"checked": "X"}, "gfm": true, "name": "ok-x-upper.md"}
 *
 *   - [X] Mercury.
 *   - [X] Venus.
 *
 * @example
 *   {"config": {"unchecked": " "}, "gfm": true, "name": "ok-space.md"}
 *
 *   - [ ] Mercury.
 *   - [ ] Venus.
 *   - [ ]␠␠
 *   - [ ]
 *
 * @example
 *   {"config": {"unchecked": "\t"}, "gfm": true, "name": "ok-tab.md"}
 *
 *   - [␉] Mercury.
 *   - [␉] Venus.
 *
 * @example
 *   {"label": "input", "gfm": true, "name": "not-ok-default.md"}
 *
 *   - [x] Mercury.
 *   - [X] Venus.
 *   - [ ] Earth.
 *   - [␉] Mars.
 * @example
 *   {"label": "output", "gfm": true, "name": "not-ok-default.md"}
 *
 *   2:5: Unexpected checked checkbox value `X`, expected `x`
 *   4:5: Unexpected unchecked checkbox value `\t`, expected ` `
 *
 * @example
 *   {"config": "🌍", "label": "output", "name": "not-ok-option.md", "positionless": true}
 *
 *   1:1: Unexpected value `🌍` for `options`, expected an object or `'consistent'`
 *
 * @example
 *   {"config": {"unchecked": "🌍"}, "label": "output", "name": "not-ok-option-unchecked.md", "positionless": true}
 *
 *   1:1: Unexpected value `🌍` for `options.unchecked`, expected `'\t'`, `' '`, or `'consistent'`
 *
 * @example
 *   {"config": {"checked": "🌍"}, "label": "output", "name": "not-ok-option-checked.md", "positionless": true}
 *
 *   1:1: Unexpected value `🌍` for `options.checked`, expected `'X'`, `'x'`, or `'consistent'`
 */
const remarkLintCheckboxCharacterStyle = lintRule$1(
  {
    origin: 'remark-lint:checkbox-character-style',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-checkbox-character-style#readme'
  },
  function (tree, file, options) {
    const value = String(file);
    let checkedExpected;
    let checkedConsistentCause;
    let uncheckedExpected;
    let uncheckedConsistentCause;
    if (options === null || options === undefined || options === 'consistent') ; else if (typeof options === 'object') {
      if (options.checked === 'X' || options.checked === 'x') {
        checkedExpected = options.checked;
      } else if (options.checked && options.checked !== 'consistent') {
        file.fail(
          'Unexpected value `' +
            options.checked +
            "` for `options.checked`, expected `'X'`, `'x'`, or `'consistent'`"
        );
      }
      if (options.unchecked === '\t' || options.unchecked === ' ') {
        uncheckedExpected = options.unchecked;
      } else if (options.unchecked && options.unchecked !== 'consistent') {
        file.fail(
          'Unexpected value `' +
            options.unchecked +
            "` for `options.unchecked`, expected `'\\t'`, `' '`, or `'consistent'`"
        );
      }
    } else {
      file.fail(
        'Unexpected value `' +
          options +
          "` for `options`, expected an object or `'consistent'`"
      );
    }
    visitParents(tree, function (node, parents) {
      if (phrasing(node)) {
        return SKIP
      }
      if (node.type !== 'listItem') return
      const head = node.children[0];
      const headStart = pointStart(head);
      if (
        !head ||
        !headStart ||
        typeof node.checked !== 'boolean' ||
        typeof headStart.offset !== 'number'
      ) {
        return
      }
      headStart.offset -= 2;
      headStart.column -= 2;
      const match = /\[([\t Xx])]/.exec(
        value.slice(headStart.offset - 2, headStart.offset + 1)
      );
      if (!match) return
      const actual = match[1];
      const actualDisplay = actual === '\t' ? '\\t' : actual;
      const expected = node.checked ? checkedExpected : uncheckedExpected;
      const expectedDisplay = expected === '\t' ? '\\t' : expected;
      if (!expected) {
        const cause = new VFileMessage(
          (node.checked ? 'C' : 'Unc') +
            "hecked checkbox style `'" +
            actualDisplay +
            "'` first defined for `'consistent'` here",
          {
            ancestors: [...parents, node],
            place: headStart,
            ruleId: 'checkbox-character-style',
            source: 'remark-lint'
          }
        );
        if (node.checked) {
          checkedExpected =  (actual);
          checkedConsistentCause = cause;
        } else {
          uncheckedExpected =  (actual);
          uncheckedConsistentCause = cause;
        }
      } else if (actual !== expected) {
        file.message(
          'Unexpected ' +
            (node.checked ? '' : 'un') +
            'checked checkbox value `' +
            actualDisplay +
            '`, expected `' +
            expectedDisplay +
            '`',
          {
            ancestors: [...parents, node],
            cause: node.checked
              ? checkedConsistentCause
              : uncheckedConsistentCause,
            place: headStart
          }
        );
      }
    });
  }
);

/**
 * remark-lint rule to warn when GFM tasklist checkboxes are followed by
 * more than one space.
 *
 * ## What is this?
 *
 * This package checks the space after checkboxes.
 *
 * ## When should I use this?
 *
 * You can use this package to check that the style of GFM tasklists is
 * a single space.
 *
 * ## API
 *
 * ### `unified().use(remarkLintCheckboxContentIndent)`
 *
 * Warn when GFM tasklist checkboxes are followed by more than one space.
 *
 * ###### Parameters
 *
 * There are no options.
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ## Recommendation
 *
 * GFM allows zero or more spaces and tabs after checkboxes.
 * No space at all arguably looks rather ugly:
 *
 * ```markdown
 * * [x]Pluto
 * ```
 *
 * More that one space is superfluous:
 *
 * ```markdown
 * * [x]   Jupiter
 * ```
 *
 * Due to this, it’s recommended to turn this rule on.
 *
 * ## Fix
 *
 * [`remark-stringify`][github-remark-stringify] formats checkboxes and the
 * content after them with a single space between.
 *
 * [api-remark-lint-checkbox-content-indent]: #unifieduseremarklintcheckboxcontentindent
 * [github-remark-stringify]: https://github.com/remarkjs/remark/tree/main/packages/remark-stringify
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module checkbox-content-indent
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"gfm": true, "name": "ok.md"}
 *
 *   - [ ] Mercury.
 *   +  [x] Venus.
 *   *   [X] Earth.
 *   -    [ ] Mars.
 *
 * @example
 *   {"gfm": true, "label": "input", "name": "not-ok.md"}
 *
 *   - [ ] Mercury.
 *   + [x]  Venus.
 *   * [X]   Earth.
 *   - [ ]    Mars.
 * @example
 *   {"gfm": true, "label": "output", "name": "not-ok.md"}
 *
 *   2:8: Unexpected `2` spaces between checkbox and content, expected `1` space, remove `1` space
 *   3:9: Unexpected `3` spaces between checkbox and content, expected `1` space, remove `2` spaces
 *   4:10: Unexpected `4` spaces between checkbox and content, expected `1` space, remove `3` spaces
 *
 * @example
 *   {"gfm": true, "label": "input", "name": "tab.md"}
 *
 *   - [ ]␉Mercury.
 *   + [x]␉␉Venus.
 * @example
 *   {"gfm": true, "label": "output", "name": "tab.md"}
 *
 *   2:8: Unexpected `2` spaces between checkbox and content, expected `1` space, remove `1` space
 */
const remarkLintCheckboxContentIndent = lintRule$1(
  {
    origin: 'remark-lint:checkbox-content-indent',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-checkbox-content-indent#readme'
  },
  function (tree, file) {
    const value = String(file);
    visitParents(tree, function (node, parents) {
      if (phrasing(node)) {
        return SKIP
      }
      if (node.type !== 'listItem') return
      const head = node.children[0];
      const headStart = pointStart(head);
      if (
        !head ||
        !headStart ||
        typeof node.checked !== 'boolean' ||
        typeof headStart.offset !== 'number'
      ) {
        return
      }
      const match = /\[([\t xX])]/.exec(
        value.slice(headStart.offset - 4, headStart.offset + 1)
      );
      if (!match) return
      let final = headStart.offset;
      let code = value.charCodeAt(final);
      while (code === 9 || code === 32) {
        final++;
        code = value.charCodeAt(final);
      }
      const size = final - headStart.offset;
      if (size) {
        file.message(
          'Unexpected `' +
            (size + 1) +
            '` ' +
            pluralize('space', size + 1) +
            ' between checkbox and content, expected `1` space, remove `' +
            size +
            '` ' +
            pluralize('space', size),
          {
            ancestors: [...parents, node],
            place: {
              line: headStart.line,
              column: headStart.column + size,
              offset: headStart.offset + size
            }
          }
        );
      }
    });
  }
);

/**
 * remark-lint rule to warn when code blocks violate a given style.
 *
 * ## What is this?
 *
 * This package checks the style of code blocks.
 *
 * ## When should I use this?
 *
 * You can use this package to check that the style of code blocks is
 * consistent.
 *
 * ## API
 *
 * ### `unified().use(remarkLintCodeBlockStyle[, options])`
 *
 * Warn when code blocks violate a given style.
 *
 * ###### Parameters
 *
 * * `options` ([`Options`][api-options], default: `'consistent'`)
 *   — preferred style or whether to detect the first style and warn for
 *   further differences
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ### `Options`
 *
 * Configuration (TypeScript type).
 *
 * ###### Type
 *
 * ```ts
 * type Options = Style | 'consistent'
 * ```
 *
 * ### `Style`
 *
 * Style (TypeScript type).
 *
 * ###### Type
 *
 * ```ts
 * type Style = 'indented' | 'fenced'
 * ```
 *
 * ## Recommendation
 *
 * Indentation in markdown is complex as lists and indented code interfere in
 * unexpected ways.
 * Fenced code has more features than indented code: it can specify a
 * programming language.
 * Since CommonMark took the idea of fenced code from GFM,
 * fenced code became widely supported.
 * Due to this, it’s recommended to configure this rule with `'fenced'`.
 *
 * ## Fix
 *
 * [`remark-stringify`][github-remark-stringify] always formats code blocks as
 * fenced.
 * Pass `fences: false` to only use fenced code blocks when they have a
 * language and as indented code otherwise.
 *
 * [api-options]: #options
 * [api-remark-lint-code-block-style]: #unifieduseremarklintcodeblockstyle-options
 * [api-style]: #style
 * [github-remark-stringify]: https://github.com/remarkjs/remark/tree/main/packages/remark-stringify
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module code-block-style
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"config": "indented", "name": "ok-indented.md"}
 *
 *       venus()
 *
 *   Mercury.
 *
 *       earth()
 *
 * @example
 *   {"config": "fenced", "name": "ok-fenced.md"}
 *
 *   ```
 *   venus()
 *   ```
 *
 *   Mercury.
 *
 *   ```
 *   earth()
 *   ```
 *
 * @example
 *   {"label": "input", "name": "not-ok-consistent.md"}
 *
 *       venus()
 *
 *   Mercury.
 *
 *   ```
 *   earth()
 *   ```
 * @example
 *   {"label": "output", "name": "not-ok-consistent.md"}
 *
 *   5:1-7:4: Unexpected fenced code block, expected indented code blocks
 *
 * @example
 *   {"config": "indented", "label": "input", "name": "not-ok-indented.md"}
 *
 *   ```
 *   venus()
 *   ```
 *
 *   Mercury.
 *
 *   ```
 *   earth()
 *   ```
 * @example
 *   {"config": "indented", "label": "output", "name": "not-ok-indented.md"}
 *
 *   1:1-3:4: Unexpected fenced code block, expected indented code blocks
 *   7:1-9:4: Unexpected fenced code block, expected indented code blocks
 *
 * @example
 *   {"config": "fenced", "label": "input", "name": "not-ok-fenced.md"}
 *
 *       venus()
 *
 *   Mercury.
 *
 *       earth()
 *
 * @example
 *   {"config": "fenced", "label": "output", "name": "not-ok-fenced.md"}
 *
 *   1:1-1:12: Unexpected indented code block, expected fenced code blocks
 *   5:1-5:12: Unexpected indented code block, expected fenced code blocks
 *
 * @example
 *   {"config": "🌍", "label": "output", "name": "not-ok-options.md", "positionless": true}
 *
 *   1:1: Unexpected value `🌍` for `options`, expected `'fenced'`, `'indented'`, or `'consistent'`
 */
const remarkLintCodeBlockStyle = lintRule$1(
  {
    origin: 'remark-lint:code-block-style',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-code-block-style#readme'
  },
  function (tree, file, options) {
    const value = String(file);
    let cause;
    let expected;
    if (options === null || options === undefined || options === 'consistent') ; else if (options === 'indented' || options === 'fenced') {
      expected = options;
    } else {
      file.fail(
        'Unexpected value `' +
          options +
          "` for `options`, expected `'fenced'`, `'indented'`, or `'consistent'`"
      );
    }
    visitParents(tree, function (node, parents) {
      if (phrasing(node)) {
        return SKIP
      }
      if (node.type !== 'code') return
      const end = pointEnd(node);
      const start = pointStart(node);
      if (
        !start ||
        !end ||
        typeof start.offset !== 'number' ||
        typeof end.offset !== 'number'
      ) {
        return
      }
      const actual =
        node.lang || /^ {0,3}([`~])/.test(value.slice(start.offset, end.offset))
          ? 'fenced'
          : 'indented';
      if (expected) {
        if (expected !== actual) {
          file.message(
            'Unexpected ' +
              actual +
              ' code block, expected ' +
              expected +
              ' code blocks',
            {ancestors: [...parents, node], cause, place: {start, end}}
          );
        }
      } else {
        expected = actual;
        cause = new VFileMessage(
          "Code block style `'" +
            actual +
            "'` first defined for `'consistent'` here",
          {
            ancestors: [...parents, node],
            place: {start, end},
            source: 'remark-lint',
            ruleId: 'code-block-style'
          }
        );
      }
    });
  }
);

/**
 * remark-lint rule to warn when consecutive whitespace is used in
 * a definition label.
 *
 * ## What is this?
 *
 * This package checks the whitepsace in definition labels.
 *
 * GFM footnotes are not affected by this rule as footnote labels cannot
 * contain whitespace.
 *
 * ## When should I use this?
 *
 * You can use this package to check that definition labels are consistent.
 *
 * ## API
 *
 * ### `unified().use(remarkLintDefinitionSpacing)`
 *
 * Warn when consecutive whitespace is used in a definition label.
 *
 * ###### Parameters
 *
 * There are no options.
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ## Recommendation
 *
 * Definitions and references are matched together by collapsing whitespace.
 * Using more whitespace in labels might incorrectly indicate that they are of
 * importance.
 * Due to this, it’s recommended to use one space and turn this rule on.
 *
 * [api-remark-lint-definition-spacing]: #unifieduseremarklintdefinitionspacing
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module definition-spacing
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"name": "ok.md"}
 *
 *   The first planet is [planet mercury][].
 *
 *   [planet mercury]: http://example.com
 *
 * @example
 *   {"label": "input", "name": "not-ok-consecutive.md"}
 *
 *   [planet␠␠␠␠mercury]: http://example.com
 * @example
 *   {"label": "output", "name": "not-ok-consecutive.md"}
 *
 *   1:1-1:40: Unexpected `4` consecutive spaces in definition label, expected `1` space, remove `3` spaces
 *
 * @example
 *   {"label": "input", "name": "not-ok-non-space.md"}
 *
 *   [pla␉net␊mer␍cury]: http://e.com
 * @example
 *   {"label": "output", "name": "not-ok-non-space.md"}
 *
 *   1:1-3:20: Unexpected non-space whitespace character `\t` in definition label, expected `1` space, replace it
 *   1:1-3:20: Unexpected non-space whitespace character `\n` in definition label, expected `1` space, replace it
 *   1:1-3:20: Unexpected non-space whitespace character `\r` in definition label, expected `1` space, replace it
 */
const remarkLintDefinitionSpacing = lintRule$1(
  {
    origin: 'remark-lint:definition-spacing',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-definition-spacing#readme'
  },
  function (tree, file) {
    visitParents(tree, function (node, parents) {
      if (phrasing(node)) {
        return SKIP
      }
      if (node.type === 'definition' && node.position && node.label) {
        const size = longestStreak(node.label, ' ');
        if (size > 1) {
          file.message(
            'Unexpected `' +
              size +
              '` consecutive spaces in definition label, expected `1` space, remove `' +
              (size - 1) +
              '` ' +
              pluralize('space', size - 1),
            {ancestors: [...parents, node], place: node.position}
          );
        }
        const disallowed = [];
        if (node.label.includes('\t')) disallowed.push('\\t');
        if (node.label.includes('\n')) disallowed.push('\\n');
        if (node.label.includes('\r')) disallowed.push('\\r');
        for (const disallow of disallowed) {
          file.message(
            'Unexpected non-space whitespace character `' +
              disallow +
              '` in definition label, expected `1` space, replace it',
            {ancestors: [...parents, node], place: node.position}
          );
        }
      }
    });
  }
);

const quotation =
  (
    function (value, open, close) {
      const start = open ;
      const end = start;
      let index = -1;
      if (Array.isArray(value)) {
        const list =  (value);
        const result = [];
        while (++index < list.length) {
          result[index] = start + list[index] + end;
        }
        return result
      }
      if (typeof value === 'string') {
        return start + value + end
      }
      throw new TypeError('Expected string or array of strings')
    }
  );

/**
 * remark-lint rule to warn when language flags of fenced code
 * are not used.
 *
 * ## What is this?
 *
 * This package checks the language flags of fenced code blocks,
 * whether they exist,
 * and optionally what values they hold.
 *
 * ## When should I use this?
 *
 * You can use this package to check that the style of language flags of fenced
 * code blocks is consistent.
 *
 * ## API
 *
 * ### `unified().use(remarkLintFencedCodeFlag[, options])`
 *
 * Warn when language flags of fenced code are not used.
 *
 * ###### Parameters
 *
 * * `options` ([`Options`][api-options] or `Array<string>`, optional)
 *   — configuration or flags to allow
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ### `Options`
 *
 * Configuration (TypeScript type).
 *
 * ###### Fields
 *
 * * `allowEmpty` (`boolean`, default: `false`)
 *   — allow language flags to be omitted
 * * `flags` (`Array<string>`, optional)
 *   — flags to allow,
 *   other flags will result in a warning
 *
 * ## Recommendation
 *
 * While omitting language flags is fine to signal that code is plain text,
 * it *could* point to a mistake.
 * It’s recommended to instead use a certain flag for plain text (such as
 * `txt`) and to turn this rule on.
 *
 * [api-options]: #options
 * [api-remark-lint-fenced-code-flag]: #unifieduseremarklintfencedcodeflag-options
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module fenced-code-flag
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"name": "ok.md"}
 *
 *   Some markdown:
 *
 *   ```markdown
 *   # Mercury
 *   ```
 *
 * @example
 *   {"label": "input", "name": "not-ok.md"}
 *
 *   ```
 *   mercury()
 *   ```
 * @example
 *   {"label": "output", "name": "not-ok.md"}
 *
 *   1:1-3:4: Unexpected missing fenced code language flag in info string, expected keyword
 *
 * @example
 *   {"config": {"allowEmpty": true}, "name": "ok-allow-empty.md"}
 *
 *   ```
 *   mercury()
 *   ```
 *
 * @example
 *   {"config": {"allowEmpty": false}, "label": "input", "name": "not-ok-allow-empty.md"}
 *
 *   ```
 *   mercury()
 *   ```
 * @example
 *   {"config": {"allowEmpty": false}, "label": "output", "name": "not-ok-allow-empty.md"}
 *
 *   1:1-3:4: Unexpected missing fenced code language flag in info string, expected keyword
 *
 * @example
 *   {"config": ["markdown"], "name": "ok-array.md"}
 *
 *   ```markdown
 *   # Mercury
 *   ```
 *
 * @example
 *   {"config": {"flags":["markdown"]}, "name": "ok-options.md"}
 *
 *   ```markdown
 *   # Mercury
 *   ```
 *
 * @example
 *   {"config": ["markdown"], "label": "input", "name": "not-ok-array.md"}
 *
 *   ```javascript
 *   mercury()
 *   ```
 * @example
 *   {"config": ["markdown"], "label": "output", "name": "not-ok-array.md"}
 *
 *   1:1-3:4: Unexpected fenced code language flag `javascript` in info string, expected `markdown`
 *
 * @example
 *   {"config": ["javascript", "markdown", "mdx", "typescript"], "label": "input", "name": "not-ok-long-array.md"}
 *
 *   ```html
 *   <h1>Mercury</h1>
 *   ```
 * @example
 *   {"config": ["javascript", "markdown", "mdx", "typescript"], "label": "output", "name": "not-ok-long-array.md"}
 *
 *   1:1-3:4: Unexpected fenced code language flag `html` in info string, expected `javascript`, `markdown`, `mdx`, …
 *
 * @example
 *   {"config": "🌍", "label": "output", "name": "not-ok-options.md", "positionless": true}
 *
 *   1:1: Unexpected value `🌍` for `options`, expected array or object
 */
const fence = /^ {0,3}([~`])\1{2,}/;
const listFormat$1 = new Intl.ListFormat('en', {type: 'disjunction'});
const listFormatUnit$1 = new Intl.ListFormat('en', {type: 'unit'});
const remarkLintFencedCodeFlag = lintRule$1(
  {
    origin: 'remark-lint:fenced-code-flag',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-fenced-code-flag#readme'
  },
  function (tree, file, options) {
    const value = String(file);
    let allowEmpty = false;
    let allowed;
    if (options === null || options === undefined) ; else if (typeof options === 'object') {
      if (Array.isArray(options)) {
        const flags =  (options);
        allowed = flags;
      } else {
        const settings =  (options);
        allowEmpty = settings.allowEmpty === true;
        if (settings.flags) {
          allowed = settings.flags;
        }
      }
    } else {
      file.fail(
        'Unexpected value `' +
          options +
          '` for `options`, expected array or object'
      );
    }
    let allowedDisplay;
    if (allowed) {
      allowedDisplay =
        allowed.length > 3
          ? listFormatUnit$1.format([...quotation(allowed.slice(0, 3), '`'), '…'])
          : listFormat$1.format(quotation(allowed, '`'));
    } else {
      allowedDisplay = 'keyword';
    }
    visitParents(tree, function (node, parents) {
      if (phrasing(node)) {
        return SKIP
      }
      if (node.type !== 'code') return
      const end = pointEnd(node);
      const start = pointStart(node);
      if (
        end &&
        start &&
        typeof end.offset === 'number' &&
        typeof start.offset === 'number'
      ) {
        if (node.lang) {
          if (allowed && !allowed.includes(node.lang)) {
            file.message(
              'Unexpected fenced code language flag `' +
                node.lang +
                '` in info string, expected ' +
                allowedDisplay,
              {ancestors: [...parents, node], place: node.position}
            );
          }
        } else if (!allowEmpty) {
          const slice = value.slice(start.offset, end.offset);
          if (fence.test(slice)) {
            file.message(
              'Unexpected missing fenced code language flag in info string, expected ' +
                allowedDisplay,
              {ancestors: [...parents, node], place: node.position}
            );
          }
        }
      }
    });
  }
);

/**
 * remark-lint rule to warn when fenced code markers are
 * inconsistent.
 *
 * ## What is this?
 *
 * This package checks fenced code block markers.
 *
 * ## When should I use this?
 *
 * You can use this package to check that fenced code block markers are
 * consistent.
 *
 * ## API
 *
 * ### `unified().use(remarkLintFencedCodeMarker[, options])`
 *
 * Warn when fenced code markers are inconsistent.
 *
 * ###### Parameters
 *
 * * `options` ([`Options`][api-options], default: `'consistent'`)
 *   — preferred style or whether to detect the first style and warn for
 *   further differences
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ### `Marker`
 *
 * Marker (TypeScript type).
 *
 * ###### Type
 *
 * ```ts
 * type Marker = '`' | '~'
 * ```
 *
 * ### `Options`
 *
 * Configuration (TypeScript type).
 *
 * ###### Type
 *
 * ```ts
 * type Options = Marker | 'consistent'
 * ```
 *
 * ## Recommendation
 *
 * Tildes are uncommon.
 * So it’s recommended to configure this rule with ``'`'``.
 *
 * ## Fix
 *
 * [`remark-stringify`][github-remark-stringify] formats fenced code with grave
 * accents by default.
 * Pass `fence: '~'` to always use tildes.
 *
 * [api-marker]: #marker
 * [api-options]: #options
 * [api-remark-lint-fenced-code-marker]: #unifieduseremarklintfencedcodemarker-options
 * [github-remark-stringify]: https://github.com/remarkjs/remark/tree/main/packages/remark-stringify
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module fenced-code-marker
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"name": "ok-indented.md"}
 *
 *   Indented code blocks are not affected by this rule:
 *
 *       mercury()
 *
 * @example
 *   {"config": "`", "name": "ok-tick.md"}
 *
 *   ```javascript
 *   mercury()
 *   ```
 *
 *   ```
 *   venus()
 *   ```
 *
 * @example
 *   {"config": "~", "name": "ok-tilde.md"}
 *
 *   ~~~javascript
 *   mercury()
 *   ~~~
 *
 *   ~~~
 *   venus()
 *   ~~~
 *
 * @example
 *   {"label": "input", "name": "not-ok-consistent-tick.md"}
 *
 *   ```javascript
 *   mercury()
 *   ```
 *
 *   ~~~
 *   venus()
 *   ~~~
 * @example
 *   {"label": "output", "name": "not-ok-consistent-tick.md"}
 *
 *   5:1-7:4: Unexpected fenced code marker `~`, expected `` ` ``
 *
 * @example
 *   {"label": "input", "name": "not-ok-consistent-tilde.md"}
 *
 *   ~~~javascript
 *   mercury()
 *   ~~~
 *
 *   ```
 *   venus()
 *   ```
 * @example
 *   {"label": "output", "name": "not-ok-consistent-tilde.md"}
 *
 *   5:1-7:4: Unexpected fenced code marker `` ` ``, expected `~`
 *
 * @example
 *   {"config": "🌍", "label": "output", "name": "not-ok-incorrect.md", "positionless": true}
 *
 *   1:1: Unexpected value `🌍` for `options`, expected ``'`'``, `'~'`, or `'consistent'`
 */
const remarkLintFencedCodeMarker = lintRule$1(
  {
    origin: 'remark-lint:fenced-code-marker',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-fenced-code-marker#readme'
  },
  function (tree, file, options) {
    const value = String(file);
    let cause;
    let expected;
    if (options === null || options === undefined || options === 'consistent') ; else if (options === '`' || options === '~') {
      expected = options;
    } else {
      file.fail(
        'Unexpected value `' +
          options +
          "` for `options`, expected ``'`'``, `'~'`, or `'consistent'`"
      );
    }
    visitParents(tree, function (node, parents) {
      if (phrasing(node)) {
        return SKIP
      }
      if (node.type !== 'code') return
      const start = pointStart(node);
      if (start && typeof start.offset === 'number') {
        const actual = value
          .slice(start.offset, start.offset + 4)
          .replace(/^\s+/, '')
          .charAt(0);
        if (actual !== '`' && actual !== '~') return
        if (expected) {
          if (actual !== expected) {
            file.message(
              'Unexpected fenced code marker ' +
                (actual === '~' ? '`~`' : '`` ` ``') +
                ', expected ' +
                (expected === '~' ? '`~`' : '`` ` ``'),
              {ancestors: [...parents, node], cause, place: node.position}
            );
          }
        } else {
          expected = actual;
          cause = new VFileMessage(
            'Fenced code marker style ' +
              (actual === '~' ? "`'~'`" : "``'`'``") +
              " first defined for `'consistent'` here",
            {
              ancestors: [...parents, node],
              place: node.position,
              ruleId: 'fenced-code-marker',
              source: 'remark-lint'
            }
          );
        }
      }
    });
  }
);

/**
 * remark-lint rule to warn for unexpected file extensions.
 *
 * ## What is this?
 *
 * This package checks the file extension.
 *
 * ## When should I use this?
 *
 * You can use this package to check that file extensions are consistent.
 *
 * ## API
 *
 * ### `unified().use(remarkLintFileExtension[, options])`
 *
 * Warn for unexpected extensions.
 *
 * ###### Parameters
 *
 * * `options` ([`Extensions`][api-extensions] or [`Options`][api-options],
 *   optional)
 *   — configuration
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ### `Extensions`
 *
 * File extension(s) (TypeScript type).
 *
 * ###### Type
 *
 * ```ts
 * type Extensions = Array<string> | string
 * ```
 *
 * ### `Options`
 *
 * Configuration (TypeScript type).
 *
 * ###### Fields
 *
 * * `allowExtensionless` (`boolean`, default: `true`)
 *   — allow no file extension such as `AUTHORS` or `LICENSE`
 * * `extensions` ([`Extensions`][api-extensions], default: `['mdx', 'md']`)
 *   — allowed file extension(s)
 *
 * ## Recommendation
 *
 * Use `md` as it’s the most common.
 * Also use `md` when your markdown contains common syntax extensions (such as
 * GFM, frontmatter, or math).
 * Do not use `md` for MDX: use `mdx` instead.
 *
 * [api-extensions]: #extensions
 * [api-options]: #options
 * [api-remark-lint-file-extension]: #unifieduseremarklintfileextension-options
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module file-extension
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"name": "readme.md"}
 *
 * @example
 *   {"name": "readme.mdx"}
 *
 * @example
 *   {"name": "readme"}
 *
 * @example
 *   {"config": {"allowExtensionless": false}, "label": "output", "name": "readme", "positionless": true}
 *
 *   1:1: Unexpected missing file extension, expected `mdx` or `md`
 *
 * @example
 *   {"label": "output", "name": "readme.mkd", "positionless": true}
 *
 *   1:1: Unexpected file extension `mkd`, expected `mdx` or `md`
 *
 * @example
 *   {"config": "mkd", "name": "readme.mkd"}
 *
 * @example
 *   {"config": ["markdown", "md", "mdown", "mdwn", "mdx", "mkd", "mkdn", "mkdown", "ron"], "label": "input", "name": "readme.css", "positionless": true}
 *
 * @example
 *   {"config": ["markdown", "md", "mdown", "mdwn", "mdx", "mkd", "mkdn", "mkdown", "ron"], "label": "output", "name": "readme.css"}
 *
 *   1:1: Unexpected file extension `css`, expected `markdown`, `md`, `mdown`, …
 */
const defaultExtensions = ['mdx', 'md'];
const listFormat = new Intl.ListFormat('en', {type: 'disjunction'});
const listFormatUnit = new Intl.ListFormat('en', {type: 'unit'});
const remarkLintFileExtension = lintRule$1(
  {
    origin: 'remark-lint:file-extension',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-file-extension#readme'
  },
  function (_, file, options) {
    let expected = defaultExtensions;
    let allowExtensionless = true;
    let extensionsValue;
    if (Array.isArray(options)) {
      extensionsValue =  (options);
    } else if (typeof options === 'string') {
      extensionsValue = options;
    } else if (options) {
      const settings =  (options);
      extensionsValue = settings.extensions;
      if (settings.allowExtensionless === false) {
        allowExtensionless = false;
      }
    }
    if (Array.isArray(extensionsValue)) {
      expected =  (extensionsValue);
    } else if (typeof extensionsValue === 'string') {
      expected = [extensionsValue];
    }
    const extname = file.extname;
    const actual = extname ? extname.slice(1) : undefined;
    const expectedDisplay =
      expected.length > 3
        ? listFormatUnit.format([...quotation(expected.slice(0, 3), '`'), '…'])
        : listFormat.format(quotation(expected, '`'));
    if (actual ? !expected.includes(actual) : !allowExtensionless) {
      file.message(
        (actual
          ? 'Unexpected file extension `' + actual + '`'
          : 'Unexpected missing file extension') +
          ', expected ' +
          expectedDisplay
      );
    }
  }
);

/**
 * remark-lint rule to warn when definitions are used *in* the
 * document instead of at the end.
 *
 * ## What is this?
 *
 * This package checks where definitions are placed.
 *
 * ## When should I use this?
 *
 * You can use this package to check that definitions are consistently at the
 * end of the document.
 *
 * ## API
 *
 * ### `unified().use(remarkLintFinalDefinition)`
 *
 * Warn when definitions are used *in* the document instead of at the end.
 *
 * ###### Parameters
 *
 * There are no options.
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ## Recommendation
 *
 * There are different strategies for placing definitions.
 * The simplest is perhaps to place them all at the bottem of documents.
 * If you prefer that, turn on this rule.
 *
 * [api-remark-lint-final-definition]: #unifieduseremarklintfinaldefinition
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module final-definition
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"name": "ok.md"}
 *
 *   Mercury.
 *
 *   [venus]: http://example.com
 *
 * @example
 *   {"name": "ok.md"}
 *
 *   [mercury]: http://example.com/mercury/
 *   [venus]: http://example.com/venus/
 *
 * @example
 *   {"name": "ok-html-comments.md"}
 *
 *   Mercury.
 *
 *   [venus]: http://example.com/venus/
 *
 *   <!-- HTML comments in markdown are ignored. -->
 *
 *   [earth]: http://example.com/earth/
 *
 * @example
 *   {"name": "ok-mdx-comments.mdx", "mdx": true}
 *
 *   Mercury.
 *
 *   [venus]: http://example.com/venus/
 *
 *   {/* Comments in expressions in MDX are ignored. *␀/}
 *
 *   [earth]: http://example.com/earth/
 *
 * @example
 *   {"label": "input", "name": "not-ok.md"}
 *
 *   Mercury.
 *
 *   [venus]: https://example.com/venus/
 *
 *   Earth.
 * @example
 *   {"label": "output", "name": "not-ok.md"}
 *
 *   3:1-3:36: Unexpected definition before last content, expected definitions after line `5`
 *
 * @example
 *   {"gfm": true, "label": "input", "name": "gfm-nok.md"}
 *
 *   Mercury.
 *
 *   [^venus]:
 *       **Venus** is the second planet from
 *       the Sun.
 *
 *   Earth.
 * @example
 *   {"gfm": true, "label": "output", "name": "gfm-nok.md"}
 *
 *   3:1-5:13: Unexpected footnote definition before last content, expected definitions after line `7`
 *
 * @example
 *   {"gfm": true, "name": "gfm-ok.md"}
 *
 *   Mercury.
 *
 *   Earth.
 *
 *   [^venus]:
 *       **Venus** is the second planet from
 *       the Sun.
 */
const remarkLintFinalDefinition = lintRule$1(
  {
    origin: 'remark-lint:final-definition',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-final-definition#readme'
  },
  function (tree, file) {
    const definitionStacks = [];
    let contentAncestors;
    visitParents(tree, function (node, parents) {
      if (phrasing(node)) {
        return SKIP
      }
      if (node.type === 'definition' || node.type === 'footnoteDefinition') {
        definitionStacks.push([...parents, node]);
        return SKIP
      }
      if (
        node.type === 'root' ||
        (node.type === 'html' && /^[\t ]*<!--/.test(node.value)) ||
        (node.type === 'mdxFlowExpression' && /^\s*\/\*/.test(node.value))
      ) {
        return
      }
      contentAncestors = [...parents, node];
    });
    const content = contentAncestors ? contentAncestors.at(-1) : undefined;
    const contentEnd = pointEnd(content);
    if (contentEnd) {
      for (const definitionAncestors of definitionStacks) {
        const definition = definitionAncestors.at(-1);
        const definitionStart = pointStart(definition);
        if (definitionStart && definitionStart.line < contentEnd.line) {
          file.message(
            'Unexpected ' +
              (definition.type === 'footnoteDefinition' ? 'footnote ' : '') +
              'definition before last content, expected definitions after line `' +
              contentEnd.line +
              '`',
            {
              ancestors: definitionAncestors,
              cause: new VFileMessage('Last content defined here', {
                ancestors: contentAncestors,
                place: content.position,
                ruleId: 'final-definition',
                source: 'remark-lint'
              }),
              place: definition.position
            }
          );
        }
      }
    }
  }
);

/**
 * remark-lint rule to warn when the first heading has an unexpected rank.
 *
 * ## What is this?
 *
 * This package checks the rank of the first heading.
 *
 * ## When should I use this?
 *
 * You can use this package to check that the rank of first headings is
 * consistent.
 *
 * ## API
 *
 * ### `unified().use(remarkLintFirstHeadingLevel[, options])`
 *
 * Warn when the first heading has an unexpected rank.
 *
 * ###### Parameters
 *
 * * `options` ([`Options`][api-options], default: `1`)
 *   — configuration
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ### `Options`
 *
 * Configuration (TypeScript type).
 *
 * ###### Type
 *
 * ```ts
 * type Options = 1 | 2 | 3 | 4 | 5 | 6
 * ```
 *
 * ## Recommendation
 *
 * In most cases you’d want to first heading in a markdown document to start at
 * rank `1`.
 * In some cases a different rank makes more sense,
 * such as when building a blog and generating the primary heading from
 * frontmatter metadata,
 * in which case a value of `2` can be defined here or the rule can be turned
 * off.
 *
 * [api-options]: #options
 * [api-remark-lint-first-heading-level]: #unifieduseremarklintfirstheadinglevel-options
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module first-heading-level
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"name": "ok.md"}
 *
 *   # Mercury
 *
 * @example
 *   {"name": "ok-delay.md"}
 *
 *   Mercury.
 *
 *   # Venus
 *
 * @example
 *   {"label": "input", "name": "not-ok.md"}
 *
 *   ## Mercury
 *
 *   Venus.
 * @example
 *   {"label": "output", "name": "not-ok.md"}
 *
 *   1:1-1:11: Unexpected first heading rank `2`, expected rank `1`
 *
 * @example
 *   {"config": 2, "name": "ok.md"}
 *
 *   ## Mercury
 *
 *   Venus.
 *
 * @example
 *   {"name": "ok-html.md"}
 *
 *   <div>Mercury.</div>
 *
 *   <h1>Venus</h1>
 *
 * @example
 *   {"mdx": true, "name": "ok-mdx.mdx"}
 *
 *   <div>Mercury.</div>
 *
 *   <h1>Venus</h1>
 *
 * @example
 *   {"config": "🌍", "label": "output", "name": "not-ok-options.md", "positionless": true}
 *
 *   1:1: Unexpected value `🌍` for `options`, expected `1`, `2`, `3`, `4`, `5`, or `6`
 */
const htmlRe$1 = /<h([1-6])/;
const jsxNameRe$1 = /^h([1-6])$/;
const remarkLintFirstHeadingLevel = lintRule$1(
  {
    origin: 'remark-lint:first-heading-level',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-first-heading-level#readme'
  },
  function (tree, file, options) {
    let expected;
    if (options === null || options === undefined) {
      expected = 1;
    } else if (
      options === 1 ||
      options === 2 ||
      options === 3 ||
      options === 4 ||
      options === 5 ||
      options === 6
    ) {
      expected = options;
    } else {
      file.fail(
        'Unexpected value `' +
          options +
          '` for `options`, expected `1`, `2`, `3`, `4`, `5`, or `6`'
      );
    }
    visitParents(tree, function (node, parents) {
      let actual;
      if (node.type === 'heading') {
        actual = node.depth;
      } else if (node.type === 'html') {
        const results = node.value.match(htmlRe$1);
        actual = results
          ?  (Number(results[1]))
          : undefined;
      } else if (
        (node.type === 'mdxJsxFlowElement' ||
          node.type === 'mdxJsxTextElement') &&
        node.name
      ) {
        const results = node.name.match(jsxNameRe$1);
        actual = results
          ?  (Number(results[1]))
          : undefined;
      }
      if (actual && node.position) {
        if (node.position && actual !== expected) {
          file.message(
            'Unexpected first heading rank `' +
              actual +
              '`, expected rank `' +
              expected +
              '`',
            {ancestors: [...parents, node], place: node.position}
          );
        }
        return EXIT
      }
    });
  }
);

function headingStyle(node, relative) {
  const last = node.children[node.children.length - 1];
  const depth = node.depth;
  const pos = node.position && node.position.end;
  const final = last && last.position && last.position.end;
  if (!pos) {
    return undefined
  }
  if (!last) {
    if (pos.column - 1 <= depth * 2) {
      return consolidate(depth, relative)
    }
    return 'atx-closed'
  }
  if (final && final.line + 1 === pos.line) {
    return 'setext'
  }
  if (final && final.column + depth < pos.column) {
    return 'atx-closed'
  }
  return consolidate(depth, relative)
}
function consolidate(depth, relative) {
  return depth < 3
    ? 'atx'
    : relative === 'atx' || relative === 'setext'
    ? relative
    : undefined
}

/**
 * remark-lint rule to warn when headings violate a given style.
 *
 * ## What is this?
 *
 * This package checks the style of headings.
 *
 * ## When should I use this?
 *
 * You can use this package to check that the style of headings is consistent.
 *
 * ## API
 *
 * ### `unified().use(remarkLintHeadingStyle[, options])`
 *
 * Warn when headings violate a given style.
 *
 * ###### Parameters
 *
 * * `options` ([`Options`][api-options], default: `'consistent'`)
 *   — preferred style or whether to detect the first style and warn for
 *   further differences
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ### `Options`
 *
 * Configuration (TypeScript type).
 *
 * ###### Type
 *
 * ```ts
 * type Options = Style | 'consistent'
 * ```
 *
 * ### `Style`
 *
 * Style (TypeScript type).
 *
 * ###### Type
 *
 * ```ts
 * type Style = 'atx' | 'atx-closed' | 'setext'
 * ```
 *
 * ## Recommendation
 *
 * Setext headings are limited in that they can only construct headings with a
 * rank of one and two.
 * They do allow multiple lines of content where ATX only allows one line.
 * The number of used markers in their underline does not matter,
 * leading to either:
 *
 * * 1 marker (`Hello\n-`),
 *   which is the bare minimum,
 *   and for rank 2 headings looks suspiciously like an empty list item
 * * using as many markers as the content (`Hello\n-----`),
 *   which is hard to maintain and diff
 * * an arbitrary number (`Hello\n---`), which for rank 2 headings looks
 *   suspiciously like a thematic break
 *
 * Setext headings are also uncommon.
 * Using a sequence of hashes at the end of ATX headings is even more uncommon.
 * Due to this,
 * it’s recommended to use ATX headings, without closing hashes.
 *
 * ## Fix
 *
 * [`remark-stringify`][github-remark-stringify] formats headings as ATX by default.
 * The other styles can be configured with `setext: true` or `closeAtx: true`.
 *
 * [api-options]: #options
 * [api-remark-lint-heading-style]: #unifieduseremarklintheadingstyle-options
 * [api-style]: #style
 * [github-remark-stringify]: https://github.com/remarkjs/remark/tree/main/packages/remark-stringify
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module heading-style
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"config": "atx", "name": "ok.md"}
 *
 *   # Mercury
 *
 *   ## Venus
 *
 *   ### Earth
 *
 * @example
 *   {"config": "atx-closed", "name": "ok.md"}
 *
 *   # Mercury ##
 *
 *   ## Venus ##
 *
 *   ### Earth ###
 *
 * @example
 *   {"config": "setext", "name": "ok.md"}
 *
 *   Mercury
 *   =======
 *
 *   Venus
 *   -----
 *
 *   ### Earth
 *
 * @example
 *   {"label": "input", "name": "not-ok.md"}
 *
 *   Mercury
 *   =======
 *
 *   ## Venus
 *
 *   ### Earth ###
 * @example
 *   {"label": "output", "name": "not-ok.md"}
 *
 *   4:1-4:9: Unexpected ATX heading, expected setext
 *   6:1-6:14: Unexpected ATX (closed) heading, expected setext
 *
 * @example
 *   {"config": "🌍", "label": "output", "name": "not-ok.md", "positionless": true}
 *
 *   1:1: Unexpected value `🌍` for `options`, expected `'atx'`, `'atx-closed'`, `'setext'`, or `'consistent'`
 */
const remarkLintHeadingStyle = lintRule$1(
  {
    origin: 'remark-lint:heading-style',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-heading-style#readme'
  },
  function (tree, file, options) {
    let cause;
    let expected;
    if (options === null || options === undefined || options === 'consistent') ; else if (
      options === 'atx' ||
      options === 'atx-closed' ||
      options === 'setext'
    ) {
      expected = options;
    } else {
      file.fail(
        'Unexpected value `' +
          options +
          "` for `options`, expected `'atx'`, `'atx-closed'`, `'setext'`, or `'consistent'`"
      );
    }
    visitParents(tree, function (node, parents) {
      if (phrasing(node)) {
        return SKIP
      }
      if (node.type !== 'heading') return
      const place = position(node);
      const actual = headingStyle(node, expected);
      if (actual) {
        if (expected) {
          if (place && actual !== expected) {
            file.message(
              'Unexpected ' +
                displayStyle(actual) +
                ' heading, expected ' +
                displayStyle(expected),
              {ancestors: [...parents, node], cause, place}
            );
          }
        } else {
          expected = actual;
          cause = new VFileMessage(
            'Heading style ' +
              displayStyle(expected) +
              " first defined for `'consistent'` here",
            {
              ancestors: [...parents, node],
              place,
              ruleId: 'heading-style',
              source: 'remark-lint'
            }
          );
        }
      }
    });
  }
);
function displayStyle(style) {
  return style === 'atx'
    ? 'ATX'
    : style === 'atx-closed'
      ? 'ATX (closed)'
      : 'setext'
}

/**
 * remark-lint rule to warn when lines are too long.
 *
 * ## What is this?
 *
 * This package checks the length of lines.
 *
 * ## When should I use this?
 *
 * You can use this package to check that lines are within reason.
 *
 * ## API
 *
 * ### `unified().use(remarkLintMaximumLineLength[, options])`
 *
 * Warn when lines are too long.
 *
 * Nodes that cannot be wrapped are ignored, such as JSX, HTML, code (flow),
 * definitions, headings, and tables.
 *
 * When code (phrasing), images, and links start before the wrap,
 * end after the wrap,
 * and contain no whitespace,
 * they are also ignored.
 *
 * ###### Parameters
 *
 * * `options` (`number`, default: `80`)
 *   — preferred max size
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ## Recommendation
 *
 * Whether to wrap prose or not is a stylistic choice.
 *
 * [api-remark-lint-maximum-line-length]: #unifieduseremarklintmaximumlinelength-options
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module maximum-line-length
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"name": "ok.md", "positionless": true}
 *
 *   Mercury mercury mercury mercury mercury mercury mercury mercury mercury mercury
 *   mercury.
 *
 *   Mercury mercury mercury mercury mercury mercury mercury mercury mercury `mercury()`.
 *
 *   Mercury mercury mercury mercury mercury mercury mercury mercury mercury <http://localhost>.
 *
 *   Mercury mercury mercury mercury mercury mercury mercury mercury mercury [mercury](http://localhost).
 *
 *   Mercury mercury mercury mercury mercury mercury mercury mercury mercury ![mercury](http://localhost).
 *
 *   <div>Mercury mercury mercury mercury mercury mercury mercury mercury mercury</div>
 *
 *   [foo]: http://localhost/mercury/mercury/mercury/mercury/mercury/mercury/mercury/mercury
 *
 * @example
 *   {"config": 20, "label": "input", "name": "not-ok.md", "positionless": true}
 *
 *   Mercury mercury mercury
 *   mercury.
 *
 *   Mercury mercury mercury `mercury()`.
 *
 *   Mercury mercury mercury <http://localhost>.
 *
 *   Mercury mercury mercury [m](example.com).
 *
 *   Mercury mercury mercury ![m](example.com).
 *
 *   `mercury()` mercury mercury mercury.
 *
 *   <http://localhost> mercury.
 *
 *   [m](example.com) mercury.
 *
 *   ![m](example.com) mercury.
 *
 *   Mercury mercury ![m](example.com) mercury.
 *
 * @example
 *   {"config": 20, "label": "output", "name": "not-ok.md", "positionless": true}
 *
 *   1:24: Unexpected `23` character line, expected at most `20` characters, remove `3` characters
 *   4:37: Unexpected `36` character line, expected at most `20` characters, remove `16` characters
 *   6:44: Unexpected `43` character line, expected at most `20` characters, remove `23` characters
 *   8:42: Unexpected `41` character line, expected at most `20` characters, remove `21` characters
 *   10:43: Unexpected `42` character line, expected at most `20` characters, remove `22` characters
 *   12:37: Unexpected `36` character line, expected at most `20` characters, remove `16` characters
 *   14:28: Unexpected `27` character line, expected at most `20` characters, remove `7` characters
 *   16:26: Unexpected `25` character line, expected at most `20` characters, remove `5` characters
 *   18:27: Unexpected `26` character line, expected at most `20` characters, remove `6` characters
 *   20:43: Unexpected `42` character line, expected at most `20` characters, remove `22` characters
 *
 * @example
 *   {"config": 20, "name": "long-autolinks-ok.md", "positionless": true}
 *
 *   <http://localhost/mercury/>
 *
 *   <http://localhost/mercury/>
 *   mercury.
 *
 *   Mercury
 *   <http://localhost/mercury/>.
 *
 *   Mercury
 *   <http://localhost/mercury/>
 *   mercury.
 *
 *   Mercury
 *   <http://localhost/mercury/>
 *   mercury mercury.
 *
 *   Mercury mercury
 *   <http://localhost/mercury/>
 *   mercury mercury.
 *
 * @example
 *   {"config": 20, "label": "input", "name": "long-autolinks-nok.md", "positionless": true}
 *
 *   <http://localhost/mercury/> mercury.
 *
 *   Mercury <http://localhost/mercury/>.
 *
 *   Mercury
 *   <http://localhost/mercury/> mercury.
 *
 *   Mercury <http://localhost/mercury/>
 *   mercury.
 * @example
 *   {"config": 20, "label": "output", "name": "long-autolinks-nok.md"}
 *
 *   1:37: Unexpected `36` character line, expected at most `20` characters, remove `16` characters
 *   6:37: Unexpected `36` character line, expected at most `20` characters, remove `16` characters
 *
 * @example
 *   {"config": 20, "frontmatter": true, "name": "ok.md", "positionless": true}
 *
 *   ---
 *   description: Mercury mercury mercury mercury.
 *   ---
 *
 * @example
 *   {"config": 20, "gfm": true, "name": "ok.md", "positionless": true}
 *
 *   | Mercury | Mercury | Mercury |
 *   | ------- | ------- | ------- |
 *
 * @example
 *   {"config": 20, "math": true, "name": "ok.md", "positionless": true}
 *
 *   $$
 *   L = \frac{1}{2} \rho v^2 S C_L
 *   $$
 *
 * @example
 *   {"config": 20, "mdx": true, "name": "ok.md", "positionless": true}
 *
 *   export const description = 'Mercury mercury mercury mercury.'
 *
 *   {description}
 *
 * @example
 *   {"config": 10, "name": "ok-mixed-line-endings.md", "positionless": true}
 *
 *   0123456789␍␊0123456789␊01234␍␊01234␊
 *
 * @example
 *   {"config": 10, "label": "input", "name": "not-ok-mixed-line-endings.md", "positionless": true}
 *
 *   012345678901␍␊012345678901␊01234567890␍␊01234567890␊
 *
 * @example
 *   {"config": 10, "label": "output", "name": "not-ok-mixed-line-endings.md", "positionless": true}
 *
 *   1:13: Unexpected `12` character line, expected at most `10` characters, remove `2` characters
 *   2:13: Unexpected `12` character line, expected at most `10` characters, remove `2` characters
 *   3:12: Unexpected `11` character line, expected at most `10` characters, remove `1` character
 *   4:12: Unexpected `11` character line, expected at most `10` characters, remove `1` character
 *
 * @example
 *   {"config": "🌍", "label": "output", "name": "not-ok.md", "positionless": true}
 *
 *   1:1: Unexpected value `🌍` for `options`, expected `number`
 */
const remarkLintMaximumLineLength = lintRule$1(
  {
    origin: 'remark-lint:maximum-line-length',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-maximum-line-length#readme'
  },
  function (tree, file, options) {
    const value = String(file);
    const lines = value.split(/\r?\n/);
    let expected = 80;
    if (options === null || options === undefined) ; else if (typeof options === 'number') {
      expected = options;
    } else {
      file.fail(
        'Unexpected value `' + options + '` for `options`, expected `number`'
      );
    }
    visit(tree, function (node, index, parent) {
      if (
        node.type === 'code' ||
        node.type === 'definition' ||
        node.type === 'heading' ||
        node.type === 'html' ||
        node.type === 'math' ||
        node.type === 'mdxjsEsm' ||
        node.type === 'mdxFlowExpression' ||
        node.type === 'mdxTextExpression' ||
        node.type === 'table' ||
        node.type === 'toml' ||
        node.type === 'yaml'
      ) {
        const end = pointEnd(node);
        const start = pointStart(node);
        if (end && start) {
          let line = start.line - 1;
          while (line < end.line) {
            lines[line++] = '';
          }
        }
        return SKIP
      }
      if (
        node.type === 'image' ||
        node.type === 'inlineCode' ||
        node.type === 'link'
      ) {
        const end = pointEnd(node);
        const start = pointStart(node);
        if (end && start && parent && typeof index === 'number') {
          if (start.column > expected) return
          if (end.column < expected) return
          const next = parent.children[index + 1];
          const nextStart = pointStart(next);
          if (
            next &&
            nextStart &&
            nextStart.line === start.line &&
            (!('value' in next) ||
              /^([^\r\n]*)[ \t]/.test(next.value))
          ) {
            return
          }
          let line = start.line - 1;
          while (line < end.line) {
            lines[line++] = '';
          }
        }
      }
    });
    let index = -1;
    while (++index < lines.length) {
      const actualBytes = lines[index].length;
      const actualCharacters = Array.from(lines[index]).length;
      const difference = actualCharacters - expected;
      if (difference > 0) {
        file.message(
          'Unexpected `' +
            actualCharacters +
            '` character line, expected at most `' +
            expected +
            '` characters, remove `' +
            difference +
            '` ' +
            pluralize('character', difference),
          {
            line: index + 1,
            column: actualBytes + 1
          }
        );
      }
    }
  }
);

/**
 * remark-lint rule to warn when multiple blank lines are used.
 *
 * ## What is this?
 *
 * This package checks the number of blank lines.
 *
 * ## When should I use this?
 *
 * You can use this package to check that there are no unneeded blank lines.
 *
 * ## API
 *
 * ### `unified().use(remarkLintNoConsecutiveBlankLines)`
 *
 * Warn when multiple blank lines are used.
 *
 * ###### Parameters
 *
 * There are no options.
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ## Recommendation
 *
 * More than one blank line has no effect between blocks.
 *
 * ## Fix
 *
 * [`remark-stringify`][github-remark-stringify] adds exactly one blank line
 * between any block.
 * It has a `join` option to configure more complex cases.
 *
 * [api-remark-lint-no-consecutive-blank-lines]: #unifieduseremarklintnoconsecutiveblanklines
 * [github-remark-stringify]: https://github.com/remarkjs/remark/tree/main/packages/remark-stringify
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module no-consecutive-blank-lines
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"name": "ok.md"}
 *
 *   # Planets
 *
 *   Mercury.
 *
 *   Venus.
 *
 * @example
 *   {"label": "input", "name": "not-ok.md"}
 *
 *   # Planets
 *
 *
 *   Mercury.
 *
 *
 *
 *   Venus.
 * @example
 *   {"label": "output", "name": "not-ok.md"}
 *
 *   4:1: Unexpected `2` blank lines before node, expected up to `1` blank line, remove `1` blank line
 *   8:1: Unexpected `3` blank lines before node, expected up to `1` blank line, remove `2` blank lines
 *
 * @example
 *   {"label": "input", "name": "initial.md"}
 *
 *   ␊Mercury.
 * @example
 *   {"label": "output", "name": "initial.md"}
 *
 *   2:1: Unexpected `1` blank line before node, expected `0` blank lines, remove `1` blank line
 *
 * @example
 *   {"name": "final-one.md"}
 *
 *   Mercury.␊
 *
 * @example
 *   {"label": "input", "name": "final-more.md"}
 *
 *   Mercury.␊␊
 * @example
 *   {"label": "output", "name": "final-more.md"}
 *
 *   1:9: Unexpected `1` blank line after node, expected `0` blank lines, remove `1` blank line
 *
 * @example
 *   {"name": "empty-document.md"}
 *
 * @example
 *   {"label": "input", "name": "block-quote.md"}
 *
 *   > Mercury.
 *
 *   Venus.
 *
 *   >
 *   > Earth.
 *   >
 * @example
 *   {"label": "output", "name": "block-quote.md"}
 *
 *   6:3: Unexpected `1` blank line before node, expected `0` blank lines, remove `1` blank line
 *   6:9: Unexpected `1` blank line after node, expected `0` blank lines, remove `1` blank line
 *
 * @example
 *   {"directive": true, "label": "input", "name": "directive.md"}
 *
 *   :::mercury
 *   Venus.
 *
 *
 *   Earth.
 *   :::
 * @example
 *   {"directive": true, "label": "output", "name": "directive.md"}
 *
 *   5:1: Unexpected `2` blank lines before node, expected up to `1` blank line, remove `1` blank line
 *
 * @example
 *   {"gfm": true, "label": "input", "name": "footnote.md"}
 *
 *   [^x]:
 *       Mercury.
 *
 *   Venus.
 *
 *   [^y]:
 *
 *       Earth.
 *
 *
 *       Mars.
 * @example
 *   {"gfm": true, "label": "output", "name": "footnote.md"}
 *
 *   8:5: Unexpected `1` blank line before node, expected `0` blank lines, remove `1` blank line
 *   11:5: Unexpected `2` blank lines before node, expected up to `1` blank line, remove `1` blank line
 *
 * @example
 *   {"label": "input", "mdx": true, "name": "jsx.md"}
 *
 *   <Mercury>
 *     Venus.
 *
 *
 *     Earth.
 *   </Mercury>
 * @example
 *   {"label": "output", "mdx": true, "name": "jsx.md"}
 *
 *   5:3: Unexpected `2` blank lines before node, expected up to `1` blank line, remove `1` blank line
 *
 * @example
 *   {"label": "input", "name": "list.md"}
 *
 *   * Mercury.
 *   * Venus.
 *
 *   ***
 *
 *   * Mercury.
 *
 *   * Venus.
 *
 *   ***
 *
 *   * Mercury.
 *
 *
 *   * Venus.
 * @example
 *   {"label": "output", "name": "list.md"}
 *
 *   15:1: Unexpected `2` blank lines before node, expected up to `1` blank line, remove `1` blank line
 *
 * @example
 *   {"label": "input", "name": "list-item.md"}
 *
 *   * Mercury.
 *     Venus.
 *
 *   ***
 *
 *   * Mercury.
 *
 *     Venus.
 *
 *   ***
 *
 *   * Mercury.
 *
 *
 *     Venus.
 *
 *   ***
 *
 *   *
 *     Mercury.
 * @example
 *   {"label": "output", "name": "list-item.md"}
 *
 *   15:3: Unexpected `2` blank lines before node, expected up to `1` blank line, remove `1` blank line
 *   20:3: Unexpected `1` blank line before node, expected `0` blank lines, remove `1` blank line
 *
 * @example
 *   {"label": "input", "name": "deep-block-quote.md"}
 *
 *   * > * > # Venus␊␊
 * @example
 *   {"label": "output", "name": "deep-block-quote.md"}
 *
 *   1:16: Unexpected `1` blank line after node, expected `0` blank lines, remove `1` blank line
 *
 * @example
 *   {"label": "input", "name": "deep-list-item.md"}
 *
 *   > * > * # Venus␊␊
 * @example
 *   {"label": "output", "name": "deep-list-item.md"}
 *
 *   1:16: Unexpected `1` blank line after node, expected `0` blank lines, remove `1` blank line
 */
const remarkLintNoConsecutiveBlankLines = lintRule$1(
  {
    origin: 'remark-lint:no-consecutive-blank-lines',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-no-consecutive-blank-lines#readme'
  },
  function (tree, file) {
    visitParents(tree, function (node, parents) {
      const parent = parents.at(-1);
      if (!parent) return
      if (phrasing(node)) {
        return SKIP
      }
      const siblings =  (parent.children);
      const index = siblings.indexOf(node);
      if (
        index === 0 &&
        parent.type !== 'containerDirective' &&
        parent.type !== 'mdxJsxFlowElement'
      ) {
        const parentStart = pointStart(parent);
        const start = pointStart(node);
        if (parentStart && start) {
          const difference =
            start.line -
            parentStart.line -
            (parent.type === 'footnoteDefinition' ? 1 : 0);
          if (difference > 0) {
            file.message(
              'Unexpected `' +
                difference +
                '` blank ' +
                pluralize('line', difference) +
                ' before node, expected `0` blank lines, remove `' +
                difference +
                '` blank ' +
                pluralize('line', difference),
              {ancestors: [...parents, node], place: start}
            );
          }
        }
      }
      const next = siblings[index + 1];
      const end = pointEnd(node);
      const nextStart = pointStart(next);
      if (end && nextStart) {
        const difference = nextStart.line - end.line - 2;
        if (difference > 0) {
          const actual = difference + 1;
          file.message(
            'Unexpected `' +
              actual +
              '` blank ' +
              pluralize('line', actual) +
              ' before node, expected up to `1` blank line, remove `' +
              difference +
              '` blank ' +
              pluralize('line', difference),
            {ancestors: [...parents, next], place: nextStart}
          );
        }
      }
      const parentEnd = pointEnd(parent);
      if (
        !next &&
        parentEnd &&
        end &&
        parent.type !== 'containerDirective' &&
        parent.type !== 'mdxJsxFlowElement'
      ) {
        const difference =
          parentEnd.line - end.line - (parent.type === 'blockquote' ? 0 : 1);
        if (difference > 0) {
          file.message(
            'Unexpected `' +
              difference +
              '` blank ' +
              pluralize('line', difference) +
              ' after node, expected `0` blank lines, remove `' +
              difference +
              '` blank ' +
              pluralize('line', difference),
            {ancestors: [...parents, node], place: end}
          );
        }
      }
    });
  }
);

/**
 * remark-lint rule to warn when file names start with `a`, `the`, and such.
 *
 * ## What is this?
 *
 * This package checks file names.
 *
 * ## When should I use this?
 *
 * You can use this package to check that file names are consistent.
 *
 * ## API
 *
 * ### `unified().use(remarkLintNoFileNameArticles)`
 *
 * Warn when file names start with `a`, `the`, and such.
 *
 * ###### Parameters
 *
 * There are no options.
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * [api-remark-lint-no-file-name-articles]: #unifieduseremarklintnofilenamearticles
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module no-file-name-articles
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"name": "title.md"}
 *
 * @example
 *   {"label": "output", "name": "a-title.md", "positionless": true}
 *
 *   1:1: Unexpected file name starting with `a`, remove it
 *
 * @example
 *   {"label": "output", "name": "the-title.md", "positionless": true}
 *
 *   1:1: Unexpected file name starting with `the`, remove it
 *
 * @example
 *   {"label": "output", "name": "an-article.md", "positionless": true}
 *
 *   1:1: Unexpected file name starting with `an`, remove it
 */
const remarkLintNoFileNameArticles = lintRule$1(
  {
    origin: 'remark-lint:no-file-name-articles',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-no-file-name-articles#readme'
  },
  function (_, file) {
    const match = file.stem && file.stem.match(/^(?:the|teh|an?)\b/i);
    if (match) {
      file.message(
        'Unexpected file name starting with `' + match[0] + '`, remove it'
      );
    }
  }
);

/**
 * remark-lint rule to warn when file names contain consecutive dashes.
 *
 * ## What is this?
 *
 * This package checks file names.
 *
 * ## When should I use this?
 *
 * You can use this package to check that file names are consistent.
 *
 * ## API
 *
 * ### `unified().use(remarkLintNoFileNameConsecutiveDashes)`
 *
 * Warn when file names contain consecutive dashes.
 *
 * ###### Parameters
 *
 * There are no options.
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * [api-remark-lint-no-file-name-consecutive-dashes]: #unifieduseremarklintnofilenameconsecutivedashes
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module no-file-name-consecutive-dashes
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"name": "plug-ins.md"}
 *
 * @example
 *   {"name": "plug--ins.md", "label": "output", "positionless": true}
 *
 *   1:1: Unexpected consecutive dashes in a file name, expected `-`
 */
const remarkLintNoFileNameConsecutiveDashes = lintRule$1(
  {
    origin: 'remark-lint:no-file-name-consecutive-dashes',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-no-file-name-consecutive-dashes#readme'
  },
  function (_, file) {
    if (file.stem && /-{2,}/.test(file.stem)) {
      file.message('Unexpected consecutive dashes in a file name, expected `-`');
    }
  }
);

/**
 * remark-lint rule to warn when file names start or end with dashes.
 *
 * ## What is this?
 *
 * This package checks file names.
 *
 * ## When should I use this?
 *
 * You can use this package to check that file names are consistent.
 *
 * ## API
 *
 * ### `unified().use(remarkLintNoFileNameOuterDashes)`
 *
 * Warn when file names start or end with dashes.
 *
 * ###### Parameters
 *
 * There are no options.
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * [api-remark-lint-no-file-name-outer-dashes]: #unifieduseremarklintnofilenameouterdashes
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module no-file-name-outer-dashes
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"name": "mercury-and-venus.md"}
 *
 * @example
 *   {"label": "output", "name": "-mercury.md", "positionless": true}
 *
 *   1:1: Unexpected initial or final dashes in file name, expected dashes to join words
 *
 * @example
 *   {"label": "output", "name": "venus-.md", "positionless": true}
 *
 *   1:1: Unexpected initial or final dashes in file name, expected dashes to join words
 */
const remarkLintNofileNameOuterDashes = lintRule$1(
  {
    origin: 'remark-lint:no-file-name-outer-dashes',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-no-file-name-outer-dashes#readme'
  },
  function (_, file) {
    if (file.stem && /^-|-$/.test(file.stem)) {
      file.message(
        'Unexpected initial or final dashes in file name, expected dashes to join words'
      );
    }
  }
);

/**
 * remark-lint rule to warn when headings are indented.
 *
 * ## What is this?
 *
 * This package checks the spaces before headings.
 *
 * ## When should I use this?
 *
 * You can use this rule to check markdown code style.
 *
 * ## API
 *
 * ### `unified().use(remarkLintNoHeadingIndent)`
 *
 * Warn when headings are indented.
 *
 * ###### Parameters
 *
 * There are no options.
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ## Recommendation
 *
 * There is no specific handling of indented headings (or anything else) in
 * markdown.
 * While it is possible to use an indent to headings on their text:
 *
 * ```markdown
 *    # Mercury
 *   ## Venus
 *  ### Earth
 * #### Mars
 * ```
 *
 * …such style is uncommon,
 * a bit hard to maintain,
 * and it’s impossible to add a heading with a rank of 5 as it would form
 * indented code instead.
 * So it’s recommended to not indent headings and to turn this rule on.
 *
 * ## Fix
 *
 * [`remark-stringify`][github-remark-stringify] formats headings without indent.
 *
 * [api-remark-lint-no-heading-indent]: #unifieduseremarklintnoheadingindent
 * [github-remark-stringify]: https://github.com/remarkjs/remark/tree/main/packages/remark-stringify
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module no-heading-indent
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"name": "ok.md"}
 *
 *   #␠Mercury
 *
 *   Venus
 *   -----
 *
 *   #␠Earth␠#
 *
 *   Mars
 *   ====
 *
 * @example
 *   {"label": "input", "name": "not-ok.md"}
 *
 *   ␠␠␠# Mercury
 *
 *   ␠Venus
 *   ------
 *
 *   ␠# Earth #
 *
 *   ␠␠␠Mars
 *   ======
 * @example
 *   {"label": "output", "name": "not-ok.md"}
 *
 *    1:4: Unexpected `3` spaces before heading, expected `0` spaces, remove `3` spaces
 *    3:2: Unexpected `1` space before heading, expected `0` spaces, remove `1` space
 *    6:2: Unexpected `1` space before heading, expected `0` spaces, remove `1` space
 *    8:4: Unexpected `3` spaces before heading, expected `0` spaces, remove `3` spaces
 */
const remarkLintNoHeadingIndent = lintRule$1(
  {
    origin: 'remark-lint:no-heading-indent',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-no-heading-indent#readme'
  },
  function (tree, file) {
    visitParents(tree, function (node, parents) {
      if (phrasing(node)) {
        return SKIP
      }
      const parent = parents[parents.length - 1];
      const start = pointStart(node);
      if (
        !start ||
        !parent ||
        node.type !== 'heading' ||
        parent.type !== 'root'
      ) {
        return
      }
      const actual = start.column - 1;
      if (actual) {
        file.message(
          'Unexpected `' +
            actual +
            '` ' +
            pluralize('space', actual) +
            ' before heading, expected `0` spaces, remove' +
            ' `' +
            actual +
            '` ' +
            pluralize('space', actual),
          {ancestors: [...parents, node], place: start}
        );
      }
    });
  }
);

/**
 * remark-lint rule to warn when top-level headings are used multiple times.
 *
 * ## What is this?
 *
 * This package checks that top-level headings are unique.
 *
 * ## When should I use this?
 *
 * You can use this package to check heading structure.
 *
 * ## API
 *
 * ### `unified().use(remarkLintNoMultipleToplevelHeadings[, options])`
 *
 * Warn when top-level headings are used multiple times.
 *
 * ###### Parameters
 *
 * * `options` ([`Options`][api-options], default: `1`)
 *   — configuration
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ### `Depth`
 *
 * Depth (TypeScript type).
 *
 * ###### Type
 *
 * ```ts
 * type Depth = 1 | 2 | 3 | 4 | 5 | 6
 * ```
 *
 * ### `Options`
 *
 * Configuration (TypeScript type).
 *
 * ###### Type
 *
 * ```ts
 * type Options = Depth
 * ```
 *
 * ## Recommendation
 *
 * Documents should almost always have one main heading,
 * which is typically a heading with a rank of `1`.
 *
 * [api-depth]: #depth
 * [api-options]: #options
 * [api-remark-lint-no-multiple-toplevel-headings]: #unifieduseremarklintnomultipletoplevelheadings-options
 * [github-remark-stringify]: https://github.com/remarkjs/remark/tree/main/packages/remark-stringify
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module no-multiple-toplevel-headings
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"name": "ok.md"}
 *
 *   # Mercury
 *
 *   ## Venus
 *
 * @example
 *   {"label": "input", "name": "not-ok.md"}
 *
 *   # Venus
 *
 *   # Mercury
 * @example
 *   {"label": "output", "name": "not-ok.md"}
 *
 *   3:1-3:10: Unexpected duplicate toplevel heading, exected a single heading with rank `1`
 *
 * @example
 *   {"config": 2, "label": "input", "name": "not-ok.md"}
 *
 *   ## Venus
 *
 *   ## Mercury
 * @example
 *   {"config": 2, "label": "output", "name": "not-ok.md"}
 *
 *   3:1-3:11: Unexpected duplicate toplevel heading, exected a single heading with rank `2`
 *
 * @example
 *   {"label": "input", "name": "html.md"}
 *
 *   Venus <b>and</b> mercury.
 *
 *   <h1>Earth</h1>
 *
 *   <h1>Mars</h1>
 * @example
 *   {"label": "output", "name": "html.md"}
 *
 *   5:1-5:14: Unexpected duplicate toplevel heading, exected a single heading with rank `1`
 *
 * @example
 *   {"label": "input", "mdx": true, "name": "mdx.mdx"}
 *
 *   Venus <b>and</b> mercury.
 *
 *   <h1>Earth</h1>
 *   <h1>Mars</h1>
 * @example
 *   {"label": "output", "mdx": true, "name": "mdx.mdx"}
 *
 *   4:1-4:14: Unexpected duplicate toplevel heading, exected a single heading with rank `1`
 */
const htmlRe = /<h([1-6])/;
const jsxNameRe = /^h([1-6])$/;
const remarkLintNoMultipleToplevelHeadings = lintRule$1(
  {
    origin: 'remark-lint:no-multiple-toplevel-headings',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-no-multiple-toplevel-headings#readme'
  },
  function (tree, file, options) {
    const option = options || 1;
    let duplicateAncestors;
    visitParents(tree, function (node, parents) {
      let rank;
      if (node.type === 'heading') {
        rank = node.depth;
      } else if (node.type === 'html') {
        const results = node.value.match(htmlRe);
        rank = results ?  (Number(results[1])) : undefined;
      } else if (
        (node.type === 'mdxJsxFlowElement' ||
          node.type === 'mdxJsxTextElement') &&
        node.name
      ) {
        const results = node.name.match(jsxNameRe);
        rank = results ?  (Number(results[1])) : undefined;
      }
      if (rank) {
        const ancestors = [...parents, node];
        if (node.position && rank === option) {
          if (duplicateAncestors) {
            const duplicate = duplicateAncestors.at(-1);
            file.message(
              'Unexpected duplicate toplevel heading, exected a single heading with rank `' +
                rank +
                '`',
              {
                ancestors,
                cause: new VFileMessage(
                  'Toplevel heading already defined here',
                  {
                    ancestors: duplicateAncestors,
                    place: duplicate.position,
                    source: 'remark-lint',
                    ruleId: 'no-multiple-toplevel-headings'
                  }
                ),
                place: node.position
              }
            );
          } else {
            duplicateAncestors = ancestors;
          }
        }
      }
    });
  }
);

/**
 * remark-lint rule to warn when every line in shell code is preceded by `$`s.
 *
 * ## What is this?
 *
 * This package checks for `$` markers prefixing shell code,
 * which are hard to copy/paste.
 *
 * ## When should I use this?
 *
 * You can use this package to check shell code blocks.
 *
 * ## API
 *
 * ### `unified().use(remarkLintNoShellDollars)`
 *
 * Warn when every line in shell code is preceded by `$`s.
 *
 * ###### Parameters
 *
 * There are no options.
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ## Recommendation
 *
 * Dollars make copy/pasting hard.
 * Either put dollars in front of some lines (commands) and don’t put them in
 * front of other lines (output),
 * or use different code blocks for commands and output.
 *
 * [api-remark-lint-no-shell-dollars]: #unifieduseremarklintnoshelldollars
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module no-shell-dollars
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"name": "ok.md"}
 *
 *   ```bash
 *   echo "Mercury and Venus"
 *   ```
 *
 *   ```sh
 *   echo "Mercury and Venus"
 *   echo "Earth and Mars" > file
 *   ```
 *
 *   Mixed dollars for input lines and without for output is also OK:
 *
 *   ```zsh
 *   $ echo "Mercury and Venus"
 *   Mercury and Venus
 *   $ echo "Earth and Mars" > file
 *   ```
 *
 *   ```command
 *   ```
 *
 *   ```js
 *   $('div').remove()
 *   ```
 *
 * @example
 *   {"label": "input", "name": "not-ok.md"}
 *
 *   ```sh
 *   $ echo "Mercury and Venus"
 *   ```
 *
 *   ```bash
 *   $ echo "Mercury and Venus"
 *   $ echo "Earth and Mars" > file
 *   ```
 *
 * @example
 *   {"label": "output", "name": "not-ok.md"}
 *
 *   1:1-3:4: Unexpected shell code with every line prefixed by `$`, expected different code for input and output
 *   5:1-8:4: Unexpected shell code with every line prefixed by `$`, expected different code for input and output
 */
const flags = new Set([
  'bash',
  'bats',
  'command',
  'csh',
  'ebuild',
  'eclass',
  'ksh',
  'sh',
  'sh.in',
  'tcsh',
  'tmux',
  'tool',
  'zsh',
  'zsh-theme',
  'abuild',
  'alpine-abuild',
  'apkbuild',
  'gentoo-ebuild',
  'gentoo-eclass',
  'openrc',
  'openrc-runscript',
  'shell',
  'shell-script'
]);
const remarkLintNoShellDollars = lintRule$1(
  {
    origin: 'remark-lint:no-shell-dollars',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-no-shell-dollars#readme'
  },
  function (tree, file) {
    visitParents(tree, function (node, parents) {
      if (phrasing(node)) {
        return SKIP
      }
      if (
        node.type === 'code' &&
        node.position &&
        node.lang &&
        flags.has(node.lang)
      ) {
        const lines = node.value.split('\n');
        let index = -1;
        let some = false;
        while (++index < lines.length) {
          const line = collapseWhiteSpace(lines[index], {
            style: 'html',
            trim: true
          });
          if (!line) continue
          if (line.charCodeAt(0) !== 36 ) return
          some = true;
        }
        if (!some) return
        file.message(
          'Unexpected shell code with every line prefixed by `$`, expected different code for input and output',
          {ancestors: [...parents, node], place: node.position}
        );
      }
    });
  }
);

/**
 * remark-lint rule to warn when GFM tables are indented.
 *
 * ## What is this?
 *
 * This package checks the indent of GFM tables.
 * Tables are a GFM feature enabled with
 * [`remark-gfm`][github-remark-gfm].
 *
 * ## When should I use this?
 *
 * You can use this package to check that tables are consistent.
 *
 * ## API
 *
 * ### `unified().use(remarkLintNoTableIndentation)`
 *
 * Warn when GFM tables are indented.
 *
 * ###### Parameters
 *
 * There are no options.
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ## Recommendation
 *
 * There is no specific handling of indented tables (or anything else) in
 * markdown.
 * So it’s recommended to not indent tables and to turn this rule on.
 *
 * ## Fix
 *
 * [`remark-stringify`][github-remark-stringify] with
 * [`remark-gfm`][github-remark-gfm] formats all tables without indent.
 *
 * [api-remark-lint-no-table-indentation]: #unifieduseremarklintnotableindentation
 * [github-remark-gfm]: https://github.com/remarkjs/remark-gfm
 * [github-remark-stringify]: https://github.com/remarkjs/remark/tree/main/packages/remark-stringify
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module no-table-indentation
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"name": "ok.md", "gfm": true}
 *
 *   | Planet  | Mean anomaly (°) |
 *   | ------- | ---------------: |
 *   | Mercury |          174 796 |
 *
 * @example
 *   {"gfm": true, "label": "input", "name": "not-ok.md"}
 *
 *   ␠| Planet  | Mean anomaly (°) |
 *   ␠␠| ------- | ---------------: |
 *   ␠␠␠| Mercury |          174 796 |
 *
 * @example
 *   {"gfm": true, "label": "output", "name": "not-ok.md"}
 *
 *   1:2: Unexpected `1` extra space before table row, remove `1` space
 *   2:3: Unexpected `2` extra spaces before table row, remove `2` spaces
 *   3:4: Unexpected `3` extra spaces before table row, remove `3` spaces
 *
 * @example
 *   {"gfm": true, "label": "input", "name": "blockquote.md"}
 *
 *   >␠| Planet  |
 *   >␠␠| ------- |
 *
 * @example
 *   {"gfm": true, "label": "output", "name": "blockquote.md"}
 *
 *   2:4: Unexpected `1` extra space before table row, remove `1` space
 *
 * @example
 *   {"gfm": true, "label": "input", "name": "list.md"}
 *
 *   *␠| Planet  |
 *   ␠␠␠| ------- |
 *
 * @example
 *   {"gfm": true, "label": "output", "name": "list.md"}
 *
 *   2:4: Unexpected `1` extra space before table row, remove `1` space
 */
const remarkLintNoTableIndentation = lintRule$1(
  {
    origin: 'remark-lint:no-table-indentation',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-no-table-indentation#readme'
  },
  function (tree, file) {
    const value = String(file);
    const locations = location(value);
    visitParents(tree, function (node, parents) {
      if (phrasing(node)) {
        return SKIP
      }
      if (node.type !== 'table') return
      const parent = parents.at(-1);
      const end = pointEnd(node);
      const start = pointStart(node);
      if (!parent || !end || !start) return
      const parentHead = parent.children[0];
      let line = start.line;
      let column;
      if (parent.type === 'root') {
        column = 1;
      } else if (parent.type === 'blockquote') {
        const parentStart = pointStart(parent);
        if (parentStart) {
          column = parentStart.column + 2;
        }
      } else if (parent.type === 'listItem') {
        const headStart = pointStart(parentHead);
        if (headStart) {
          column = headStart.column;
          if (parentHead === node) {
            line++;
          }
        }
      }
      if (!column) return
      while (line <= end.line) {
        let index = locations.toOffset({line, column});
        if (typeof index !== 'number') continue
        const expected = index;
        let code = value.charCodeAt(index - 1);
        while (code === 9  || code === 32 ) {
          index--;
          code = value.charCodeAt(index - 1);
        }
        if (
          code === 10  ||
          code === 13  ||
          code === 62  ||
          Number.isNaN(code)
        ) {
          let actual = expected;
          code = value.charCodeAt(actual);
          while (code === 9  || code === 32 ) {
            code = value.charCodeAt(++actual);
          }
          const difference = actual - expected;
          if (difference !== 0) {
            file.message(
              'Unexpected `' +
                difference +
                '` extra ' +
                pluralize('space', difference) +
                ' before table row, remove `' +
                difference +
                '` ' +
                pluralize('space', difference),
              {
                ancestors: [...parents, node],
                place: {
                  line,
                  column: column + difference,
                  offset: actual
                }
              }
            );
          }
        }
        line++;
      }
      return SKIP
    });
  }
);

/**
 * remark-lint rule to warn when tabs are used.
 *
 * ## What is this?
 *
 * This package checks for tabs.
 *
 * ## When should I use this?
 *
 * You can use this package to check tabs.
 *
 * ## API
 *
 * ### `unified().use(remarkLintNoTabs)`
 *
 * Warn when tabs are used.
 *
 * ###### Parameters
 *
 * There are no options.
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ## Recommendation
 *
 * Regardless of the debate in other languages of whether to use tabs versus
 * spaces,
 * when it comes to markdown,
 * tabs do not work as expected.
 * Largely around things such as block quotes, lists, and indented code.
 *
 * Take for example block quotes: `>\ta` gives a paragraph with the text `a`
 * in a blockquote,
 * so one might expect that `>\t\ta` results in indented code with the text `a`
 * in a block quote.
 *
 * ```markdown
 * >\ta
 *
 * >\t\ta
 * ```
 *
 * Yields:
 *
 * ```html
 * <blockquote>
 * <p>a</p>
 * </blockquote>
 * <blockquote>
 * <pre><code>  a
 * </code></pre>
 * </blockquote>
 * ```
 *
 * Because markdown uses a hardcoded tab size of 4,
 * the first tab could be represented as 3 spaces (because there’s a `>`
 * before).
 * One of those “spaces” is taken because block quotes allow the `>` to be
 * followed by one space,
 * leaving 2 spaces.
 * The next tab can be represented as 4 spaces,
 * so together we have 6 spaces.
 * The indented code uses 4 spaces, so there are two spaces left, which are
 * shown in the indented code.
 *
 * ## Fix
 *
 * [`remark-stringify`](https://github.com/remarkjs/remark/tree/main/packages/remark-stringify)
 * uses spaces exclusively for indentation.
 *
 * [api-remark-lint-no-tabs]: #unifieduseremarklintnotabs
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module no-tabs
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"name": "ok.md"}
 *
 *   ␠␠␠␠mercury()
 *
 * @example
 *   {"label": "input", "name": "not-ok.md", "positionless": true}
 *
 *   ␉mercury()
 *
 *   Venus␉and Earth.
 * @example
 *   {"label": "output", "name": "not-ok.md"}
 *
 *   1:1: Unexpected tab (`\t`), expected spaces
 *   3:6: Unexpected tab (`\t`), expected spaces
 */
const remarkLintNoTabs = lintRule$1(
  {
    origin: 'remark-lint:no-tabs',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-no-tabs#readme'
  },
  function (_, file) {
    const value = String(file);
    const toPoint = location(file).toPoint;
    let index = value.indexOf('\t');
    while (index !== -1) {
      file.message('Unexpected tab (`\\t`), expected spaces', {
        place: toPoint(index)
      });
      index = value.indexOf('\t', index + 1);
    }
  }
);

var sliced$1 = function (args, slice, sliceEnd) {
  var ret = [];
  var len = args.length;
  if (0 === len) return ret;
  var start = slice < 0
    ? Math.max(0, slice + len)
    : slice || 0;
  if (sliceEnd !== undefined) {
    len = sliceEnd < 0
      ? sliceEnd + len
      : sliceEnd;
  }
  while (len-- > start) {
    ret[len - start] = args[len];
  }
  return ret;
};
getDefaultExportFromCjs(sliced$1);

var slice = Array.prototype.slice;
var co_1 = co$1;
function co$1(fn) {
  var isGenFun = isGeneratorFunction(fn);
  return function (done) {
    var ctx = this;
    var gen = fn;
    if (isGenFun) {
      var args = slice.call(arguments), len = args.length;
      var hasCallback = len && 'function' == typeof args[len - 1];
      done = hasCallback ? args.pop() : error;
      gen = fn.apply(this, args);
    } else {
      done = done || error;
    }
    next();
    function exit(err, res) {
      setImmediate(function(){
        done.call(ctx, err, res);
      });
    }
    function next(err, res) {
      var ret;
      if (arguments.length > 2) res = slice.call(arguments, 1);
      if (err) {
        try {
          ret = gen.throw(err);
        } catch (e) {
          return exit(e);
        }
      }
      if (!err) {
        try {
          ret = gen.next(res);
        } catch (e) {
          return exit(e);
        }
      }
      if (ret.done) return exit(null, ret.value);
      ret.value = toThunk(ret.value, ctx);
      if ('function' == typeof ret.value) {
        var called = false;
        try {
          ret.value.call(ctx, function(){
            if (called) return;
            called = true;
            next.apply(ctx, arguments);
          });
        } catch (e) {
          setImmediate(function(){
            if (called) return;
            called = true;
            next(e);
          });
        }
        return;
      }
      next(new TypeError('You may only yield a function, promise, generator, array, or object, '
        + 'but the following was passed: "' + String(ret.value) + '"'));
    }
  }
}
function toThunk(obj, ctx) {
  if (isGeneratorFunction(obj)) {
    return co$1(obj.call(ctx));
  }
  if (isGenerator(obj)) {
    return co$1(obj);
  }
  if (isPromise(obj)) {
    return promiseToThunk(obj);
  }
  if ('function' == typeof obj) {
    return obj;
  }
  if (isObject$1(obj) || Array.isArray(obj)) {
    return objectToThunk.call(ctx, obj);
  }
  return obj;
}
function objectToThunk(obj){
  var ctx = this;
  var isArray = Array.isArray(obj);
  return function(done){
    var keys = Object.keys(obj);
    var pending = keys.length;
    var results = isArray
      ? new Array(pending)
      : new obj.constructor();
    var finished;
    if (!pending) {
      setImmediate(function(){
        done(null, results);
      });
      return;
    }
    if (!isArray) {
      for (var i = 0; i < pending; i++) {
        results[keys[i]] = undefined;
      }
    }
    for (var i = 0; i < keys.length; i++) {
      run(obj[keys[i]], keys[i]);
    }
    function run(fn, key) {
      if (finished) return;
      try {
        fn = toThunk(fn, ctx);
        if ('function' != typeof fn) {
          results[key] = fn;
          return --pending || done(null, results);
        }
        fn.call(ctx, function(err, res){
          if (finished) return;
          if (err) {
            finished = true;
            return done(err);
          }
          results[key] = res;
          --pending || done(null, results);
        });
      } catch (err) {
        finished = true;
        done(err);
      }
    }
  }
}
function promiseToThunk(promise) {
  return function(fn){
    promise.then(function(res) {
      fn(null, res);
    }, fn);
  }
}
function isPromise(obj) {
  return obj && 'function' == typeof obj.then;
}
function isGenerator(obj) {
  return obj && 'function' == typeof obj.next && 'function' == typeof obj.throw;
}
function isGeneratorFunction(obj) {
  return obj && obj.constructor && 'GeneratorFunction' == obj.constructor.name;
}
function isObject$1(val) {
  return val && Object == val.constructor;
}
function error(err) {
  if (!err) return;
  setImmediate(function(){
    throw err;
  });
}
getDefaultExportFromCjs(co_1);

var sliced = sliced$1;
var noop = function(){};
var co = co_1;
var wrapped_1 = wrapped$1;
function wrapped$1(fn) {
  function wrap() {
    var args = sliced(arguments);
    var last = args[args.length - 1];
    var ctx = this;
    var done = typeof last == 'function' ? args.pop() : noop;
    if (!fn) {
      return done.apply(ctx, [null].concat(args));
    }
    if (generator(fn)) {
      return co(fn).apply(ctx, args.concat(done));
    }
    if (fn.length > args.length) {
      try {
        return fn.apply(ctx, args.concat(done));
      } catch (e) {
        return done(e);
      }
    }
    return sync(fn, done).apply(ctx, args);
  }
  return wrap;
}
function sync(fn, done) {
  return function () {
    var ret;
    try {
      ret = fn.apply(this, arguments);
    } catch (err) {
      return done(err);
    }
    if (promise(ret)) {
      ret.then(function (value) { done(null, value); }, done);
    } else {
      ret instanceof Error ? done(ret) : done(null, ret);
    }
  }
}
function generator(value) {
  return value
    && value.constructor
    && 'GeneratorFunction' == value.constructor.name;
}
function promise(value) {
  return value && 'function' == typeof value.then;
}
getDefaultExportFromCjs(wrapped_1);

var wrapped = wrapped_1;
var unifiedLintRule = factory;
function factory(id, rule) {
  var parts = id.split(':');
  var source = parts[0];
  var ruleId = parts[1];
  var fn = wrapped(rule);
  if (!ruleId) {
    ruleId = source;
    source = null;
  }
  attacher.displayName = id;
  return attacher
  function attacher(raw) {
    var config = coerce$1(ruleId, raw);
    var severity = config[0];
    var options = config[1];
    var fatal = severity === 2;
    return severity ? transformer : undefined
    function transformer(tree, file, next) {
      var index = file.messages.length;
      fn(tree, file, options, done);
      function done(err) {
        var messages = file.messages;
        var message;
        if (err && messages.indexOf(err) === -1) {
          try {
            file.fail(err);
          } catch (_) {}
        }
        while (index < messages.length) {
          message = messages[index];
          message.ruleId = ruleId;
          message.source = source;
          message.fatal = fatal;
          index++;
        }
        next();
      }
    }
  }
}
function coerce$1(name, value) {
  var def = 1;
  var result;
  var level;
  if (typeof value === 'boolean') {
    result = [value];
  } else if (value == null) {
    result = [def];
  } else if (
    typeof value === 'object' &&
    (typeof value[0] === 'number' ||
      typeof value[0] === 'boolean' ||
      typeof value[0] === 'string')
  ) {
    result = value.concat();
  } else {
    result = [1, value];
  }
  level = result[0];
  if (typeof level === 'boolean') {
    level = level ? 1 : 0;
  } else if (typeof level === 'string') {
    if (level === 'off') {
      level = 0;
    } else if (level === 'on' || level === 'warn') {
      level = 1;
    } else if (level === 'error') {
      level = 2;
    } else {
      level = 1;
      result = [level, result];
    }
  }
  if (level < 0 || level > 2) {
    throw new Error(
      'Incorrect severity `' +
        level +
        '` for `' +
        name +
        '`, ' +
        'expected 0, 1, or 2'
    )
  }
  result[0] = level;
  return result
}
getDefaultExportFromCjs(unifiedLintRule);

var rule = unifiedLintRule;
var remarkLintNoTrailingSpaces = rule('remark-lint:no-trailing-spaces', noTrailingSpaces);
function noTrailingSpaces(ast, file) {
  var lines = file.toString().split(/\r?\n/);
  for (var i = 0; i < lines.length; i++) {
    var currentLine = lines[i];
    var lineIndex = i + 1;
    if (/\s$/.test(currentLine)) {
      file.message('Remove trailing whitespace', {
        position: {
          start: { line: lineIndex, column: currentLine.length + 1 },
          end: { line: lineIndex }
        }
      });
    }
  }
}
var remarkLintNoTrailingSpaces$1 = getDefaultExportFromCjs(remarkLintNoTrailingSpaces);

function* getLinksRecursively(node) {
  if (node.url) {
    yield node;
  }
  for (const child of node.children || []) {
    yield* getLinksRecursively(child);
  }
}
function validateLinks(tree, vfile) {
  const currentFileURL = pathToFileURL(path$2.join(vfile.cwd, vfile.path));
  let previousDefinitionLabel;
  for (const node of getLinksRecursively(tree)) {
    if (node.url[0] !== "#") {
      const targetURL = new URL(node.url, currentFileURL);
      if (targetURL.protocol === "file:" && !fs.existsSync(targetURL)) {
        vfile.message("Broken link", node);
      } else if (targetURL.pathname === currentFileURL.pathname) {
        const expected = node.url.includes("#")
          ? node.url.slice(node.url.indexOf("#"))
          : "#";
        vfile.message(
          `Self-reference must start with hash (expected "${expected}", got "${node.url}")`,
          node,
        );
      }
    }
    if (node.type === "definition") {
      if (previousDefinitionLabel && previousDefinitionLabel > node.label) {
        vfile.message(
          `Unordered reference ("${node.label}" should be before "${previousDefinitionLabel}")`,
          node,
        );
      }
      previousDefinitionLabel = node.label;
    }
  }
}
const remarkLintNodejsLinks = lintRule$1(
  "remark-lint:nodejs-links",
  validateLinks,
);

/*! js-yaml 4.1.0 https://github.com/nodeca/js-yaml @license MIT */
function isNothing(subject) {
  return (typeof subject === 'undefined') || (subject === null);
}
function isObject(subject) {
  return (typeof subject === 'object') && (subject !== null);
}
function toArray(sequence) {
  if (Array.isArray(sequence)) return sequence;
  else if (isNothing(sequence)) return [];
  return [ sequence ];
}
function extend(target, source) {
  var index, length, key, sourceKeys;
  if (source) {
    sourceKeys = Object.keys(source);
    for (index = 0, length = sourceKeys.length; index < length; index += 1) {
      key = sourceKeys[index];
      target[key] = source[key];
    }
  }
  return target;
}
function repeat(string, count) {
  var result = '', cycle;
  for (cycle = 0; cycle < count; cycle += 1) {
    result += string;
  }
  return result;
}
function isNegativeZero(number) {
  return (number === 0) && (Number.NEGATIVE_INFINITY === 1 / number);
}
var isNothing_1      = isNothing;
var isObject_1       = isObject;
var toArray_1        = toArray;
var repeat_1         = repeat;
var isNegativeZero_1 = isNegativeZero;
var extend_1         = extend;
var common = {
	isNothing: isNothing_1,
	isObject: isObject_1,
	toArray: toArray_1,
	repeat: repeat_1,
	isNegativeZero: isNegativeZero_1,
	extend: extend_1
};
function formatError(exception, compact) {
  var where = '', message = exception.reason || '(unknown reason)';
  if (!exception.mark) return message;
  if (exception.mark.name) {
    where += 'in "' + exception.mark.name + '" ';
  }
  where += '(' + (exception.mark.line + 1) + ':' + (exception.mark.column + 1) + ')';
  if (!compact && exception.mark.snippet) {
    where += '\n\n' + exception.mark.snippet;
  }
  return message + ' ' + where;
}
function YAMLException$1(reason, mark) {
  Error.call(this);
  this.name = 'YAMLException';
  this.reason = reason;
  this.mark = mark;
  this.message = formatError(this, false);
  if (Error.captureStackTrace) {
    Error.captureStackTrace(this, this.constructor);
  } else {
    this.stack = (new Error()).stack || '';
  }
}
YAMLException$1.prototype = Object.create(Error.prototype);
YAMLException$1.prototype.constructor = YAMLException$1;
YAMLException$1.prototype.toString = function toString(compact) {
  return this.name + ': ' + formatError(this, compact);
};
var exception = YAMLException$1;
function getLine(buffer, lineStart, lineEnd, position, maxLineLength) {
  var head = '';
  var tail = '';
  var maxHalfLength = Math.floor(maxLineLength / 2) - 1;
  if (position - lineStart > maxHalfLength) {
    head = ' ... ';
    lineStart = position - maxHalfLength + head.length;
  }
  if (lineEnd - position > maxHalfLength) {
    tail = ' ...';
    lineEnd = position + maxHalfLength - tail.length;
  }
  return {
    str: head + buffer.slice(lineStart, lineEnd).replace(/\t/g, '→') + tail,
    pos: position - lineStart + head.length
  };
}
function padStart(string, max) {
  return common.repeat(' ', max - string.length) + string;
}
function makeSnippet(mark, options) {
  options = Object.create(options || null);
  if (!mark.buffer) return null;
  if (!options.maxLength) options.maxLength = 79;
  if (typeof options.indent      !== 'number') options.indent      = 1;
  if (typeof options.linesBefore !== 'number') options.linesBefore = 3;
  if (typeof options.linesAfter  !== 'number') options.linesAfter  = 2;
  var re = /\r?\n|\r|\0/g;
  var lineStarts = [ 0 ];
  var lineEnds = [];
  var match;
  var foundLineNo = -1;
  while ((match = re.exec(mark.buffer))) {
    lineEnds.push(match.index);
    lineStarts.push(match.index + match[0].length);
    if (mark.position <= match.index && foundLineNo < 0) {
      foundLineNo = lineStarts.length - 2;
    }
  }
  if (foundLineNo < 0) foundLineNo = lineStarts.length - 1;
  var result = '', i, line;
  var lineNoLength = Math.min(mark.line + options.linesAfter, lineEnds.length).toString().length;
  var maxLineLength = options.maxLength - (options.indent + lineNoLength + 3);
  for (i = 1; i <= options.linesBefore; i++) {
    if (foundLineNo - i < 0) break;
    line = getLine(
      mark.buffer,
      lineStarts[foundLineNo - i],
      lineEnds[foundLineNo - i],
      mark.position - (lineStarts[foundLineNo] - lineStarts[foundLineNo - i]),
      maxLineLength
    );
    result = common.repeat(' ', options.indent) + padStart((mark.line - i + 1).toString(), lineNoLength) +
      ' | ' + line.str + '\n' + result;
  }
  line = getLine(mark.buffer, lineStarts[foundLineNo], lineEnds[foundLineNo], mark.position, maxLineLength);
  result += common.repeat(' ', options.indent) + padStart((mark.line + 1).toString(), lineNoLength) +
    ' | ' + line.str + '\n';
  result += common.repeat('-', options.indent + lineNoLength + 3 + line.pos) + '^' + '\n';
  for (i = 1; i <= options.linesAfter; i++) {
    if (foundLineNo + i >= lineEnds.length) break;
    line = getLine(
      mark.buffer,
      lineStarts[foundLineNo + i],
      lineEnds[foundLineNo + i],
      mark.position - (lineStarts[foundLineNo] - lineStarts[foundLineNo + i]),
      maxLineLength
    );
    result += common.repeat(' ', options.indent) + padStart((mark.line + i + 1).toString(), lineNoLength) +
      ' | ' + line.str + '\n';
  }
  return result.replace(/\n$/, '');
}
var snippet = makeSnippet;
var TYPE_CONSTRUCTOR_OPTIONS = [
  'kind',
  'multi',
  'resolve',
  'construct',
  'instanceOf',
  'predicate',
  'represent',
  'representName',
  'defaultStyle',
  'styleAliases'
];
var YAML_NODE_KINDS = [
  'scalar',
  'sequence',
  'mapping'
];
function compileStyleAliases(map) {
  var result = {};
  if (map !== null) {
    Object.keys(map).forEach(function (style) {
      map[style].forEach(function (alias) {
        result[String(alias)] = style;
      });
    });
  }
  return result;
}
function Type$1(tag, options) {
  options = options || {};
  Object.keys(options).forEach(function (name) {
    if (TYPE_CONSTRUCTOR_OPTIONS.indexOf(name) === -1) {
      throw new exception('Unknown option "' + name + '" is met in definition of "' + tag + '" YAML type.');
    }
  });
  this.options       = options;
  this.tag           = tag;
  this.kind          = options['kind']          || null;
  this.resolve       = options['resolve']       || function () { return true; };
  this.construct     = options['construct']     || function (data) { return data; };
  this.instanceOf    = options['instanceOf']    || null;
  this.predicate     = options['predicate']     || null;
  this.represent     = options['represent']     || null;
  this.representName = options['representName'] || null;
  this.defaultStyle  = options['defaultStyle']  || null;
  this.multi         = options['multi']         || false;
  this.styleAliases  = compileStyleAliases(options['styleAliases'] || null);
  if (YAML_NODE_KINDS.indexOf(this.kind) === -1) {
    throw new exception('Unknown kind "' + this.kind + '" is specified for "' + tag + '" YAML type.');
  }
}
var type = Type$1;
function compileList(schema, name) {
  var result = [];
  schema[name].forEach(function (currentType) {
    var newIndex = result.length;
    result.forEach(function (previousType, previousIndex) {
      if (previousType.tag === currentType.tag &&
          previousType.kind === currentType.kind &&
          previousType.multi === currentType.multi) {
        newIndex = previousIndex;
      }
    });
    result[newIndex] = currentType;
  });
  return result;
}
function compileMap() {
  var result = {
        scalar: {},
        sequence: {},
        mapping: {},
        fallback: {},
        multi: {
          scalar: [],
          sequence: [],
          mapping: [],
          fallback: []
        }
      }, index, length;
  function collectType(type) {
    if (type.multi) {
      result.multi[type.kind].push(type);
      result.multi['fallback'].push(type);
    } else {
      result[type.kind][type.tag] = result['fallback'][type.tag] = type;
    }
  }
  for (index = 0, length = arguments.length; index < length; index += 1) {
    arguments[index].forEach(collectType);
  }
  return result;
}
function Schema$1(definition) {
  return this.extend(definition);
}
Schema$1.prototype.extend = function extend(definition) {
  var implicit = [];
  var explicit = [];
  if (definition instanceof type) {
    explicit.push(definition);
  } else if (Array.isArray(definition)) {
    explicit = explicit.concat(definition);
  } else if (definition && (Array.isArray(definition.implicit) || Array.isArray(definition.explicit))) {
    if (definition.implicit) implicit = implicit.concat(definition.implicit);
    if (definition.explicit) explicit = explicit.concat(definition.explicit);
  } else {
    throw new exception('Schema.extend argument should be a Type, [ Type ], ' +
      'or a schema definition ({ implicit: [...], explicit: [...] })');
  }
  implicit.forEach(function (type$1) {
    if (!(type$1 instanceof type)) {
      throw new exception('Specified list of YAML types (or a single Type object) contains a non-Type object.');
    }
    if (type$1.loadKind && type$1.loadKind !== 'scalar') {
      throw new exception('There is a non-scalar type in the implicit list of a schema. Implicit resolving of such types is not supported.');
    }
    if (type$1.multi) {
      throw new exception('There is a multi type in the implicit list of a schema. Multi tags can only be listed as explicit.');
    }
  });
  explicit.forEach(function (type$1) {
    if (!(type$1 instanceof type)) {
      throw new exception('Specified list of YAML types (or a single Type object) contains a non-Type object.');
    }
  });
  var result = Object.create(Schema$1.prototype);
  result.implicit = (this.implicit || []).concat(implicit);
  result.explicit = (this.explicit || []).concat(explicit);
  result.compiledImplicit = compileList(result, 'implicit');
  result.compiledExplicit = compileList(result, 'explicit');
  result.compiledTypeMap  = compileMap(result.compiledImplicit, result.compiledExplicit);
  return result;
};
var schema = Schema$1;
var str = new type('tag:yaml.org,2002:str', {
  kind: 'scalar',
  construct: function (data) { return data !== null ? data : ''; }
});
var seq = new type('tag:yaml.org,2002:seq', {
  kind: 'sequence',
  construct: function (data) { return data !== null ? data : []; }
});
var map = new type('tag:yaml.org,2002:map', {
  kind: 'mapping',
  construct: function (data) { return data !== null ? data : {}; }
});
var failsafe = new schema({
  explicit: [
    str,
    seq,
    map
  ]
});
function resolveYamlNull(data) {
  if (data === null) return true;
  var max = data.length;
  return (max === 1 && data === '~') ||
         (max === 4 && (data === 'null' || data === 'Null' || data === 'NULL'));
}
function constructYamlNull() {
  return null;
}
function isNull(object) {
  return object === null;
}
var _null = new type('tag:yaml.org,2002:null', {
  kind: 'scalar',
  resolve: resolveYamlNull,
  construct: constructYamlNull,
  predicate: isNull,
  represent: {
    canonical: function () { return '~';    },
    lowercase: function () { return 'null'; },
    uppercase: function () { return 'NULL'; },
    camelcase: function () { return 'Null'; },
    empty:     function () { return '';     }
  },
  defaultStyle: 'lowercase'
});
function resolveYamlBoolean(data) {
  if (data === null) return false;
  var max = data.length;
  return (max === 4 && (data === 'true' || data === 'True' || data === 'TRUE')) ||
         (max === 5 && (data === 'false' || data === 'False' || data === 'FALSE'));
}
function constructYamlBoolean(data) {
  return data === 'true' ||
         data === 'True' ||
         data === 'TRUE';
}
function isBoolean(object) {
  return Object.prototype.toString.call(object) === '[object Boolean]';
}
var bool = new type('tag:yaml.org,2002:bool', {
  kind: 'scalar',
  resolve: resolveYamlBoolean,
  construct: constructYamlBoolean,
  predicate: isBoolean,
  represent: {
    lowercase: function (object) { return object ? 'true' : 'false'; },
    uppercase: function (object) { return object ? 'TRUE' : 'FALSE'; },
    camelcase: function (object) { return object ? 'True' : 'False'; }
  },
  defaultStyle: 'lowercase'
});
function isHexCode(c) {
  return ((0x30 <= c) && (c <= 0x39)) ||
         ((0x41 <= c) && (c <= 0x46)) ||
         ((0x61 <= c) && (c <= 0x66));
}
function isOctCode(c) {
  return ((0x30 <= c) && (c <= 0x37));
}
function isDecCode(c) {
  return ((0x30 <= c) && (c <= 0x39));
}
function resolveYamlInteger(data) {
  if (data === null) return false;
  var max = data.length,
      index = 0,
      hasDigits = false,
      ch;
  if (!max) return false;
  ch = data[index];
  if (ch === '-' || ch === '+') {
    ch = data[++index];
  }
  if (ch === '0') {
    if (index + 1 === max) return true;
    ch = data[++index];
    if (ch === 'b') {
      index++;
      for (; index < max; index++) {
        ch = data[index];
        if (ch === '_') continue;
        if (ch !== '0' && ch !== '1') return false;
        hasDigits = true;
      }
      return hasDigits && ch !== '_';
    }
    if (ch === 'x') {
      index++;
      for (; index < max; index++) {
        ch = data[index];
        if (ch === '_') continue;
        if (!isHexCode(data.charCodeAt(index))) return false;
        hasDigits = true;
      }
      return hasDigits && ch !== '_';
    }
    if (ch === 'o') {
      index++;
      for (; index < max; index++) {
        ch = data[index];
        if (ch === '_') continue;
        if (!isOctCode(data.charCodeAt(index))) return false;
        hasDigits = true;
      }
      return hasDigits && ch !== '_';
    }
  }
  if (ch === '_') return false;
  for (; index < max; index++) {
    ch = data[index];
    if (ch === '_') continue;
    if (!isDecCode(data.charCodeAt(index))) {
      return false;
    }
    hasDigits = true;
  }
  if (!hasDigits || ch === '_') return false;
  return true;
}
function constructYamlInteger(data) {
  var value = data, sign = 1, ch;
  if (value.indexOf('_') !== -1) {
    value = value.replace(/_/g, '');
  }
  ch = value[0];
  if (ch === '-' || ch === '+') {
    if (ch === '-') sign = -1;
    value = value.slice(1);
    ch = value[0];
  }
  if (value === '0') return 0;
  if (ch === '0') {
    if (value[1] === 'b') return sign * parseInt(value.slice(2), 2);
    if (value[1] === 'x') return sign * parseInt(value.slice(2), 16);
    if (value[1] === 'o') return sign * parseInt(value.slice(2), 8);
  }
  return sign * parseInt(value, 10);
}
function isInteger(object) {
  return (Object.prototype.toString.call(object)) === '[object Number]' &&
         (object % 1 === 0 && !common.isNegativeZero(object));
}
var int = new type('tag:yaml.org,2002:int', {
  kind: 'scalar',
  resolve: resolveYamlInteger,
  construct: constructYamlInteger,
  predicate: isInteger,
  represent: {
    binary:      function (obj) { return obj >= 0 ? '0b' + obj.toString(2) : '-0b' + obj.toString(2).slice(1); },
    octal:       function (obj) { return obj >= 0 ? '0o'  + obj.toString(8) : '-0o'  + obj.toString(8).slice(1); },
    decimal:     function (obj) { return obj.toString(10); },
    hexadecimal: function (obj) { return obj >= 0 ? '0x' + obj.toString(16).toUpperCase() :  '-0x' + obj.toString(16).toUpperCase().slice(1); }
  },
  defaultStyle: 'decimal',
  styleAliases: {
    binary:      [ 2,  'bin' ],
    octal:       [ 8,  'oct' ],
    decimal:     [ 10, 'dec' ],
    hexadecimal: [ 16, 'hex' ]
  }
});
var YAML_FLOAT_PATTERN = new RegExp(
  '^(?:[-+]?(?:[0-9][0-9_]*)(?:\\.[0-9_]*)?(?:[eE][-+]?[0-9]+)?' +
  '|\\.[0-9_]+(?:[eE][-+]?[0-9]+)?' +
  '|[-+]?\\.(?:inf|Inf|INF)' +
  '|\\.(?:nan|NaN|NAN))$');
function resolveYamlFloat(data) {
  if (data === null) return false;
  if (!YAML_FLOAT_PATTERN.test(data) ||
      data[data.length - 1] === '_') {
    return false;
  }
  return true;
}
function constructYamlFloat(data) {
  var value, sign;
  value  = data.replace(/_/g, '').toLowerCase();
  sign   = value[0] === '-' ? -1 : 1;
  if ('+-'.indexOf(value[0]) >= 0) {
    value = value.slice(1);
  }
  if (value === '.inf') {
    return (sign === 1) ? Number.POSITIVE_INFINITY : Number.NEGATIVE_INFINITY;
  } else if (value === '.nan') {
    return NaN;
  }
  return sign * parseFloat(value, 10);
}
var SCIENTIFIC_WITHOUT_DOT = /^[-+]?[0-9]+e/;
function representYamlFloat(object, style) {
  var res;
  if (isNaN(object)) {
    switch (style) {
      case 'lowercase': return '.nan';
      case 'uppercase': return '.NAN';
      case 'camelcase': return '.NaN';
    }
  } else if (Number.POSITIVE_INFINITY === object) {
    switch (style) {
      case 'lowercase': return '.inf';
      case 'uppercase': return '.INF';
      case 'camelcase': return '.Inf';
    }
  } else if (Number.NEGATIVE_INFINITY === object) {
    switch (style) {
      case 'lowercase': return '-.inf';
      case 'uppercase': return '-.INF';
      case 'camelcase': return '-.Inf';
    }
  } else if (common.isNegativeZero(object)) {
    return '-0.0';
  }
  res = object.toString(10);
  return SCIENTIFIC_WITHOUT_DOT.test(res) ? res.replace('e', '.e') : res;
}
function isFloat(object) {
  return (Object.prototype.toString.call(object) === '[object Number]') &&
         (object % 1 !== 0 || common.isNegativeZero(object));
}
var float = new type('tag:yaml.org,2002:float', {
  kind: 'scalar',
  resolve: resolveYamlFloat,
  construct: constructYamlFloat,
  predicate: isFloat,
  represent: representYamlFloat,
  defaultStyle: 'lowercase'
});
var json = failsafe.extend({
  implicit: [
    _null,
    bool,
    int,
    float
  ]
});
var core = json;
var YAML_DATE_REGEXP = new RegExp(
  '^([0-9][0-9][0-9][0-9])'          +
  '-([0-9][0-9])'                    +
  '-([0-9][0-9])$');
var YAML_TIMESTAMP_REGEXP = new RegExp(
  '^([0-9][0-9][0-9][0-9])'          +
  '-([0-9][0-9]?)'                   +
  '-([0-9][0-9]?)'                   +
  '(?:[Tt]|[ \\t]+)'                 +
  '([0-9][0-9]?)'                    +
  ':([0-9][0-9])'                    +
  ':([0-9][0-9])'                    +
  '(?:\\.([0-9]*))?'                 +
  '(?:[ \\t]*(Z|([-+])([0-9][0-9]?)' +
  '(?::([0-9][0-9]))?))?$');
function resolveYamlTimestamp(data) {
  if (data === null) return false;
  if (YAML_DATE_REGEXP.exec(data) !== null) return true;
  if (YAML_TIMESTAMP_REGEXP.exec(data) !== null) return true;
  return false;
}
function constructYamlTimestamp(data) {
  var match, year, month, day, hour, minute, second, fraction = 0,
      delta = null, tz_hour, tz_minute, date;
  match = YAML_DATE_REGEXP.exec(data);
  if (match === null) match = YAML_TIMESTAMP_REGEXP.exec(data);
  if (match === null) throw new Error('Date resolve error');
  year = +(match[1]);
  month = +(match[2]) - 1;
  day = +(match[3]);
  if (!match[4]) {
    return new Date(Date.UTC(year, month, day));
  }
  hour = +(match[4]);
  minute = +(match[5]);
  second = +(match[6]);
  if (match[7]) {
    fraction = match[7].slice(0, 3);
    while (fraction.length < 3) {
      fraction += '0';
    }
    fraction = +fraction;
  }
  if (match[9]) {
    tz_hour = +(match[10]);
    tz_minute = +(match[11] || 0);
    delta = (tz_hour * 60 + tz_minute) * 60000;
    if (match[9] === '-') delta = -delta;
  }
  date = new Date(Date.UTC(year, month, day, hour, minute, second, fraction));
  if (delta) date.setTime(date.getTime() - delta);
  return date;
}
function representYamlTimestamp(object ) {
  return object.toISOString();
}
var timestamp = new type('tag:yaml.org,2002:timestamp', {
  kind: 'scalar',
  resolve: resolveYamlTimestamp,
  construct: constructYamlTimestamp,
  instanceOf: Date,
  represent: representYamlTimestamp
});
function resolveYamlMerge(data) {
  return data === '<<' || data === null;
}
var merge = new type('tag:yaml.org,2002:merge', {
  kind: 'scalar',
  resolve: resolveYamlMerge
});
var BASE64_MAP = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=\n\r';
function resolveYamlBinary(data) {
  if (data === null) return false;
  var code, idx, bitlen = 0, max = data.length, map = BASE64_MAP;
  for (idx = 0; idx < max; idx++) {
    code = map.indexOf(data.charAt(idx));
    if (code > 64) continue;
    if (code < 0) return false;
    bitlen += 6;
  }
  return (bitlen % 8) === 0;
}
function constructYamlBinary(data) {
  var idx, tailbits,
      input = data.replace(/[\r\n=]/g, ''),
      max = input.length,
      map = BASE64_MAP,
      bits = 0,
      result = [];
  for (idx = 0; idx < max; idx++) {
    if ((idx % 4 === 0) && idx) {
      result.push((bits >> 16) & 0xFF);
      result.push((bits >> 8) & 0xFF);
      result.push(bits & 0xFF);
    }
    bits = (bits << 6) | map.indexOf(input.charAt(idx));
  }
  tailbits = (max % 4) * 6;
  if (tailbits === 0) {
    result.push((bits >> 16) & 0xFF);
    result.push((bits >> 8) & 0xFF);
    result.push(bits & 0xFF);
  } else if (tailbits === 18) {
    result.push((bits >> 10) & 0xFF);
    result.push((bits >> 2) & 0xFF);
  } else if (tailbits === 12) {
    result.push((bits >> 4) & 0xFF);
  }
  return new Uint8Array(result);
}
function representYamlBinary(object ) {
  var result = '', bits = 0, idx, tail,
      max = object.length,
      map = BASE64_MAP;
  for (idx = 0; idx < max; idx++) {
    if ((idx % 3 === 0) && idx) {
      result += map[(bits >> 18) & 0x3F];
      result += map[(bits >> 12) & 0x3F];
      result += map[(bits >> 6) & 0x3F];
      result += map[bits & 0x3F];
    }
    bits = (bits << 8) + object[idx];
  }
  tail = max % 3;
  if (tail === 0) {
    result += map[(bits >> 18) & 0x3F];
    result += map[(bits >> 12) & 0x3F];
    result += map[(bits >> 6) & 0x3F];
    result += map[bits & 0x3F];
  } else if (tail === 2) {
    result += map[(bits >> 10) & 0x3F];
    result += map[(bits >> 4) & 0x3F];
    result += map[(bits << 2) & 0x3F];
    result += map[64];
  } else if (tail === 1) {
    result += map[(bits >> 2) & 0x3F];
    result += map[(bits << 4) & 0x3F];
    result += map[64];
    result += map[64];
  }
  return result;
}
function isBinary(obj) {
  return Object.prototype.toString.call(obj) ===  '[object Uint8Array]';
}
var binary = new type('tag:yaml.org,2002:binary', {
  kind: 'scalar',
  resolve: resolveYamlBinary,
  construct: constructYamlBinary,
  predicate: isBinary,
  represent: representYamlBinary
});
var _hasOwnProperty$3 = Object.prototype.hasOwnProperty;
var _toString$2       = Object.prototype.toString;
function resolveYamlOmap(data) {
  if (data === null) return true;
  var objectKeys = [], index, length, pair, pairKey, pairHasKey,
      object = data;
  for (index = 0, length = object.length; index < length; index += 1) {
    pair = object[index];
    pairHasKey = false;
    if (_toString$2.call(pair) !== '[object Object]') return false;
    for (pairKey in pair) {
      if (_hasOwnProperty$3.call(pair, pairKey)) {
        if (!pairHasKey) pairHasKey = true;
        else return false;
      }
    }
    if (!pairHasKey) return false;
    if (objectKeys.indexOf(pairKey) === -1) objectKeys.push(pairKey);
    else return false;
  }
  return true;
}
function constructYamlOmap(data) {
  return data !== null ? data : [];
}
var omap = new type('tag:yaml.org,2002:omap', {
  kind: 'sequence',
  resolve: resolveYamlOmap,
  construct: constructYamlOmap
});
var _toString$1 = Object.prototype.toString;
function resolveYamlPairs(data) {
  if (data === null) return true;
  var index, length, pair, keys, result,
      object = data;
  result = new Array(object.length);
  for (index = 0, length = object.length; index < length; index += 1) {
    pair = object[index];
    if (_toString$1.call(pair) !== '[object Object]') return false;
    keys = Object.keys(pair);
    if (keys.length !== 1) return false;
    result[index] = [ keys[0], pair[keys[0]] ];
  }
  return true;
}
function constructYamlPairs(data) {
  if (data === null) return [];
  var index, length, pair, keys, result,
      object = data;
  result = new Array(object.length);
  for (index = 0, length = object.length; index < length; index += 1) {
    pair = object[index];
    keys = Object.keys(pair);
    result[index] = [ keys[0], pair[keys[0]] ];
  }
  return result;
}
var pairs = new type('tag:yaml.org,2002:pairs', {
  kind: 'sequence',
  resolve: resolveYamlPairs,
  construct: constructYamlPairs
});
var _hasOwnProperty$2 = Object.prototype.hasOwnProperty;
function resolveYamlSet(data) {
  if (data === null) return true;
  var key, object = data;
  for (key in object) {
    if (_hasOwnProperty$2.call(object, key)) {
      if (object[key] !== null) return false;
    }
  }
  return true;
}
function constructYamlSet(data) {
  return data !== null ? data : {};
}
var set = new type('tag:yaml.org,2002:set', {
  kind: 'mapping',
  resolve: resolveYamlSet,
  construct: constructYamlSet
});
var _default = core.extend({
  implicit: [
    timestamp,
    merge
  ],
  explicit: [
    binary,
    omap,
    pairs,
    set
  ]
});
var _hasOwnProperty$1 = Object.prototype.hasOwnProperty;
var CONTEXT_FLOW_IN   = 1;
var CONTEXT_FLOW_OUT  = 2;
var CONTEXT_BLOCK_IN  = 3;
var CONTEXT_BLOCK_OUT = 4;
var CHOMPING_CLIP  = 1;
var CHOMPING_STRIP = 2;
var CHOMPING_KEEP  = 3;
var PATTERN_NON_PRINTABLE         = /[\x00-\x08\x0B\x0C\x0E-\x1F\x7F-\x84\x86-\x9F\uFFFE\uFFFF]|[\uD800-\uDBFF](?![\uDC00-\uDFFF])|(?:[^\uD800-\uDBFF]|^)[\uDC00-\uDFFF]/;
var PATTERN_NON_ASCII_LINE_BREAKS = /[\x85\u2028\u2029]/;
var PATTERN_FLOW_INDICATORS       = /[,\[\]\{\}]/;
var PATTERN_TAG_HANDLE            = /^(?:!|!!|![a-z\-]+!)$/i;
var PATTERN_TAG_URI               = /^(?:!|[^,\[\]\{\}])(?:%[0-9a-f]{2}|[0-9a-z\-#;\/\?:@&=\+\$,_\.!~\*'\(\)\[\]])*$/i;
function _class(obj) { return Object.prototype.toString.call(obj); }
function is_EOL(c) {
  return (c === 0x0A) || (c === 0x0D);
}
function is_WHITE_SPACE(c) {
  return (c === 0x09) || (c === 0x20);
}
function is_WS_OR_EOL(c) {
  return (c === 0x09) ||
         (c === 0x20) ||
         (c === 0x0A) ||
         (c === 0x0D);
}
function is_FLOW_INDICATOR(c) {
  return c === 0x2C ||
         c === 0x5B ||
         c === 0x5D ||
         c === 0x7B ||
         c === 0x7D;
}
function fromHexCode(c) {
  var lc;
  if ((0x30 <= c) && (c <= 0x39)) {
    return c - 0x30;
  }
  lc = c | 0x20;
  if ((0x61 <= lc) && (lc <= 0x66)) {
    return lc - 0x61 + 10;
  }
  return -1;
}
function escapedHexLen(c) {
  if (c === 0x78) { return 2; }
  if (c === 0x75) { return 4; }
  if (c === 0x55) { return 8; }
  return 0;
}
function fromDecimalCode(c) {
  if ((0x30 <= c) && (c <= 0x39)) {
    return c - 0x30;
  }
  return -1;
}
function simpleEscapeSequence(c) {
  return (c === 0x30) ? '\x00' :
        (c === 0x61) ? '\x07' :
        (c === 0x62) ? '\x08' :
        (c === 0x74) ? '\x09' :
        (c === 0x09) ? '\x09' :
        (c === 0x6E) ? '\x0A' :
        (c === 0x76) ? '\x0B' :
        (c === 0x66) ? '\x0C' :
        (c === 0x72) ? '\x0D' :
        (c === 0x65) ? '\x1B' :
        (c === 0x20) ? ' ' :
        (c === 0x22) ? '\x22' :
        (c === 0x2F) ? '/' :
        (c === 0x5C) ? '\x5C' :
        (c === 0x4E) ? '\x85' :
        (c === 0x5F) ? '\xA0' :
        (c === 0x4C) ? '\u2028' :
        (c === 0x50) ? '\u2029' : '';
}
function charFromCodepoint(c) {
  if (c <= 0xFFFF) {
    return String.fromCharCode(c);
  }
  return String.fromCharCode(
    ((c - 0x010000) >> 10) + 0xD800,
    ((c - 0x010000) & 0x03FF) + 0xDC00
  );
}
var simpleEscapeCheck = new Array(256);
var simpleEscapeMap = new Array(256);
for (var i = 0; i < 256; i++) {
  simpleEscapeCheck[i] = simpleEscapeSequence(i) ? 1 : 0;
  simpleEscapeMap[i] = simpleEscapeSequence(i);
}
function State$1(input, options) {
  this.input = input;
  this.filename  = options['filename']  || null;
  this.schema    = options['schema']    || _default;
  this.onWarning = options['onWarning'] || null;
  this.legacy    = options['legacy']    || false;
  this.json      = options['json']      || false;
  this.listener  = options['listener']  || null;
  this.implicitTypes = this.schema.compiledImplicit;
  this.typeMap       = this.schema.compiledTypeMap;
  this.length     = input.length;
  this.position   = 0;
  this.line       = 0;
  this.lineStart  = 0;
  this.lineIndent = 0;
  this.firstTabInLine = -1;
  this.documents = [];
}
function generateError(state, message) {
  var mark = {
    name:     state.filename,
    buffer:   state.input.slice(0, -1),
    position: state.position,
    line:     state.line,
    column:   state.position - state.lineStart
  };
  mark.snippet = snippet(mark);
  return new exception(message, mark);
}
function throwError(state, message) {
  throw generateError(state, message);
}
function throwWarning(state, message) {
  if (state.onWarning) {
    state.onWarning.call(null, generateError(state, message));
  }
}
var directiveHandlers = {
  YAML: function handleYamlDirective(state, name, args) {
    var match, major, minor;
    if (state.version !== null) {
      throwError(state, 'duplication of %YAML directive');
    }
    if (args.length !== 1) {
      throwError(state, 'YAML directive accepts exactly one argument');
    }
    match = /^([0-9]+)\.([0-9]+)$/.exec(args[0]);
    if (match === null) {
      throwError(state, 'ill-formed argument of the YAML directive');
    }
    major = parseInt(match[1], 10);
    minor = parseInt(match[2], 10);
    if (major !== 1) {
      throwError(state, 'unacceptable YAML version of the document');
    }
    state.version = args[0];
    state.checkLineBreaks = (minor < 2);
    if (minor !== 1 && minor !== 2) {
      throwWarning(state, 'unsupported YAML version of the document');
    }
  },
  TAG: function handleTagDirective(state, name, args) {
    var handle, prefix;
    if (args.length !== 2) {
      throwError(state, 'TAG directive accepts exactly two arguments');
    }
    handle = args[0];
    prefix = args[1];
    if (!PATTERN_TAG_HANDLE.test(handle)) {
      throwError(state, 'ill-formed tag handle (first argument) of the TAG directive');
    }
    if (_hasOwnProperty$1.call(state.tagMap, handle)) {
      throwError(state, 'there is a previously declared suffix for "' + handle + '" tag handle');
    }
    if (!PATTERN_TAG_URI.test(prefix)) {
      throwError(state, 'ill-formed tag prefix (second argument) of the TAG directive');
    }
    try {
      prefix = decodeURIComponent(prefix);
    } catch (err) {
      throwError(state, 'tag prefix is malformed: ' + prefix);
    }
    state.tagMap[handle] = prefix;
  }
};
function captureSegment(state, start, end, checkJson) {
  var _position, _length, _character, _result;
  if (start < end) {
    _result = state.input.slice(start, end);
    if (checkJson) {
      for (_position = 0, _length = _result.length; _position < _length; _position += 1) {
        _character = _result.charCodeAt(_position);
        if (!(_character === 0x09 ||
              (0x20 <= _character && _character <= 0x10FFFF))) {
          throwError(state, 'expected valid JSON character');
        }
      }
    } else if (PATTERN_NON_PRINTABLE.test(_result)) {
      throwError(state, 'the stream contains non-printable characters');
    }
    state.result += _result;
  }
}
function mergeMappings(state, destination, source, overridableKeys) {
  var sourceKeys, key, index, quantity;
  if (!common.isObject(source)) {
    throwError(state, 'cannot merge mappings; the provided source object is unacceptable');
  }
  sourceKeys = Object.keys(source);
  for (index = 0, quantity = sourceKeys.length; index < quantity; index += 1) {
    key = sourceKeys[index];
    if (!_hasOwnProperty$1.call(destination, key)) {
      destination[key] = source[key];
      overridableKeys[key] = true;
    }
  }
}
function storeMappingPair(state, _result, overridableKeys, keyTag, keyNode, valueNode,
  startLine, startLineStart, startPos) {
  var index, quantity;
  if (Array.isArray(keyNode)) {
    keyNode = Array.prototype.slice.call(keyNode);
    for (index = 0, quantity = keyNode.length; index < quantity; index += 1) {
      if (Array.isArray(keyNode[index])) {
        throwError(state, 'nested arrays are not supported inside keys');
      }
      if (typeof keyNode === 'object' && _class(keyNode[index]) === '[object Object]') {
        keyNode[index] = '[object Object]';
      }
    }
  }
  if (typeof keyNode === 'object' && _class(keyNode) === '[object Object]') {
    keyNode = '[object Object]';
  }
  keyNode = String(keyNode);
  if (_result === null) {
    _result = {};
  }
  if (keyTag === 'tag:yaml.org,2002:merge') {
    if (Array.isArray(valueNode)) {
      for (index = 0, quantity = valueNode.length; index < quantity; index += 1) {
        mergeMappings(state, _result, valueNode[index], overridableKeys);
      }
    } else {
      mergeMappings(state, _result, valueNode, overridableKeys);
    }
  } else {
    if (!state.json &&
        !_hasOwnProperty$1.call(overridableKeys, keyNode) &&
        _hasOwnProperty$1.call(_result, keyNode)) {
      state.line = startLine || state.line;
      state.lineStart = startLineStart || state.lineStart;
      state.position = startPos || state.position;
      throwError(state, 'duplicated mapping key');
    }
    if (keyNode === '__proto__') {
      Object.defineProperty(_result, keyNode, {
        configurable: true,
        enumerable: true,
        writable: true,
        value: valueNode
      });
    } else {
      _result[keyNode] = valueNode;
    }
    delete overridableKeys[keyNode];
  }
  return _result;
}
function readLineBreak(state) {
  var ch;
  ch = state.input.charCodeAt(state.position);
  if (ch === 0x0A) {
    state.position++;
  } else if (ch === 0x0D) {
    state.position++;
    if (state.input.charCodeAt(state.position) === 0x0A) {
      state.position++;
    }
  } else {
    throwError(state, 'a line break is expected');
  }
  state.line += 1;
  state.lineStart = state.position;
  state.firstTabInLine = -1;
}
function skipSeparationSpace(state, allowComments, checkIndent) {
  var lineBreaks = 0,
      ch = state.input.charCodeAt(state.position);
  while (ch !== 0) {
    while (is_WHITE_SPACE(ch)) {
      if (ch === 0x09 && state.firstTabInLine === -1) {
        state.firstTabInLine = state.position;
      }
      ch = state.input.charCodeAt(++state.position);
    }
    if (allowComments && ch === 0x23) {
      do {
        ch = state.input.charCodeAt(++state.position);
      } while (ch !== 0x0A && ch !== 0x0D && ch !== 0);
    }
    if (is_EOL(ch)) {
      readLineBreak(state);
      ch = state.input.charCodeAt(state.position);
      lineBreaks++;
      state.lineIndent = 0;
      while (ch === 0x20) {
        state.lineIndent++;
        ch = state.input.charCodeAt(++state.position);
      }
    } else {
      break;
    }
  }
  if (checkIndent !== -1 && lineBreaks !== 0 && state.lineIndent < checkIndent) {
    throwWarning(state, 'deficient indentation');
  }
  return lineBreaks;
}
function testDocumentSeparator(state) {
  var _position = state.position,
      ch;
  ch = state.input.charCodeAt(_position);
  if ((ch === 0x2D || ch === 0x2E) &&
      ch === state.input.charCodeAt(_position + 1) &&
      ch === state.input.charCodeAt(_position + 2)) {
    _position += 3;
    ch = state.input.charCodeAt(_position);
    if (ch === 0 || is_WS_OR_EOL(ch)) {
      return true;
    }
  }
  return false;
}
function writeFoldedLines(state, count) {
  if (count === 1) {
    state.result += ' ';
  } else if (count > 1) {
    state.result += common.repeat('\n', count - 1);
  }
}
function readPlainScalar(state, nodeIndent, withinFlowCollection) {
  var preceding,
      following,
      captureStart,
      captureEnd,
      hasPendingContent,
      _line,
      _lineStart,
      _lineIndent,
      _kind = state.kind,
      _result = state.result,
      ch;
  ch = state.input.charCodeAt(state.position);
  if (is_WS_OR_EOL(ch)      ||
      is_FLOW_INDICATOR(ch) ||
      ch === 0x23    ||
      ch === 0x26    ||
      ch === 0x2A    ||
      ch === 0x21    ||
      ch === 0x7C    ||
      ch === 0x3E    ||
      ch === 0x27    ||
      ch === 0x22    ||
      ch === 0x25    ||
      ch === 0x40    ||
      ch === 0x60) {
    return false;
  }
  if (ch === 0x3F || ch === 0x2D) {
    following = state.input.charCodeAt(state.position + 1);
    if (is_WS_OR_EOL(following) ||
        withinFlowCollection && is_FLOW_INDICATOR(following)) {
      return false;
    }
  }
  state.kind = 'scalar';
  state.result = '';
  captureStart = captureEnd = state.position;
  hasPendingContent = false;
  while (ch !== 0) {
    if (ch === 0x3A) {
      following = state.input.charCodeAt(state.position + 1);
      if (is_WS_OR_EOL(following) ||
          withinFlowCollection && is_FLOW_INDICATOR(following)) {
        break;
      }
    } else if (ch === 0x23) {
      preceding = state.input.charCodeAt(state.position - 1);
      if (is_WS_OR_EOL(preceding)) {
        break;
      }
    } else if ((state.position === state.lineStart && testDocumentSeparator(state)) ||
               withinFlowCollection && is_FLOW_INDICATOR(ch)) {
      break;
    } else if (is_EOL(ch)) {
      _line = state.line;
      _lineStart = state.lineStart;
      _lineIndent = state.lineIndent;
      skipSeparationSpace(state, false, -1);
      if (state.lineIndent >= nodeIndent) {
        hasPendingContent = true;
        ch = state.input.charCodeAt(state.position);
        continue;
      } else {
        state.position = captureEnd;
        state.line = _line;
        state.lineStart = _lineStart;
        state.lineIndent = _lineIndent;
        break;
      }
    }
    if (hasPendingContent) {
      captureSegment(state, captureStart, captureEnd, false);
      writeFoldedLines(state, state.line - _line);
      captureStart = captureEnd = state.position;
      hasPendingContent = false;
    }
    if (!is_WHITE_SPACE(ch)) {
      captureEnd = state.position + 1;
    }
    ch = state.input.charCodeAt(++state.position);
  }
  captureSegment(state, captureStart, captureEnd, false);
  if (state.result) {
    return true;
  }
  state.kind = _kind;
  state.result = _result;
  return false;
}
function readSingleQuotedScalar(state, nodeIndent) {
  var ch,
      captureStart, captureEnd;
  ch = state.input.charCodeAt(state.position);
  if (ch !== 0x27) {
    return false;
  }
  state.kind = 'scalar';
  state.result = '';
  state.position++;
  captureStart = captureEnd = state.position;
  while ((ch = state.input.charCodeAt(state.position)) !== 0) {
    if (ch === 0x27) {
      captureSegment(state, captureStart, state.position, true);
      ch = state.input.charCodeAt(++state.position);
      if (ch === 0x27) {
        captureStart = state.position;
        state.position++;
        captureEnd = state.position;
      } else {
        return true;
      }
    } else if (is_EOL(ch)) {
      captureSegment(state, captureStart, captureEnd, true);
      writeFoldedLines(state, skipSeparationSpace(state, false, nodeIndent));
      captureStart = captureEnd = state.position;
    } else if (state.position === state.lineStart && testDocumentSeparator(state)) {
      throwError(state, 'unexpected end of the document within a single quoted scalar');
    } else {
      state.position++;
      captureEnd = state.position;
    }
  }
  throwError(state, 'unexpected end of the stream within a single quoted scalar');
}
function readDoubleQuotedScalar(state, nodeIndent) {
  var captureStart,
      captureEnd,
      hexLength,
      hexResult,
      tmp,
      ch;
  ch = state.input.charCodeAt(state.position);
  if (ch !== 0x22) {
    return false;
  }
  state.kind = 'scalar';
  state.result = '';
  state.position++;
  captureStart = captureEnd = state.position;
  while ((ch = state.input.charCodeAt(state.position)) !== 0) {
    if (ch === 0x22) {
      captureSegment(state, captureStart, state.position, true);
      state.position++;
      return true;
    } else if (ch === 0x5C) {
      captureSegment(state, captureStart, state.position, true);
      ch = state.input.charCodeAt(++state.position);
      if (is_EOL(ch)) {
        skipSeparationSpace(state, false, nodeIndent);
      } else if (ch < 256 && simpleEscapeCheck[ch]) {
        state.result += simpleEscapeMap[ch];
        state.position++;
      } else if ((tmp = escapedHexLen(ch)) > 0) {
        hexLength = tmp;
        hexResult = 0;
        for (; hexLength > 0; hexLength--) {
          ch = state.input.charCodeAt(++state.position);
          if ((tmp = fromHexCode(ch)) >= 0) {
            hexResult = (hexResult << 4) + tmp;
          } else {
            throwError(state, 'expected hexadecimal character');
          }
        }
        state.result += charFromCodepoint(hexResult);
        state.position++;
      } else {
        throwError(state, 'unknown escape sequence');
      }
      captureStart = captureEnd = state.position;
    } else if (is_EOL(ch)) {
      captureSegment(state, captureStart, captureEnd, true);
      writeFoldedLines(state, skipSeparationSpace(state, false, nodeIndent));
      captureStart = captureEnd = state.position;
    } else if (state.position === state.lineStart && testDocumentSeparator(state)) {
      throwError(state, 'unexpected end of the document within a double quoted scalar');
    } else {
      state.position++;
      captureEnd = state.position;
    }
  }
  throwError(state, 'unexpected end of the stream within a double quoted scalar');
}
function readFlowCollection(state, nodeIndent) {
  var readNext = true,
      _line,
      _lineStart,
      _pos,
      _tag     = state.tag,
      _result,
      _anchor  = state.anchor,
      following,
      terminator,
      isPair,
      isExplicitPair,
      isMapping,
      overridableKeys = Object.create(null),
      keyNode,
      keyTag,
      valueNode,
      ch;
  ch = state.input.charCodeAt(state.position);
  if (ch === 0x5B) {
    terminator = 0x5D;
    isMapping = false;
    _result = [];
  } else if (ch === 0x7B) {
    terminator = 0x7D;
    isMapping = true;
    _result = {};
  } else {
    return false;
  }
  if (state.anchor !== null) {
    state.anchorMap[state.anchor] = _result;
  }
  ch = state.input.charCodeAt(++state.position);
  while (ch !== 0) {
    skipSeparationSpace(state, true, nodeIndent);
    ch = state.input.charCodeAt(state.position);
    if (ch === terminator) {
      state.position++;
      state.tag = _tag;
      state.anchor = _anchor;
      state.kind = isMapping ? 'mapping' : 'sequence';
      state.result = _result;
      return true;
    } else if (!readNext) {
      throwError(state, 'missed comma between flow collection entries');
    } else if (ch === 0x2C) {
      throwError(state, "expected the node content, but found ','");
    }
    keyTag = keyNode = valueNode = null;
    isPair = isExplicitPair = false;
    if (ch === 0x3F) {
      following = state.input.charCodeAt(state.position + 1);
      if (is_WS_OR_EOL(following)) {
        isPair = isExplicitPair = true;
        state.position++;
        skipSeparationSpace(state, true, nodeIndent);
      }
    }
    _line = state.line;
    _lineStart = state.lineStart;
    _pos = state.position;
    composeNode(state, nodeIndent, CONTEXT_FLOW_IN, false, true);
    keyTag = state.tag;
    keyNode = state.result;
    skipSeparationSpace(state, true, nodeIndent);
    ch = state.input.charCodeAt(state.position);
    if ((isExplicitPair || state.line === _line) && ch === 0x3A) {
      isPair = true;
      ch = state.input.charCodeAt(++state.position);
      skipSeparationSpace(state, true, nodeIndent);
      composeNode(state, nodeIndent, CONTEXT_FLOW_IN, false, true);
      valueNode = state.result;
    }
    if (isMapping) {
      storeMappingPair(state, _result, overridableKeys, keyTag, keyNode, valueNode, _line, _lineStart, _pos);
    } else if (isPair) {
      _result.push(storeMappingPair(state, null, overridableKeys, keyTag, keyNode, valueNode, _line, _lineStart, _pos));
    } else {
      _result.push(keyNode);
    }
    skipSeparationSpace(state, true, nodeIndent);
    ch = state.input.charCodeAt(state.position);
    if (ch === 0x2C) {
      readNext = true;
      ch = state.input.charCodeAt(++state.position);
    } else {
      readNext = false;
    }
  }
  throwError(state, 'unexpected end of the stream within a flow collection');
}
function readBlockScalar(state, nodeIndent) {
  var captureStart,
      folding,
      chomping       = CHOMPING_CLIP,
      didReadContent = false,
      detectedIndent = false,
      textIndent     = nodeIndent,
      emptyLines     = 0,
      atMoreIndented = false,
      tmp,
      ch;
  ch = state.input.charCodeAt(state.position);
  if (ch === 0x7C) {
    folding = false;
  } else if (ch === 0x3E) {
    folding = true;
  } else {
    return false;
  }
  state.kind = 'scalar';
  state.result = '';
  while (ch !== 0) {
    ch = state.input.charCodeAt(++state.position);
    if (ch === 0x2B || ch === 0x2D) {
      if (CHOMPING_CLIP === chomping) {
        chomping = (ch === 0x2B) ? CHOMPING_KEEP : CHOMPING_STRIP;
      } else {
        throwError(state, 'repeat of a chomping mode identifier');
      }
    } else if ((tmp = fromDecimalCode(ch)) >= 0) {
      if (tmp === 0) {
        throwError(state, 'bad explicit indentation width of a block scalar; it cannot be less than one');
      } else if (!detectedIndent) {
        textIndent = nodeIndent + tmp - 1;
        detectedIndent = true;
      } else {
        throwError(state, 'repeat of an indentation width identifier');
      }
    } else {
      break;
    }
  }
  if (is_WHITE_SPACE(ch)) {
    do { ch = state.input.charCodeAt(++state.position); }
    while (is_WHITE_SPACE(ch));
    if (ch === 0x23) {
      do { ch = state.input.charCodeAt(++state.position); }
      while (!is_EOL(ch) && (ch !== 0));
    }
  }
  while (ch !== 0) {
    readLineBreak(state);
    state.lineIndent = 0;
    ch = state.input.charCodeAt(state.position);
    while ((!detectedIndent || state.lineIndent < textIndent) &&
           (ch === 0x20)) {
      state.lineIndent++;
      ch = state.input.charCodeAt(++state.position);
    }
    if (!detectedIndent && state.lineIndent > textIndent) {
      textIndent = state.lineIndent;
    }
    if (is_EOL(ch)) {
      emptyLines++;
      continue;
    }
    if (state.lineIndent < textIndent) {
      if (chomping === CHOMPING_KEEP) {
        state.result += common.repeat('\n', didReadContent ? 1 + emptyLines : emptyLines);
      } else if (chomping === CHOMPING_CLIP) {
        if (didReadContent) {
          state.result += '\n';
        }
      }
      break;
    }
    if (folding) {
      if (is_WHITE_SPACE(ch)) {
        atMoreIndented = true;
        state.result += common.repeat('\n', didReadContent ? 1 + emptyLines : emptyLines);
      } else if (atMoreIndented) {
        atMoreIndented = false;
        state.result += common.repeat('\n', emptyLines + 1);
      } else if (emptyLines === 0) {
        if (didReadContent) {
          state.result += ' ';
        }
      } else {
        state.result += common.repeat('\n', emptyLines);
      }
    } else {
      state.result += common.repeat('\n', didReadContent ? 1 + emptyLines : emptyLines);
    }
    didReadContent = true;
    detectedIndent = true;
    emptyLines = 0;
    captureStart = state.position;
    while (!is_EOL(ch) && (ch !== 0)) {
      ch = state.input.charCodeAt(++state.position);
    }
    captureSegment(state, captureStart, state.position, false);
  }
  return true;
}
function readBlockSequence(state, nodeIndent) {
  var _line,
      _tag      = state.tag,
      _anchor   = state.anchor,
      _result   = [],
      following,
      detected  = false,
      ch;
  if (state.firstTabInLine !== -1) return false;
  if (state.anchor !== null) {
    state.anchorMap[state.anchor] = _result;
  }
  ch = state.input.charCodeAt(state.position);
  while (ch !== 0) {
    if (state.firstTabInLine !== -1) {
      state.position = state.firstTabInLine;
      throwError(state, 'tab characters must not be used in indentation');
    }
    if (ch !== 0x2D) {
      break;
    }
    following = state.input.charCodeAt(state.position + 1);
    if (!is_WS_OR_EOL(following)) {
      break;
    }
    detected = true;
    state.position++;
    if (skipSeparationSpace(state, true, -1)) {
      if (state.lineIndent <= nodeIndent) {
        _result.push(null);
        ch = state.input.charCodeAt(state.position);
        continue;
      }
    }
    _line = state.line;
    composeNode(state, nodeIndent, CONTEXT_BLOCK_IN, false, true);
    _result.push(state.result);
    skipSeparationSpace(state, true, -1);
    ch = state.input.charCodeAt(state.position);
    if ((state.line === _line || state.lineIndent > nodeIndent) && (ch !== 0)) {
      throwError(state, 'bad indentation of a sequence entry');
    } else if (state.lineIndent < nodeIndent) {
      break;
    }
  }
  if (detected) {
    state.tag = _tag;
    state.anchor = _anchor;
    state.kind = 'sequence';
    state.result = _result;
    return true;
  }
  return false;
}
function readBlockMapping(state, nodeIndent, flowIndent) {
  var following,
      allowCompact,
      _line,
      _keyLine,
      _keyLineStart,
      _keyPos,
      _tag          = state.tag,
      _anchor       = state.anchor,
      _result       = {},
      overridableKeys = Object.create(null),
      keyTag        = null,
      keyNode       = null,
      valueNode     = null,
      atExplicitKey = false,
      detected      = false,
      ch;
  if (state.firstTabInLine !== -1) return false;
  if (state.anchor !== null) {
    state.anchorMap[state.anchor] = _result;
  }
  ch = state.input.charCodeAt(state.position);
  while (ch !== 0) {
    if (!atExplicitKey && state.firstTabInLine !== -1) {
      state.position = state.firstTabInLine;
      throwError(state, 'tab characters must not be used in indentation');
    }
    following = state.input.charCodeAt(state.position + 1);
    _line = state.line;
    if ((ch === 0x3F || ch === 0x3A) && is_WS_OR_EOL(following)) {
      if (ch === 0x3F) {
        if (atExplicitKey) {
          storeMappingPair(state, _result, overridableKeys, keyTag, keyNode, null, _keyLine, _keyLineStart, _keyPos);
          keyTag = keyNode = valueNode = null;
        }
        detected = true;
        atExplicitKey = true;
        allowCompact = true;
      } else if (atExplicitKey) {
        atExplicitKey = false;
        allowCompact = true;
      } else {
        throwError(state, 'incomplete explicit mapping pair; a key node is missed; or followed by a non-tabulated empty line');
      }
      state.position += 1;
      ch = following;
    } else {
      _keyLine = state.line;
      _keyLineStart = state.lineStart;
      _keyPos = state.position;
      if (!composeNode(state, flowIndent, CONTEXT_FLOW_OUT, false, true)) {
        break;
      }
      if (state.line === _line) {
        ch = state.input.charCodeAt(state.position);
        while (is_WHITE_SPACE(ch)) {
          ch = state.input.charCodeAt(++state.position);
        }
        if (ch === 0x3A) {
          ch = state.input.charCodeAt(++state.position);
          if (!is_WS_OR_EOL(ch)) {
            throwError(state, 'a whitespace character is expected after the key-value separator within a block mapping');
          }
          if (atExplicitKey) {
            storeMappingPair(state, _result, overridableKeys, keyTag, keyNode, null, _keyLine, _keyLineStart, _keyPos);
            keyTag = keyNode = valueNode = null;
          }
          detected = true;
          atExplicitKey = false;
          allowCompact = false;
          keyTag = state.tag;
          keyNode = state.result;
        } else if (detected) {
          throwError(state, 'can not read an implicit mapping pair; a colon is missed');
        } else {
          state.tag = _tag;
          state.anchor = _anchor;
          return true;
        }
      } else if (detected) {
        throwError(state, 'can not read a block mapping entry; a multiline key may not be an implicit key');
      } else {
        state.tag = _tag;
        state.anchor = _anchor;
        return true;
      }
    }
    if (state.line === _line || state.lineIndent > nodeIndent) {
      if (atExplicitKey) {
        _keyLine = state.line;
        _keyLineStart = state.lineStart;
        _keyPos = state.position;
      }
      if (composeNode(state, nodeIndent, CONTEXT_BLOCK_OUT, true, allowCompact)) {
        if (atExplicitKey) {
          keyNode = state.result;
        } else {
          valueNode = state.result;
        }
      }
      if (!atExplicitKey) {
        storeMappingPair(state, _result, overridableKeys, keyTag, keyNode, valueNode, _keyLine, _keyLineStart, _keyPos);
        keyTag = keyNode = valueNode = null;
      }
      skipSeparationSpace(state, true, -1);
      ch = state.input.charCodeAt(state.position);
    }
    if ((state.line === _line || state.lineIndent > nodeIndent) && (ch !== 0)) {
      throwError(state, 'bad indentation of a mapping entry');
    } else if (state.lineIndent < nodeIndent) {
      break;
    }
  }
  if (atExplicitKey) {
    storeMappingPair(state, _result, overridableKeys, keyTag, keyNode, null, _keyLine, _keyLineStart, _keyPos);
  }
  if (detected) {
    state.tag = _tag;
    state.anchor = _anchor;
    state.kind = 'mapping';
    state.result = _result;
  }
  return detected;
}
function readTagProperty(state) {
  var _position,
      isVerbatim = false,
      isNamed    = false,
      tagHandle,
      tagName,
      ch;
  ch = state.input.charCodeAt(state.position);
  if (ch !== 0x21) return false;
  if (state.tag !== null) {
    throwError(state, 'duplication of a tag property');
  }
  ch = state.input.charCodeAt(++state.position);
  if (ch === 0x3C) {
    isVerbatim = true;
    ch = state.input.charCodeAt(++state.position);
  } else if (ch === 0x21) {
    isNamed = true;
    tagHandle = '!!';
    ch = state.input.charCodeAt(++state.position);
  } else {
    tagHandle = '!';
  }
  _position = state.position;
  if (isVerbatim) {
    do { ch = state.input.charCodeAt(++state.position); }
    while (ch !== 0 && ch !== 0x3E);
    if (state.position < state.length) {
      tagName = state.input.slice(_position, state.position);
      ch = state.input.charCodeAt(++state.position);
    } else {
      throwError(state, 'unexpected end of the stream within a verbatim tag');
    }
  } else {
    while (ch !== 0 && !is_WS_OR_EOL(ch)) {
      if (ch === 0x21) {
        if (!isNamed) {
          tagHandle = state.input.slice(_position - 1, state.position + 1);
          if (!PATTERN_TAG_HANDLE.test(tagHandle)) {
            throwError(state, 'named tag handle cannot contain such characters');
          }
          isNamed = true;
          _position = state.position + 1;
        } else {
          throwError(state, 'tag suffix cannot contain exclamation marks');
        }
      }
      ch = state.input.charCodeAt(++state.position);
    }
    tagName = state.input.slice(_position, state.position);
    if (PATTERN_FLOW_INDICATORS.test(tagName)) {
      throwError(state, 'tag suffix cannot contain flow indicator characters');
    }
  }
  if (tagName && !PATTERN_TAG_URI.test(tagName)) {
    throwError(state, 'tag name cannot contain such characters: ' + tagName);
  }
  try {
    tagName = decodeURIComponent(tagName);
  } catch (err) {
    throwError(state, 'tag name is malformed: ' + tagName);
  }
  if (isVerbatim) {
    state.tag = tagName;
  } else if (_hasOwnProperty$1.call(state.tagMap, tagHandle)) {
    state.tag = state.tagMap[tagHandle] + tagName;
  } else if (tagHandle === '!') {
    state.tag = '!' + tagName;
  } else if (tagHandle === '!!') {
    state.tag = 'tag:yaml.org,2002:' + tagName;
  } else {
    throwError(state, 'undeclared tag handle "' + tagHandle + '"');
  }
  return true;
}
function readAnchorProperty(state) {
  var _position,
      ch;
  ch = state.input.charCodeAt(state.position);
  if (ch !== 0x26) return false;
  if (state.anchor !== null) {
    throwError(state, 'duplication of an anchor property');
  }
  ch = state.input.charCodeAt(++state.position);
  _position = state.position;
  while (ch !== 0 && !is_WS_OR_EOL(ch) && !is_FLOW_INDICATOR(ch)) {
    ch = state.input.charCodeAt(++state.position);
  }
  if (state.position === _position) {
    throwError(state, 'name of an anchor node must contain at least one character');
  }
  state.anchor = state.input.slice(_position, state.position);
  return true;
}
function readAlias(state) {
  var _position, alias,
      ch;
  ch = state.input.charCodeAt(state.position);
  if (ch !== 0x2A) return false;
  ch = state.input.charCodeAt(++state.position);
  _position = state.position;
  while (ch !== 0 && !is_WS_OR_EOL(ch) && !is_FLOW_INDICATOR(ch)) {
    ch = state.input.charCodeAt(++state.position);
  }
  if (state.position === _position) {
    throwError(state, 'name of an alias node must contain at least one character');
  }
  alias = state.input.slice(_position, state.position);
  if (!_hasOwnProperty$1.call(state.anchorMap, alias)) {
    throwError(state, 'unidentified alias "' + alias + '"');
  }
  state.result = state.anchorMap[alias];
  skipSeparationSpace(state, true, -1);
  return true;
}
function composeNode(state, parentIndent, nodeContext, allowToSeek, allowCompact) {
  var allowBlockStyles,
      allowBlockScalars,
      allowBlockCollections,
      indentStatus = 1,
      atNewLine  = false,
      hasContent = false,
      typeIndex,
      typeQuantity,
      typeList,
      type,
      flowIndent,
      blockIndent;
  if (state.listener !== null) {
    state.listener('open', state);
  }
  state.tag    = null;
  state.anchor = null;
  state.kind   = null;
  state.result = null;
  allowBlockStyles = allowBlockScalars = allowBlockCollections =
    CONTEXT_BLOCK_OUT === nodeContext ||
    CONTEXT_BLOCK_IN  === nodeContext;
  if (allowToSeek) {
    if (skipSeparationSpace(state, true, -1)) {
      atNewLine = true;
      if (state.lineIndent > parentIndent) {
        indentStatus = 1;
      } else if (state.lineIndent === parentIndent) {
        indentStatus = 0;
      } else if (state.lineIndent < parentIndent) {
        indentStatus = -1;
      }
    }
  }
  if (indentStatus === 1) {
    while (readTagProperty(state) || readAnchorProperty(state)) {
      if (skipSeparationSpace(state, true, -1)) {
        atNewLine = true;
        allowBlockCollections = allowBlockStyles;
        if (state.lineIndent > parentIndent) {
          indentStatus = 1;
        } else if (state.lineIndent === parentIndent) {
          indentStatus = 0;
        } else if (state.lineIndent < parentIndent) {
          indentStatus = -1;
        }
      } else {
        allowBlockCollections = false;
      }
    }
  }
  if (allowBlockCollections) {
    allowBlockCollections = atNewLine || allowCompact;
  }
  if (indentStatus === 1 || CONTEXT_BLOCK_OUT === nodeContext) {
    if (CONTEXT_FLOW_IN === nodeContext || CONTEXT_FLOW_OUT === nodeContext) {
      flowIndent = parentIndent;
    } else {
      flowIndent = parentIndent + 1;
    }
    blockIndent = state.position - state.lineStart;
    if (indentStatus === 1) {
      if (allowBlockCollections &&
          (readBlockSequence(state, blockIndent) ||
           readBlockMapping(state, blockIndent, flowIndent)) ||
          readFlowCollection(state, flowIndent)) {
        hasContent = true;
      } else {
        if ((allowBlockScalars && readBlockScalar(state, flowIndent)) ||
            readSingleQuotedScalar(state, flowIndent) ||
            readDoubleQuotedScalar(state, flowIndent)) {
          hasContent = true;
        } else if (readAlias(state)) {
          hasContent = true;
          if (state.tag !== null || state.anchor !== null) {
            throwError(state, 'alias node should not have any properties');
          }
        } else if (readPlainScalar(state, flowIndent, CONTEXT_FLOW_IN === nodeContext)) {
          hasContent = true;
          if (state.tag === null) {
            state.tag = '?';
          }
        }
        if (state.anchor !== null) {
          state.anchorMap[state.anchor] = state.result;
        }
      }
    } else if (indentStatus === 0) {
      hasContent = allowBlockCollections && readBlockSequence(state, blockIndent);
    }
  }
  if (state.tag === null) {
    if (state.anchor !== null) {
      state.anchorMap[state.anchor] = state.result;
    }
  } else if (state.tag === '?') {
    if (state.result !== null && state.kind !== 'scalar') {
      throwError(state, 'unacceptable node kind for !<?> tag; it should be "scalar", not "' + state.kind + '"');
    }
    for (typeIndex = 0, typeQuantity = state.implicitTypes.length; typeIndex < typeQuantity; typeIndex += 1) {
      type = state.implicitTypes[typeIndex];
      if (type.resolve(state.result)) {
        state.result = type.construct(state.result);
        state.tag = type.tag;
        if (state.anchor !== null) {
          state.anchorMap[state.anchor] = state.result;
        }
        break;
      }
    }
  } else if (state.tag !== '!') {
    if (_hasOwnProperty$1.call(state.typeMap[state.kind || 'fallback'], state.tag)) {
      type = state.typeMap[state.kind || 'fallback'][state.tag];
    } else {
      type = null;
      typeList = state.typeMap.multi[state.kind || 'fallback'];
      for (typeIndex = 0, typeQuantity = typeList.length; typeIndex < typeQuantity; typeIndex += 1) {
        if (state.tag.slice(0, typeList[typeIndex].tag.length) === typeList[typeIndex].tag) {
          type = typeList[typeIndex];
          break;
        }
      }
    }
    if (!type) {
      throwError(state, 'unknown tag !<' + state.tag + '>');
    }
    if (state.result !== null && type.kind !== state.kind) {
      throwError(state, 'unacceptable node kind for !<' + state.tag + '> tag; it should be "' + type.kind + '", not "' + state.kind + '"');
    }
    if (!type.resolve(state.result, state.tag)) {
      throwError(state, 'cannot resolve a node with !<' + state.tag + '> explicit tag');
    } else {
      state.result = type.construct(state.result, state.tag);
      if (state.anchor !== null) {
        state.anchorMap[state.anchor] = state.result;
      }
    }
  }
  if (state.listener !== null) {
    state.listener('close', state);
  }
  return state.tag !== null ||  state.anchor !== null || hasContent;
}
function readDocument(state) {
  var documentStart = state.position,
      _position,
      directiveName,
      directiveArgs,
      hasDirectives = false,
      ch;
  state.version = null;
  state.checkLineBreaks = state.legacy;
  state.tagMap = Object.create(null);
  state.anchorMap = Object.create(null);
  while ((ch = state.input.charCodeAt(state.position)) !== 0) {
    skipSeparationSpace(state, true, -1);
    ch = state.input.charCodeAt(state.position);
    if (state.lineIndent > 0 || ch !== 0x25) {
      break;
    }
    hasDirectives = true;
    ch = state.input.charCodeAt(++state.position);
    _position = state.position;
    while (ch !== 0 && !is_WS_OR_EOL(ch)) {
      ch = state.input.charCodeAt(++state.position);
    }
    directiveName = state.input.slice(_position, state.position);
    directiveArgs = [];
    if (directiveName.length < 1) {
      throwError(state, 'directive name must not be less than one character in length');
    }
    while (ch !== 0) {
      while (is_WHITE_SPACE(ch)) {
        ch = state.input.charCodeAt(++state.position);
      }
      if (ch === 0x23) {
        do { ch = state.input.charCodeAt(++state.position); }
        while (ch !== 0 && !is_EOL(ch));
        break;
      }
      if (is_EOL(ch)) break;
      _position = state.position;
      while (ch !== 0 && !is_WS_OR_EOL(ch)) {
        ch = state.input.charCodeAt(++state.position);
      }
      directiveArgs.push(state.input.slice(_position, state.position));
    }
    if (ch !== 0) readLineBreak(state);
    if (_hasOwnProperty$1.call(directiveHandlers, directiveName)) {
      directiveHandlers[directiveName](state, directiveName, directiveArgs);
    } else {
      throwWarning(state, 'unknown document directive "' + directiveName + '"');
    }
  }
  skipSeparationSpace(state, true, -1);
  if (state.lineIndent === 0 &&
      state.input.charCodeAt(state.position)     === 0x2D &&
      state.input.charCodeAt(state.position + 1) === 0x2D &&
      state.input.charCodeAt(state.position + 2) === 0x2D) {
    state.position += 3;
    skipSeparationSpace(state, true, -1);
  } else if (hasDirectives) {
    throwError(state, 'directives end mark is expected');
  }
  composeNode(state, state.lineIndent - 1, CONTEXT_BLOCK_OUT, false, true);
  skipSeparationSpace(state, true, -1);
  if (state.checkLineBreaks &&
      PATTERN_NON_ASCII_LINE_BREAKS.test(state.input.slice(documentStart, state.position))) {
    throwWarning(state, 'non-ASCII line breaks are interpreted as content');
  }
  state.documents.push(state.result);
  if (state.position === state.lineStart && testDocumentSeparator(state)) {
    if (state.input.charCodeAt(state.position) === 0x2E) {
      state.position += 3;
      skipSeparationSpace(state, true, -1);
    }
    return;
  }
  if (state.position < (state.length - 1)) {
    throwError(state, 'end of the stream or a document separator is expected');
  } else {
    return;
  }
}
function loadDocuments(input, options) {
  input = String(input);
  options = options || {};
  if (input.length !== 0) {
    if (input.charCodeAt(input.length - 1) !== 0x0A &&
        input.charCodeAt(input.length - 1) !== 0x0D) {
      input += '\n';
    }
    if (input.charCodeAt(0) === 0xFEFF) {
      input = input.slice(1);
    }
  }
  var state = new State$1(input, options);
  var nullpos = input.indexOf('\0');
  if (nullpos !== -1) {
    state.position = nullpos;
    throwError(state, 'null byte is not allowed in input');
  }
  state.input += '\0';
  while (state.input.charCodeAt(state.position) === 0x20) {
    state.lineIndent += 1;
    state.position += 1;
  }
  while (state.position < (state.length - 1)) {
    readDocument(state);
  }
  return state.documents;
}
function loadAll$1(input, iterator, options) {
  if (iterator !== null && typeof iterator === 'object' && typeof options === 'undefined') {
    options = iterator;
    iterator = null;
  }
  var documents = loadDocuments(input, options);
  if (typeof iterator !== 'function') {
    return documents;
  }
  for (var index = 0, length = documents.length; index < length; index += 1) {
    iterator(documents[index]);
  }
}
function load$1(input, options) {
  var documents = loadDocuments(input, options);
  if (documents.length === 0) {
    return undefined;
  } else if (documents.length === 1) {
    return documents[0];
  }
  throw new exception('expected a single document in the stream, but found more');
}
var loadAll_1 = loadAll$1;
var load_1    = load$1;
var loader = {
	loadAll: loadAll_1,
	load: load_1
};
var _toString       = Object.prototype.toString;
var _hasOwnProperty = Object.prototype.hasOwnProperty;
var CHAR_BOM                  = 0xFEFF;
var CHAR_TAB                  = 0x09;
var CHAR_LINE_FEED            = 0x0A;
var CHAR_CARRIAGE_RETURN      = 0x0D;
var CHAR_SPACE                = 0x20;
var CHAR_EXCLAMATION          = 0x21;
var CHAR_DOUBLE_QUOTE         = 0x22;
var CHAR_SHARP                = 0x23;
var CHAR_PERCENT              = 0x25;
var CHAR_AMPERSAND            = 0x26;
var CHAR_SINGLE_QUOTE         = 0x27;
var CHAR_ASTERISK             = 0x2A;
var CHAR_COMMA                = 0x2C;
var CHAR_MINUS                = 0x2D;
var CHAR_COLON                = 0x3A;
var CHAR_EQUALS               = 0x3D;
var CHAR_GREATER_THAN         = 0x3E;
var CHAR_QUESTION             = 0x3F;
var CHAR_COMMERCIAL_AT        = 0x40;
var CHAR_LEFT_SQUARE_BRACKET  = 0x5B;
var CHAR_RIGHT_SQUARE_BRACKET = 0x5D;
var CHAR_GRAVE_ACCENT         = 0x60;
var CHAR_LEFT_CURLY_BRACKET   = 0x7B;
var CHAR_VERTICAL_LINE        = 0x7C;
var CHAR_RIGHT_CURLY_BRACKET  = 0x7D;
var ESCAPE_SEQUENCES = {};
ESCAPE_SEQUENCES[0x00]   = '\\0';
ESCAPE_SEQUENCES[0x07]   = '\\a';
ESCAPE_SEQUENCES[0x08]   = '\\b';
ESCAPE_SEQUENCES[0x09]   = '\\t';
ESCAPE_SEQUENCES[0x0A]   = '\\n';
ESCAPE_SEQUENCES[0x0B]   = '\\v';
ESCAPE_SEQUENCES[0x0C]   = '\\f';
ESCAPE_SEQUENCES[0x0D]   = '\\r';
ESCAPE_SEQUENCES[0x1B]   = '\\e';
ESCAPE_SEQUENCES[0x22]   = '\\"';
ESCAPE_SEQUENCES[0x5C]   = '\\\\';
ESCAPE_SEQUENCES[0x85]   = '\\N';
ESCAPE_SEQUENCES[0xA0]   = '\\_';
ESCAPE_SEQUENCES[0x2028] = '\\L';
ESCAPE_SEQUENCES[0x2029] = '\\P';
var DEPRECATED_BOOLEANS_SYNTAX = [
  'y', 'Y', 'yes', 'Yes', 'YES', 'on', 'On', 'ON',
  'n', 'N', 'no', 'No', 'NO', 'off', 'Off', 'OFF'
];
var DEPRECATED_BASE60_SYNTAX = /^[-+]?[0-9_]+(?::[0-9_]+)+(?:\.[0-9_]*)?$/;
function compileStyleMap(schema, map) {
  var result, keys, index, length, tag, style, type;
  if (map === null) return {};
  result = {};
  keys = Object.keys(map);
  for (index = 0, length = keys.length; index < length; index += 1) {
    tag = keys[index];
    style = String(map[tag]);
    if (tag.slice(0, 2) === '!!') {
      tag = 'tag:yaml.org,2002:' + tag.slice(2);
    }
    type = schema.compiledTypeMap['fallback'][tag];
    if (type && _hasOwnProperty.call(type.styleAliases, style)) {
      style = type.styleAliases[style];
    }
    result[tag] = style;
  }
  return result;
}
function encodeHex(character) {
  var string, handle, length;
  string = character.toString(16).toUpperCase();
  if (character <= 0xFF) {
    handle = 'x';
    length = 2;
  } else if (character <= 0xFFFF) {
    handle = 'u';
    length = 4;
  } else if (character <= 0xFFFFFFFF) {
    handle = 'U';
    length = 8;
  } else {
    throw new exception('code point within a string may not be greater than 0xFFFFFFFF');
  }
  return '\\' + handle + common.repeat('0', length - string.length) + string;
}
var QUOTING_TYPE_SINGLE = 1,
    QUOTING_TYPE_DOUBLE = 2;
function State(options) {
  this.schema        = options['schema'] || _default;
  this.indent        = Math.max(1, (options['indent'] || 2));
  this.noArrayIndent = options['noArrayIndent'] || false;
  this.skipInvalid   = options['skipInvalid'] || false;
  this.flowLevel     = (common.isNothing(options['flowLevel']) ? -1 : options['flowLevel']);
  this.styleMap      = compileStyleMap(this.schema, options['styles'] || null);
  this.sortKeys      = options['sortKeys'] || false;
  this.lineWidth     = options['lineWidth'] || 80;
  this.noRefs        = options['noRefs'] || false;
  this.noCompatMode  = options['noCompatMode'] || false;
  this.condenseFlow  = options['condenseFlow'] || false;
  this.quotingType   = options['quotingType'] === '"' ? QUOTING_TYPE_DOUBLE : QUOTING_TYPE_SINGLE;
  this.forceQuotes   = options['forceQuotes'] || false;
  this.replacer      = typeof options['replacer'] === 'function' ? options['replacer'] : null;
  this.implicitTypes = this.schema.compiledImplicit;
  this.explicitTypes = this.schema.compiledExplicit;
  this.tag = null;
  this.result = '';
  this.duplicates = [];
  this.usedDuplicates = null;
}
function indentString(string, spaces) {
  var ind = common.repeat(' ', spaces),
      position = 0,
      next = -1,
      result = '',
      line,
      length = string.length;
  while (position < length) {
    next = string.indexOf('\n', position);
    if (next === -1) {
      line = string.slice(position);
      position = length;
    } else {
      line = string.slice(position, next + 1);
      position = next + 1;
    }
    if (line.length && line !== '\n') result += ind;
    result += line;
  }
  return result;
}
function generateNextLine(state, level) {
  return '\n' + common.repeat(' ', state.indent * level);
}
function testImplicitResolving(state, str) {
  var index, length, type;
  for (index = 0, length = state.implicitTypes.length; index < length; index += 1) {
    type = state.implicitTypes[index];
    if (type.resolve(str)) {
      return true;
    }
  }
  return false;
}
function isWhitespace(c) {
  return c === CHAR_SPACE || c === CHAR_TAB;
}
function isPrintable(c) {
  return  (0x00020 <= c && c <= 0x00007E)
      || ((0x000A1 <= c && c <= 0x00D7FF) && c !== 0x2028 && c !== 0x2029)
      || ((0x0E000 <= c && c <= 0x00FFFD) && c !== CHAR_BOM)
      ||  (0x10000 <= c && c <= 0x10FFFF);
}
function isNsCharOrWhitespace(c) {
  return isPrintable(c)
    && c !== CHAR_BOM
    && c !== CHAR_CARRIAGE_RETURN
    && c !== CHAR_LINE_FEED;
}
function isPlainSafe(c, prev, inblock) {
  var cIsNsCharOrWhitespace = isNsCharOrWhitespace(c);
  var cIsNsChar = cIsNsCharOrWhitespace && !isWhitespace(c);
  return (
    inblock ?
      cIsNsCharOrWhitespace
      : cIsNsCharOrWhitespace
        && c !== CHAR_COMMA
        && c !== CHAR_LEFT_SQUARE_BRACKET
        && c !== CHAR_RIGHT_SQUARE_BRACKET
        && c !== CHAR_LEFT_CURLY_BRACKET
        && c !== CHAR_RIGHT_CURLY_BRACKET
  )
    && c !== CHAR_SHARP
    && !(prev === CHAR_COLON && !cIsNsChar)
    || (isNsCharOrWhitespace(prev) && !isWhitespace(prev) && c === CHAR_SHARP)
    || (prev === CHAR_COLON && cIsNsChar);
}
function isPlainSafeFirst(c) {
  return isPrintable(c) && c !== CHAR_BOM
    && !isWhitespace(c)
    && c !== CHAR_MINUS
    && c !== CHAR_QUESTION
    && c !== CHAR_COLON
    && c !== CHAR_COMMA
    && c !== CHAR_LEFT_SQUARE_BRACKET
    && c !== CHAR_RIGHT_SQUARE_BRACKET
    && c !== CHAR_LEFT_CURLY_BRACKET
    && c !== CHAR_RIGHT_CURLY_BRACKET
    && c !== CHAR_SHARP
    && c !== CHAR_AMPERSAND
    && c !== CHAR_ASTERISK
    && c !== CHAR_EXCLAMATION
    && c !== CHAR_VERTICAL_LINE
    && c !== CHAR_EQUALS
    && c !== CHAR_GREATER_THAN
    && c !== CHAR_SINGLE_QUOTE
    && c !== CHAR_DOUBLE_QUOTE
    && c !== CHAR_PERCENT
    && c !== CHAR_COMMERCIAL_AT
    && c !== CHAR_GRAVE_ACCENT;
}
function isPlainSafeLast(c) {
  return !isWhitespace(c) && c !== CHAR_COLON;
}
function codePointAt(string, pos) {
  var first = string.charCodeAt(pos), second;
  if (first >= 0xD800 && first <= 0xDBFF && pos + 1 < string.length) {
    second = string.charCodeAt(pos + 1);
    if (second >= 0xDC00 && second <= 0xDFFF) {
      return (first - 0xD800) * 0x400 + second - 0xDC00 + 0x10000;
    }
  }
  return first;
}
function needIndentIndicator(string) {
  var leadingSpaceRe = /^\n* /;
  return leadingSpaceRe.test(string);
}
var STYLE_PLAIN   = 1,
    STYLE_SINGLE  = 2,
    STYLE_LITERAL = 3,
    STYLE_FOLDED  = 4,
    STYLE_DOUBLE  = 5;
function chooseScalarStyle(string, singleLineOnly, indentPerLevel, lineWidth,
  testAmbiguousType, quotingType, forceQuotes, inblock) {
  var i;
  var char = 0;
  var prevChar = null;
  var hasLineBreak = false;
  var hasFoldableLine = false;
  var shouldTrackWidth = lineWidth !== -1;
  var previousLineBreak = -1;
  var plain = isPlainSafeFirst(codePointAt(string, 0))
          && isPlainSafeLast(codePointAt(string, string.length - 1));
  if (singleLineOnly || forceQuotes) {
    for (i = 0; i < string.length; char >= 0x10000 ? i += 2 : i++) {
      char = codePointAt(string, i);
      if (!isPrintable(char)) {
        return STYLE_DOUBLE;
      }
      plain = plain && isPlainSafe(char, prevChar, inblock);
      prevChar = char;
    }
  } else {
    for (i = 0; i < string.length; char >= 0x10000 ? i += 2 : i++) {
      char = codePointAt(string, i);
      if (char === CHAR_LINE_FEED) {
        hasLineBreak = true;
        if (shouldTrackWidth) {
          hasFoldableLine = hasFoldableLine ||
            (i - previousLineBreak - 1 > lineWidth &&
             string[previousLineBreak + 1] !== ' ');
          previousLineBreak = i;
        }
      } else if (!isPrintable(char)) {
        return STYLE_DOUBLE;
      }
      plain = plain && isPlainSafe(char, prevChar, inblock);
      prevChar = char;
    }
    hasFoldableLine = hasFoldableLine || (shouldTrackWidth &&
      (i - previousLineBreak - 1 > lineWidth &&
       string[previousLineBreak + 1] !== ' '));
  }
  if (!hasLineBreak && !hasFoldableLine) {
    if (plain && !forceQuotes && !testAmbiguousType(string)) {
      return STYLE_PLAIN;
    }
    return quotingType === QUOTING_TYPE_DOUBLE ? STYLE_DOUBLE : STYLE_SINGLE;
  }
  if (indentPerLevel > 9 && needIndentIndicator(string)) {
    return STYLE_DOUBLE;
  }
  if (!forceQuotes) {
    return hasFoldableLine ? STYLE_FOLDED : STYLE_LITERAL;
  }
  return quotingType === QUOTING_TYPE_DOUBLE ? STYLE_DOUBLE : STYLE_SINGLE;
}
function writeScalar(state, string, level, iskey, inblock) {
  state.dump = (function () {
    if (string.length === 0) {
      return state.quotingType === QUOTING_TYPE_DOUBLE ? '""' : "''";
    }
    if (!state.noCompatMode) {
      if (DEPRECATED_BOOLEANS_SYNTAX.indexOf(string) !== -1 || DEPRECATED_BASE60_SYNTAX.test(string)) {
        return state.quotingType === QUOTING_TYPE_DOUBLE ? ('"' + string + '"') : ("'" + string + "'");
      }
    }
    var indent = state.indent * Math.max(1, level);
    var lineWidth = state.lineWidth === -1
      ? -1 : Math.max(Math.min(state.lineWidth, 40), state.lineWidth - indent);
    var singleLineOnly = iskey
      || (state.flowLevel > -1 && level >= state.flowLevel);
    function testAmbiguity(string) {
      return testImplicitResolving(state, string);
    }
    switch (chooseScalarStyle(string, singleLineOnly, state.indent, lineWidth,
      testAmbiguity, state.quotingType, state.forceQuotes && !iskey, inblock)) {
      case STYLE_PLAIN:
        return string;
      case STYLE_SINGLE:
        return "'" + string.replace(/'/g, "''") + "'";
      case STYLE_LITERAL:
        return '|' + blockHeader(string, state.indent)
          + dropEndingNewline(indentString(string, indent));
      case STYLE_FOLDED:
        return '>' + blockHeader(string, state.indent)
          + dropEndingNewline(indentString(foldString(string, lineWidth), indent));
      case STYLE_DOUBLE:
        return '"' + escapeString(string) + '"';
      default:
        throw new exception('impossible error: invalid scalar style');
    }
  }());
}
function blockHeader(string, indentPerLevel) {
  var indentIndicator = needIndentIndicator(string) ? String(indentPerLevel) : '';
  var clip =          string[string.length - 1] === '\n';
  var keep = clip && (string[string.length - 2] === '\n' || string === '\n');
  var chomp = keep ? '+' : (clip ? '' : '-');
  return indentIndicator + chomp + '\n';
}
function dropEndingNewline(string) {
  return string[string.length - 1] === '\n' ? string.slice(0, -1) : string;
}
function foldString(string, width) {
  var lineRe = /(\n+)([^\n]*)/g;
  var result = (function () {
    var nextLF = string.indexOf('\n');
    nextLF = nextLF !== -1 ? nextLF : string.length;
    lineRe.lastIndex = nextLF;
    return foldLine(string.slice(0, nextLF), width);
  }());
  var prevMoreIndented = string[0] === '\n' || string[0] === ' ';
  var moreIndented;
  var match;
  while ((match = lineRe.exec(string))) {
    var prefix = match[1], line = match[2];
    moreIndented = (line[0] === ' ');
    result += prefix
      + (!prevMoreIndented && !moreIndented && line !== ''
        ? '\n' : '')
      + foldLine(line, width);
    prevMoreIndented = moreIndented;
  }
  return result;
}
function foldLine(line, width) {
  if (line === '' || line[0] === ' ') return line;
  var breakRe = / [^ ]/g;
  var match;
  var start = 0, end, curr = 0, next = 0;
  var result = '';
  while ((match = breakRe.exec(line))) {
    next = match.index;
    if (next - start > width) {
      end = (curr > start) ? curr : next;
      result += '\n' + line.slice(start, end);
      start = end + 1;
    }
    curr = next;
  }
  result += '\n';
  if (line.length - start > width && curr > start) {
    result += line.slice(start, curr) + '\n' + line.slice(curr + 1);
  } else {
    result += line.slice(start);
  }
  return result.slice(1);
}
function escapeString(string) {
  var result = '';
  var char = 0;
  var escapeSeq;
  for (var i = 0; i < string.length; char >= 0x10000 ? i += 2 : i++) {
    char = codePointAt(string, i);
    escapeSeq = ESCAPE_SEQUENCES[char];
    if (!escapeSeq && isPrintable(char)) {
      result += string[i];
      if (char >= 0x10000) result += string[i + 1];
    } else {
      result += escapeSeq || encodeHex(char);
    }
  }
  return result;
}
function writeFlowSequence(state, level, object) {
  var _result = '',
      _tag    = state.tag,
      index,
      length,
      value;
  for (index = 0, length = object.length; index < length; index += 1) {
    value = object[index];
    if (state.replacer) {
      value = state.replacer.call(object, String(index), value);
    }
    if (writeNode(state, level, value, false, false) ||
        (typeof value === 'undefined' &&
         writeNode(state, level, null, false, false))) {
      if (_result !== '') _result += ',' + (!state.condenseFlow ? ' ' : '');
      _result += state.dump;
    }
  }
  state.tag = _tag;
  state.dump = '[' + _result + ']';
}
function writeBlockSequence(state, level, object, compact) {
  var _result = '',
      _tag    = state.tag,
      index,
      length,
      value;
  for (index = 0, length = object.length; index < length; index += 1) {
    value = object[index];
    if (state.replacer) {
      value = state.replacer.call(object, String(index), value);
    }
    if (writeNode(state, level + 1, value, true, true, false, true) ||
        (typeof value === 'undefined' &&
         writeNode(state, level + 1, null, true, true, false, true))) {
      if (!compact || _result !== '') {
        _result += generateNextLine(state, level);
      }
      if (state.dump && CHAR_LINE_FEED === state.dump.charCodeAt(0)) {
        _result += '-';
      } else {
        _result += '- ';
      }
      _result += state.dump;
    }
  }
  state.tag = _tag;
  state.dump = _result || '[]';
}
function writeFlowMapping(state, level, object) {
  var _result       = '',
      _tag          = state.tag,
      objectKeyList = Object.keys(object),
      index,
      length,
      objectKey,
      objectValue,
      pairBuffer;
  for (index = 0, length = objectKeyList.length; index < length; index += 1) {
    pairBuffer = '';
    if (_result !== '') pairBuffer += ', ';
    if (state.condenseFlow) pairBuffer += '"';
    objectKey = objectKeyList[index];
    objectValue = object[objectKey];
    if (state.replacer) {
      objectValue = state.replacer.call(object, objectKey, objectValue);
    }
    if (!writeNode(state, level, objectKey, false, false)) {
      continue;
    }
    if (state.dump.length > 1024) pairBuffer += '? ';
    pairBuffer += state.dump + (state.condenseFlow ? '"' : '') + ':' + (state.condenseFlow ? '' : ' ');
    if (!writeNode(state, level, objectValue, false, false)) {
      continue;
    }
    pairBuffer += state.dump;
    _result += pairBuffer;
  }
  state.tag = _tag;
  state.dump = '{' + _result + '}';
}
function writeBlockMapping(state, level, object, compact) {
  var _result       = '',
      _tag          = state.tag,
      objectKeyList = Object.keys(object),
      index,
      length,
      objectKey,
      objectValue,
      explicitPair,
      pairBuffer;
  if (state.sortKeys === true) {
    objectKeyList.sort();
  } else if (typeof state.sortKeys === 'function') {
    objectKeyList.sort(state.sortKeys);
  } else if (state.sortKeys) {
    throw new exception('sortKeys must be a boolean or a function');
  }
  for (index = 0, length = objectKeyList.length; index < length; index += 1) {
    pairBuffer = '';
    if (!compact || _result !== '') {
      pairBuffer += generateNextLine(state, level);
    }
    objectKey = objectKeyList[index];
    objectValue = object[objectKey];
    if (state.replacer) {
      objectValue = state.replacer.call(object, objectKey, objectValue);
    }
    if (!writeNode(state, level + 1, objectKey, true, true, true)) {
      continue;
    }
    explicitPair = (state.tag !== null && state.tag !== '?') ||
                   (state.dump && state.dump.length > 1024);
    if (explicitPair) {
      if (state.dump && CHAR_LINE_FEED === state.dump.charCodeAt(0)) {
        pairBuffer += '?';
      } else {
        pairBuffer += '? ';
      }
    }
    pairBuffer += state.dump;
    if (explicitPair) {
      pairBuffer += generateNextLine(state, level);
    }
    if (!writeNode(state, level + 1, objectValue, true, explicitPair)) {
      continue;
    }
    if (state.dump && CHAR_LINE_FEED === state.dump.charCodeAt(0)) {
      pairBuffer += ':';
    } else {
      pairBuffer += ': ';
    }
    pairBuffer += state.dump;
    _result += pairBuffer;
  }
  state.tag = _tag;
  state.dump = _result || '{}';
}
function detectType(state, object, explicit) {
  var _result, typeList, index, length, type, style;
  typeList = explicit ? state.explicitTypes : state.implicitTypes;
  for (index = 0, length = typeList.length; index < length; index += 1) {
    type = typeList[index];
    if ((type.instanceOf  || type.predicate) &&
        (!type.instanceOf || ((typeof object === 'object') && (object instanceof type.instanceOf))) &&
        (!type.predicate  || type.predicate(object))) {
      if (explicit) {
        if (type.multi && type.representName) {
          state.tag = type.representName(object);
        } else {
          state.tag = type.tag;
        }
      } else {
        state.tag = '?';
      }
      if (type.represent) {
        style = state.styleMap[type.tag] || type.defaultStyle;
        if (_toString.call(type.represent) === '[object Function]') {
          _result = type.represent(object, style);
        } else if (_hasOwnProperty.call(type.represent, style)) {
          _result = type.represent[style](object, style);
        } else {
          throw new exception('!<' + type.tag + '> tag resolver accepts not "' + style + '" style');
        }
        state.dump = _result;
      }
      return true;
    }
  }
  return false;
}
function writeNode(state, level, object, block, compact, iskey, isblockseq) {
  state.tag = null;
  state.dump = object;
  if (!detectType(state, object, false)) {
    detectType(state, object, true);
  }
  var type = _toString.call(state.dump);
  var inblock = block;
  var tagStr;
  if (block) {
    block = (state.flowLevel < 0 || state.flowLevel > level);
  }
  var objectOrArray = type === '[object Object]' || type === '[object Array]',
      duplicateIndex,
      duplicate;
  if (objectOrArray) {
    duplicateIndex = state.duplicates.indexOf(object);
    duplicate = duplicateIndex !== -1;
  }
  if ((state.tag !== null && state.tag !== '?') || duplicate || (state.indent !== 2 && level > 0)) {
    compact = false;
  }
  if (duplicate && state.usedDuplicates[duplicateIndex]) {
    state.dump = '*ref_' + duplicateIndex;
  } else {
    if (objectOrArray && duplicate && !state.usedDuplicates[duplicateIndex]) {
      state.usedDuplicates[duplicateIndex] = true;
    }
    if (type === '[object Object]') {
      if (block && (Object.keys(state.dump).length !== 0)) {
        writeBlockMapping(state, level, state.dump, compact);
        if (duplicate) {
          state.dump = '&ref_' + duplicateIndex + state.dump;
        }
      } else {
        writeFlowMapping(state, level, state.dump);
        if (duplicate) {
          state.dump = '&ref_' + duplicateIndex + ' ' + state.dump;
        }
      }
    } else if (type === '[object Array]') {
      if (block && (state.dump.length !== 0)) {
        if (state.noArrayIndent && !isblockseq && level > 0) {
          writeBlockSequence(state, level - 1, state.dump, compact);
        } else {
          writeBlockSequence(state, level, state.dump, compact);
        }
        if (duplicate) {
          state.dump = '&ref_' + duplicateIndex + state.dump;
        }
      } else {
        writeFlowSequence(state, level, state.dump);
        if (duplicate) {
          state.dump = '&ref_' + duplicateIndex + ' ' + state.dump;
        }
      }
    } else if (type === '[object String]') {
      if (state.tag !== '?') {
        writeScalar(state, state.dump, level, iskey, inblock);
      }
    } else if (type === '[object Undefined]') {
      return false;
    } else {
      if (state.skipInvalid) return false;
      throw new exception('unacceptable kind of an object to dump ' + type);
    }
    if (state.tag !== null && state.tag !== '?') {
      tagStr = encodeURI(
        state.tag[0] === '!' ? state.tag.slice(1) : state.tag
      ).replace(/!/g, '%21');
      if (state.tag[0] === '!') {
        tagStr = '!' + tagStr;
      } else if (tagStr.slice(0, 18) === 'tag:yaml.org,2002:') {
        tagStr = '!!' + tagStr.slice(18);
      } else {
        tagStr = '!<' + tagStr + '>';
      }
      state.dump = tagStr + ' ' + state.dump;
    }
  }
  return true;
}
function getDuplicateReferences(object, state) {
  var objects = [],
      duplicatesIndexes = [],
      index,
      length;
  inspectNode(object, objects, duplicatesIndexes);
  for (index = 0, length = duplicatesIndexes.length; index < length; index += 1) {
    state.duplicates.push(objects[duplicatesIndexes[index]]);
  }
  state.usedDuplicates = new Array(length);
}
function inspectNode(object, objects, duplicatesIndexes) {
  var objectKeyList,
      index,
      length;
  if (object !== null && typeof object === 'object') {
    index = objects.indexOf(object);
    if (index !== -1) {
      if (duplicatesIndexes.indexOf(index) === -1) {
        duplicatesIndexes.push(index);
      }
    } else {
      objects.push(object);
      if (Array.isArray(object)) {
        for (index = 0, length = object.length; index < length; index += 1) {
          inspectNode(object[index], objects, duplicatesIndexes);
        }
      } else {
        objectKeyList = Object.keys(object);
        for (index = 0, length = objectKeyList.length; index < length; index += 1) {
          inspectNode(object[objectKeyList[index]], objects, duplicatesIndexes);
        }
      }
    }
  }
}
function dump$1(input, options) {
  options = options || {};
  var state = new State(options);
  if (!state.noRefs) getDuplicateReferences(input, state);
  var value = input;
  if (state.replacer) {
    value = state.replacer.call({ '': value }, '', value);
  }
  if (writeNode(state, 0, value, true, true)) return state.dump + '\n';
  return '';
}
var dump_1 = dump$1;
var dumper = {
	dump: dump_1
};
function renamed(from, to) {
  return function () {
    throw new Error('Function yaml.' + from + ' is removed in js-yaml 4. ' +
      'Use yaml.' + to + ' instead, which is now safe by default.');
  };
}
var Type                = type;
var Schema              = schema;
var FAILSAFE_SCHEMA     = failsafe;
var JSON_SCHEMA         = json;
var CORE_SCHEMA         = core;
var DEFAULT_SCHEMA      = _default;
var load                = loader.load;
var loadAll             = loader.loadAll;
var dump                = dumper.dump;
var YAMLException       = exception;
var types = {
  binary:    binary,
  float:     float,
  map:       map,
  null:      _null,
  pairs:     pairs,
  set:       set,
  timestamp: timestamp,
  bool:      bool,
  int:       int,
  merge:     merge,
  omap:      omap,
  seq:       seq,
  str:       str
};
var safeLoad            = renamed('safeLoad', 'load');
var safeLoadAll         = renamed('safeLoadAll', 'loadAll');
var safeDump            = renamed('safeDump', 'dump');
var jsYaml = {
	Type: Type,
	Schema: Schema,
	FAILSAFE_SCHEMA: FAILSAFE_SCHEMA,
	JSON_SCHEMA: JSON_SCHEMA,
	CORE_SCHEMA: CORE_SCHEMA,
	DEFAULT_SCHEMA: DEFAULT_SCHEMA,
	load: load,
	loadAll: loadAll,
	dump: dump,
	YAMLException: YAMLException,
	types: types,
	safeLoad: safeLoad,
	safeLoadAll: safeLoadAll,
	safeDump: safeDump
};

const debug$1 = (
  typeof process === 'object' &&
  process.env &&
  process.env.NODE_DEBUG &&
  /\bsemver\b/i.test(process.env.NODE_DEBUG)
) ? (...args) => console.error('SEMVER', ...args)
  : () => {};
var debug_1 = debug$1;
getDefaultExportFromCjs(debug_1);

const SEMVER_SPEC_VERSION = '2.0.0';
const MAX_LENGTH$1 = 256;
const MAX_SAFE_INTEGER$1 = Number.MAX_SAFE_INTEGER ||
 9007199254740991;
const MAX_SAFE_COMPONENT_LENGTH = 16;
const MAX_SAFE_BUILD_LENGTH = MAX_LENGTH$1 - 6;
const RELEASE_TYPES = [
  'major',
  'premajor',
  'minor',
  'preminor',
  'patch',
  'prepatch',
  'prerelease',
];
var constants = {
  MAX_LENGTH: MAX_LENGTH$1,
  MAX_SAFE_COMPONENT_LENGTH,
  MAX_SAFE_BUILD_LENGTH,
  MAX_SAFE_INTEGER: MAX_SAFE_INTEGER$1,
  RELEASE_TYPES,
  SEMVER_SPEC_VERSION,
  FLAG_INCLUDE_PRERELEASE: 0b001,
  FLAG_LOOSE: 0b010,
};
getDefaultExportFromCjs(constants);

var re$1 = {exports: {}};

(function (module, exports) {
	const {
	  MAX_SAFE_COMPONENT_LENGTH,
	  MAX_SAFE_BUILD_LENGTH,
	  MAX_LENGTH,
	} = constants;
	const debug = debug_1;
	exports = module.exports = {};
	const re = exports.re = [];
	const safeRe = exports.safeRe = [];
	const src = exports.src = [];
	const t = exports.t = {};
	let R = 0;
	const LETTERDASHNUMBER = '[a-zA-Z0-9-]';
	const safeRegexReplacements = [
	  ['\\s', 1],
	  ['\\d', MAX_LENGTH],
	  [LETTERDASHNUMBER, MAX_SAFE_BUILD_LENGTH],
	];
	const makeSafeRegex = (value) => {
	  for (const [token, max] of safeRegexReplacements) {
	    value = value
	      .split(`${token}*`).join(`${token}{0,${max}}`)
	      .split(`${token}+`).join(`${token}{1,${max}}`);
	  }
	  return value
	};
	const createToken = (name, value, isGlobal) => {
	  const safe = makeSafeRegex(value);
	  const index = R++;
	  debug(name, index, value);
	  t[name] = index;
	  src[index] = value;
	  re[index] = new RegExp(value, isGlobal ? 'g' : undefined);
	  safeRe[index] = new RegExp(safe, isGlobal ? 'g' : undefined);
	};
	createToken('NUMERICIDENTIFIER', '0|[1-9]\\d*');
	createToken('NUMERICIDENTIFIERLOOSE', '\\d+');
	createToken('NONNUMERICIDENTIFIER', `\\d*[a-zA-Z-]${LETTERDASHNUMBER}*`);
	createToken('MAINVERSION', `(${src[t.NUMERICIDENTIFIER]})\\.` +
	                   `(${src[t.NUMERICIDENTIFIER]})\\.` +
	                   `(${src[t.NUMERICIDENTIFIER]})`);
	createToken('MAINVERSIONLOOSE', `(${src[t.NUMERICIDENTIFIERLOOSE]})\\.` +
	                        `(${src[t.NUMERICIDENTIFIERLOOSE]})\\.` +
	                        `(${src[t.NUMERICIDENTIFIERLOOSE]})`);
	createToken('PRERELEASEIDENTIFIER', `(?:${src[t.NUMERICIDENTIFIER]
	}|${src[t.NONNUMERICIDENTIFIER]})`);
	createToken('PRERELEASEIDENTIFIERLOOSE', `(?:${src[t.NUMERICIDENTIFIERLOOSE]
	}|${src[t.NONNUMERICIDENTIFIER]})`);
	createToken('PRERELEASE', `(?:-(${src[t.PRERELEASEIDENTIFIER]
	}(?:\\.${src[t.PRERELEASEIDENTIFIER]})*))`);
	createToken('PRERELEASELOOSE', `(?:-?(${src[t.PRERELEASEIDENTIFIERLOOSE]
	}(?:\\.${src[t.PRERELEASEIDENTIFIERLOOSE]})*))`);
	createToken('BUILDIDENTIFIER', `${LETTERDASHNUMBER}+`);
	createToken('BUILD', `(?:\\+(${src[t.BUILDIDENTIFIER]
	}(?:\\.${src[t.BUILDIDENTIFIER]})*))`);
	createToken('FULLPLAIN', `v?${src[t.MAINVERSION]
	}${src[t.PRERELEASE]}?${
	  src[t.BUILD]}?`);
	createToken('FULL', `^${src[t.FULLPLAIN]}$`);
	createToken('LOOSEPLAIN', `[v=\\s]*${src[t.MAINVERSIONLOOSE]
	}${src[t.PRERELEASELOOSE]}?${
	  src[t.BUILD]}?`);
	createToken('LOOSE', `^${src[t.LOOSEPLAIN]}$`);
	createToken('GTLT', '((?:<|>)?=?)');
	createToken('XRANGEIDENTIFIERLOOSE', `${src[t.NUMERICIDENTIFIERLOOSE]}|x|X|\\*`);
	createToken('XRANGEIDENTIFIER', `${src[t.NUMERICIDENTIFIER]}|x|X|\\*`);
	createToken('XRANGEPLAIN', `[v=\\s]*(${src[t.XRANGEIDENTIFIER]})` +
	                   `(?:\\.(${src[t.XRANGEIDENTIFIER]})` +
	                   `(?:\\.(${src[t.XRANGEIDENTIFIER]})` +
	                   `(?:${src[t.PRERELEASE]})?${
	                     src[t.BUILD]}?` +
	                   `)?)?`);
	createToken('XRANGEPLAINLOOSE', `[v=\\s]*(${src[t.XRANGEIDENTIFIERLOOSE]})` +
	                        `(?:\\.(${src[t.XRANGEIDENTIFIERLOOSE]})` +
	                        `(?:\\.(${src[t.XRANGEIDENTIFIERLOOSE]})` +
	                        `(?:${src[t.PRERELEASELOOSE]})?${
	                          src[t.BUILD]}?` +
	                        `)?)?`);
	createToken('XRANGE', `^${src[t.GTLT]}\\s*${src[t.XRANGEPLAIN]}$`);
	createToken('XRANGELOOSE', `^${src[t.GTLT]}\\s*${src[t.XRANGEPLAINLOOSE]}$`);
	createToken('COERCEPLAIN', `${'(^|[^\\d])' +
	              '(\\d{1,'}${MAX_SAFE_COMPONENT_LENGTH}})` +
	              `(?:\\.(\\d{1,${MAX_SAFE_COMPONENT_LENGTH}}))?` +
	              `(?:\\.(\\d{1,${MAX_SAFE_COMPONENT_LENGTH}}))?`);
	createToken('COERCE', `${src[t.COERCEPLAIN]}(?:$|[^\\d])`);
	createToken('COERCEFULL', src[t.COERCEPLAIN] +
	              `(?:${src[t.PRERELEASE]})?` +
	              `(?:${src[t.BUILD]})?` +
	              `(?:$|[^\\d])`);
	createToken('COERCERTL', src[t.COERCE], true);
	createToken('COERCERTLFULL', src[t.COERCEFULL], true);
	createToken('LONETILDE', '(?:~>?)');
	createToken('TILDETRIM', `(\\s*)${src[t.LONETILDE]}\\s+`, true);
	exports.tildeTrimReplace = '$1~';
	createToken('TILDE', `^${src[t.LONETILDE]}${src[t.XRANGEPLAIN]}$`);
	createToken('TILDELOOSE', `^${src[t.LONETILDE]}${src[t.XRANGEPLAINLOOSE]}$`);
	createToken('LONECARET', '(?:\\^)');
	createToken('CARETTRIM', `(\\s*)${src[t.LONECARET]}\\s+`, true);
	exports.caretTrimReplace = '$1^';
	createToken('CARET', `^${src[t.LONECARET]}${src[t.XRANGEPLAIN]}$`);
	createToken('CARETLOOSE', `^${src[t.LONECARET]}${src[t.XRANGEPLAINLOOSE]}$`);
	createToken('COMPARATORLOOSE', `^${src[t.GTLT]}\\s*(${src[t.LOOSEPLAIN]})$|^$`);
	createToken('COMPARATOR', `^${src[t.GTLT]}\\s*(${src[t.FULLPLAIN]})$|^$`);
	createToken('COMPARATORTRIM', `(\\s*)${src[t.GTLT]
	}\\s*(${src[t.LOOSEPLAIN]}|${src[t.XRANGEPLAIN]})`, true);
	exports.comparatorTrimReplace = '$1$2$3';
	createToken('HYPHENRANGE', `^\\s*(${src[t.XRANGEPLAIN]})` +
	                   `\\s+-\\s+` +
	                   `(${src[t.XRANGEPLAIN]})` +
	                   `\\s*$`);
	createToken('HYPHENRANGELOOSE', `^\\s*(${src[t.XRANGEPLAINLOOSE]})` +
	                        `\\s+-\\s+` +
	                        `(${src[t.XRANGEPLAINLOOSE]})` +
	                        `\\s*$`);
	createToken('STAR', '(<|>)?=?\\s*\\*');
	createToken('GTE0', '^\\s*>=\\s*0\\.0\\.0\\s*$');
	createToken('GTE0PRE', '^\\s*>=\\s*0\\.0\\.0-0\\s*$');
} (re$1, re$1.exports));
var reExports = re$1.exports;
getDefaultExportFromCjs(reExports);

const looseOption = Object.freeze({ loose: true });
const emptyOpts = Object.freeze({ });
const parseOptions$1 = options => {
  if (!options) {
    return emptyOpts
  }
  if (typeof options !== 'object') {
    return looseOption
  }
  return options
};
var parseOptions_1 = parseOptions$1;
getDefaultExportFromCjs(parseOptions_1);

const numeric = /^[0-9]+$/;
const compareIdentifiers$1 = (a, b) => {
  const anum = numeric.test(a);
  const bnum = numeric.test(b);
  if (anum && bnum) {
    a = +a;
    b = +b;
  }
  return a === b ? 0
    : (anum && !bnum) ? -1
    : (bnum && !anum) ? 1
    : a < b ? -1
    : 1
};
const rcompareIdentifiers = (a, b) => compareIdentifiers$1(b, a);
var identifiers = {
  compareIdentifiers: compareIdentifiers$1,
  rcompareIdentifiers,
};
getDefaultExportFromCjs(identifiers);

const debug = debug_1;
const { MAX_LENGTH, MAX_SAFE_INTEGER } = constants;
const { safeRe: re, t } = reExports;
const parseOptions = parseOptions_1;
const { compareIdentifiers } = identifiers;
let SemVer$2 = class SemVer {
  constructor (version, options) {
    options = parseOptions(options);
    if (version instanceof SemVer) {
      if (version.loose === !!options.loose &&
          version.includePrerelease === !!options.includePrerelease) {
        return version
      } else {
        version = version.version;
      }
    } else if (typeof version !== 'string') {
      throw new TypeError(`Invalid version. Must be a string. Got type "${typeof version}".`)
    }
    if (version.length > MAX_LENGTH) {
      throw new TypeError(
        `version is longer than ${MAX_LENGTH} characters`
      )
    }
    debug('SemVer', version, options);
    this.options = options;
    this.loose = !!options.loose;
    this.includePrerelease = !!options.includePrerelease;
    const m = version.trim().match(options.loose ? re[t.LOOSE] : re[t.FULL]);
    if (!m) {
      throw new TypeError(`Invalid Version: ${version}`)
    }
    this.raw = version;
    this.major = +m[1];
    this.minor = +m[2];
    this.patch = +m[3];
    if (this.major > MAX_SAFE_INTEGER || this.major < 0) {
      throw new TypeError('Invalid major version')
    }
    if (this.minor > MAX_SAFE_INTEGER || this.minor < 0) {
      throw new TypeError('Invalid minor version')
    }
    if (this.patch > MAX_SAFE_INTEGER || this.patch < 0) {
      throw new TypeError('Invalid patch version')
    }
    if (!m[4]) {
      this.prerelease = [];
    } else {
      this.prerelease = m[4].split('.').map((id) => {
        if (/^[0-9]+$/.test(id)) {
          const num = +id;
          if (num >= 0 && num < MAX_SAFE_INTEGER) {
            return num
          }
        }
        return id
      });
    }
    this.build = m[5] ? m[5].split('.') : [];
    this.format();
  }
  format () {
    this.version = `${this.major}.${this.minor}.${this.patch}`;
    if (this.prerelease.length) {
      this.version += `-${this.prerelease.join('.')}`;
    }
    return this.version
  }
  toString () {
    return this.version
  }
  compare (other) {
    debug('SemVer.compare', this.version, this.options, other);
    if (!(other instanceof SemVer)) {
      if (typeof other === 'string' && other === this.version) {
        return 0
      }
      other = new SemVer(other, this.options);
    }
    if (other.version === this.version) {
      return 0
    }
    return this.compareMain(other) || this.comparePre(other)
  }
  compareMain (other) {
    if (!(other instanceof SemVer)) {
      other = new SemVer(other, this.options);
    }
    return (
      compareIdentifiers(this.major, other.major) ||
      compareIdentifiers(this.minor, other.minor) ||
      compareIdentifiers(this.patch, other.patch)
    )
  }
  comparePre (other) {
    if (!(other instanceof SemVer)) {
      other = new SemVer(other, this.options);
    }
    if (this.prerelease.length && !other.prerelease.length) {
      return -1
    } else if (!this.prerelease.length && other.prerelease.length) {
      return 1
    } else if (!this.prerelease.length && !other.prerelease.length) {
      return 0
    }
    let i = 0;
    do {
      const a = this.prerelease[i];
      const b = other.prerelease[i];
      debug('prerelease compare', i, a, b);
      if (a === undefined && b === undefined) {
        return 0
      } else if (b === undefined) {
        return 1
      } else if (a === undefined) {
        return -1
      } else if (a === b) {
        continue
      } else {
        return compareIdentifiers(a, b)
      }
    } while (++i)
  }
  compareBuild (other) {
    if (!(other instanceof SemVer)) {
      other = new SemVer(other, this.options);
    }
    let i = 0;
    do {
      const a = this.build[i];
      const b = other.build[i];
      debug('build compare', i, a, b);
      if (a === undefined && b === undefined) {
        return 0
      } else if (b === undefined) {
        return 1
      } else if (a === undefined) {
        return -1
      } else if (a === b) {
        continue
      } else {
        return compareIdentifiers(a, b)
      }
    } while (++i)
  }
  inc (release, identifier, identifierBase) {
    switch (release) {
      case 'premajor':
        this.prerelease.length = 0;
        this.patch = 0;
        this.minor = 0;
        this.major++;
        this.inc('pre', identifier, identifierBase);
        break
      case 'preminor':
        this.prerelease.length = 0;
        this.patch = 0;
        this.minor++;
        this.inc('pre', identifier, identifierBase);
        break
      case 'prepatch':
        this.prerelease.length = 0;
        this.inc('patch', identifier, identifierBase);
        this.inc('pre', identifier, identifierBase);
        break
      case 'prerelease':
        if (this.prerelease.length === 0) {
          this.inc('patch', identifier, identifierBase);
        }
        this.inc('pre', identifier, identifierBase);
        break
      case 'major':
        if (
          this.minor !== 0 ||
          this.patch !== 0 ||
          this.prerelease.length === 0
        ) {
          this.major++;
        }
        this.minor = 0;
        this.patch = 0;
        this.prerelease = [];
        break
      case 'minor':
        if (this.patch !== 0 || this.prerelease.length === 0) {
          this.minor++;
        }
        this.patch = 0;
        this.prerelease = [];
        break
      case 'patch':
        if (this.prerelease.length === 0) {
          this.patch++;
        }
        this.prerelease = [];
        break
      case 'pre': {
        const base = Number(identifierBase) ? 1 : 0;
        if (!identifier && identifierBase === false) {
          throw new Error('invalid increment argument: identifier is empty')
        }
        if (this.prerelease.length === 0) {
          this.prerelease = [base];
        } else {
          let i = this.prerelease.length;
          while (--i >= 0) {
            if (typeof this.prerelease[i] === 'number') {
              this.prerelease[i]++;
              i = -2;
            }
          }
          if (i === -1) {
            if (identifier === this.prerelease.join('.') && identifierBase === false) {
              throw new Error('invalid increment argument: identifier already exists')
            }
            this.prerelease.push(base);
          }
        }
        if (identifier) {
          let prerelease = [identifier, base];
          if (identifierBase === false) {
            prerelease = [identifier];
          }
          if (compareIdentifiers(this.prerelease[0], identifier) === 0) {
            if (isNaN(this.prerelease[1])) {
              this.prerelease = prerelease;
            }
          } else {
            this.prerelease = prerelease;
          }
        }
        break
      }
      default:
        throw new Error(`invalid increment argument: ${release}`)
    }
    this.raw = this.format();
    if (this.build.length) {
      this.raw += `+${this.build.join('.')}`;
    }
    return this
  }
};
var semver = SemVer$2;
getDefaultExportFromCjs(semver);

const SemVer$1 = semver;
const parse = (version, options, throwErrors = false) => {
  if (version instanceof SemVer$1) {
    return version
  }
  try {
    return new SemVer$1(version, options)
  } catch (er) {
    if (!throwErrors) {
      return null
    }
    throw er
  }
};
var parse_1 = parse;
var semverParse = getDefaultExportFromCjs(parse_1);

const SemVer = semver;
const compare$1 = (a, b, loose) =>
  new SemVer(a, loose).compare(new SemVer(b, loose));
var compare_1 = compare$1;
getDefaultExportFromCjs(compare_1);

const compare = compare_1;
const lt = (a, b, loose) => compare(a, b, loose) < 0;
var lt_1 = lt;
var semverLt = getDefaultExportFromCjs(lt_1);

const allowedKeys = [
  "added",
  "napiVersion",
  "deprecated",
  "removed",
  "changes",
];
const changesExpectedKeys = ["version", "pr-url", "description"];
const VERSION_PLACEHOLDER = "REPLACEME";
const MAX_SAFE_SEMVER_VERSION = semverParse(
  Array.from({ length: 3 }, () => Number.MAX_SAFE_INTEGER).join("."),
);
const validVersionNumberRegex = /^v\d+\.\d+\.\d+$/;
const prUrlRegex = new RegExp("^https://github.com/nodejs/node/pull/\\d+$");
const privatePRUrl = "https://github.com/nodejs-private/node-private/pull/";
let releasedVersions;
let invalidVersionMessage = "version(s) must respect the pattern `vx.x.x` or";
if (process.env.NODE_RELEASED_VERSIONS) {
  console.log("Using release list from env...");
  releasedVersions = process.env.NODE_RELEASED_VERSIONS.split(",").map(
    (v) => `v${v}`,
  );
  invalidVersionMessage = `version not listed in the changelogs, `;
}
invalidVersionMessage += `use the placeholder \`${VERSION_PLACEHOLDER}\``;
const kContainsIllegalKey = Symbol("illegal key");
const kWrongKeyOrder = Symbol("Wrong key order");
function unorderedKeys(meta) {
  const keys = Object.keys(meta);
  let previousKeyIndex = -1;
  for (const key of keys) {
    const keyIndex = allowedKeys.indexOf(key);
    if (keyIndex <= previousKeyIndex) {
      return keyIndex === -1 ? kContainsIllegalKey : kWrongKeyOrder;
    }
    previousKeyIndex = keyIndex;
  }
}
function containsInvalidVersionNumber(version) {
  if (Array.isArray(version)) {
    return version.some(containsInvalidVersionNumber);
  }
  if (version === undefined || version === VERSION_PLACEHOLDER) return false;
  if (
    releasedVersions &&
    (version[1] !== "0" || (version[3] !== "0" && version[3] !== "1"))
  )
    return !releasedVersions.includes(version);
  return !validVersionNumberRegex.test(version);
}
const getValidSemver = (version) =>
  version === VERSION_PLACEHOLDER ? MAX_SAFE_SEMVER_VERSION : version;
function areVersionsUnordered(versions) {
  if (!Array.isArray(versions)) return false;
  for (let index = 1; index < versions.length; index++) {
    if (
      semverLt(
        getValidSemver(versions[index - 1]),
        getValidSemver(versions[index]),
      )
    ) {
      return true;
    }
  }
}
function invalidChangesKeys(change) {
  const keys = Object.keys(change);
  const { length } = keys;
  if (length !== changesExpectedKeys.length) return true;
  for (let index = 0; index < length; index++) {
    if (keys[index] !== changesExpectedKeys[index]) return true;
  }
}
function validateSecurityChange(file, node, change, index) {
  if ("commit" in change) {
    if (typeof change.commit !== "string" || isNaN(`0x${change.commit}`)) {
      file.message(
        `changes[${index}]: Ill-formed security change commit ID`,
        node,
      );
    }
    if (Object.keys(change)[1] === "commit") {
      change = { ...change };
      delete change.commit;
    }
  }
  if (invalidChangesKeys(change)) {
    const securityChangeExpectedKeys = [...changesExpectedKeys];
    securityChangeExpectedKeys[0] += "[, commit]";
    file.message(
      `changes[${index}]: Invalid keys. Expected keys are: ` +
        securityChangeExpectedKeys.join(", "),
      node,
    );
  }
}
function validateChanges(file, node, changes) {
  if (!Array.isArray(changes))
    return file.message("`changes` must be a YAML list", node);
  const changesVersions = [];
  for (let index = 0; index < changes.length; index++) {
    const change = changes[index];
    const isAncient =
      typeof change.version === "string" && change.version.startsWith("v0.");
    const isSecurityChange =
      !isAncient &&
      typeof change["pr-url"] === "string" &&
      change["pr-url"].startsWith(privatePRUrl);
    if (isSecurityChange) {
      validateSecurityChange(file, node, change, index);
    } else if (!isAncient && invalidChangesKeys(change)) {
      file.message(
        `changes[${index}]: Invalid keys. Expected keys are: ` +
          changesExpectedKeys.join(", "),
        node,
      );
    }
    if (containsInvalidVersionNumber(change.version)) {
      file.message(`changes[${index}]: ${invalidVersionMessage}`, node);
    } else if (areVersionsUnordered(change.version)) {
      file.message(`changes[${index}]: list of versions is not in order`, node);
    }
    if (!isAncient && !isSecurityChange && !prUrlRegex.test(change["pr-url"])) {
      file.message(
        `changes[${index}]: PR-URL does not match the expected pattern`,
        node,
      );
    }
    if (typeof change.description !== "string" || !change.description.length) {
      file.message(
        `changes[${index}]: must contain a non-empty description`,
        node,
      );
    } else if (!change.description.endsWith(".")) {
      file.message(
        `changes[${index}]: description must end with a period`,
        node,
      );
    }
    changesVersions.push(
      Array.isArray(change.version) ? change.version[0] : change.version,
    );
  }
  if (areVersionsUnordered(changesVersions)) {
    file.message("Items in `changes` list are not in order", node);
  }
}
function validateMeta(node, file, meta) {
  switch (unorderedKeys(meta)) {
    case kContainsIllegalKey:
      file.message(
        "YAML dictionary contains illegal keys. Accepted values are: " +
          allowedKeys.join(", "),
        node,
      );
      break;
    case kWrongKeyOrder:
      file.message(
        "YAML dictionary keys should be in this order: " +
          allowedKeys.join(", "),
        node,
      );
      break;
  }
  if (containsInvalidVersionNumber(meta.added)) {
    file.message(`Invalid \`added\` value: ${invalidVersionMessage}`, node);
  } else if (areVersionsUnordered(meta.added)) {
    file.message("Versions in `added` list are not in order", node);
  }
  if (containsInvalidVersionNumber(meta.deprecated)) {
    file.message(
      `Invalid \`deprecated\` value: ${invalidVersionMessage}`,
      node,
    );
  } else if (areVersionsUnordered(meta.deprecated)) {
    file.message("Versions in `deprecated` list are not in order", node);
  }
  if (containsInvalidVersionNumber(meta.removed)) {
    file.message(`Invalid \`removed\` value: ${invalidVersionMessage}`, node);
  } else if (areVersionsUnordered(meta.removed)) {
    file.message("Versions in `removed` list are not in order", node);
  }
  if ("changes" in meta) {
    validateChanges(file, node, meta.changes);
  }
}
function validateYAMLComments(tree, file) {
  visit(tree, "html", function visitor(node) {
    if (node.value.startsWith("<!--YAML\n"))
      file.message(
        "Expected `<!-- YAML`, found `<!--YAML`. Please add a space",
        node,
      );
    if (!node.value.startsWith("<!-- YAML\n")) return;
    try {
      const meta = jsYaml.load("#" + node.value.slice(0, -"-->".length));
      validateMeta(node, file, meta);
    } catch (e) {
      file.message(e, node);
    }
  });
}
const remarkLintNodejsYamlComments = lintRule$1(
  "remark-lint:nodejs-yaml-comments",
  validateYAMLComments,
);

function lintRule(meta, rule) {
  const id = meta ;
  const url = undefined ;
  const parts = id.split(':');
  const source = parts[1] ? parts[0] : undefined;
  const ruleId = parts[1];
  Object.defineProperty(plugin, 'name', {value: id});
  return plugin
  function plugin(config) {
    const [severity, options] = coerce(ruleId, config);
    if (!severity) return
    const fatal = severity === 2;
    return (tree, file, next) => {
      let index = file.messages.length - 1;
      wrap(rule, (error) => {
        const messages = file.messages;
        if (error && !messages.includes(error)) {
          try {
            file.fail(error);
          } catch {}
        }
        while (++index < messages.length) {
          Object.assign(messages[index], {ruleId, source, fatal, url});
        }
        next();
      })(tree, file, options);
    }
  }
}
function coerce(name, config) {
  if (!Array.isArray(config)) return [1, config]
  const [severity, ...options] = config;
  switch (severity) {
    case false:
    case 'off':
    case 0: {
      return [0, ...options]
    }
    case true:
    case 'on':
    case 'warn':
    case 1: {
      return [1, ...options]
    }
    case 'error':
    case 2: {
      return [2, ...options]
    }
    default: {
      if (typeof severity !== 'number') return [1, config]
      throw new Error(
        'Incorrect severity `' +
          severity +
          '` for `' +
          name +
          '`, ' +
          'expected 0, 1, or 2'
      )
    }
  }
}

const remarkLintProhibitedStrings = lintRule('remark-lint:prohibited-strings', prohibitedStrings);
function testProhibited (val, content) {
  let regexpFlags = 'g';
  let no = val.no;
  if (!no) {
    no = escapeStringRegexp(val.yes);
    regexpFlags += 'i';
  }
  let regexpString = '(?<!\\.|@[a-zA-Z0-9/-]*)';
  let ignoreNextTo;
  if (val.ignoreNextTo) {
    if (Array.isArray(val.ignoreNextTo)) {
      const parts = val.ignoreNextTo.map(a => escapeStringRegexp(a)).join('|');
      ignoreNextTo = `(?:${parts})`;
    } else {
      ignoreNextTo = escapeStringRegexp(val.ignoreNextTo);
    }
  } else {
    ignoreNextTo = '';
  }
  const replaceCaptureGroups = !!val.replaceCaptureGroups;
  if (/^\b/.test(no)) {
    regexpString += '\\b';
  }
  if (ignoreNextTo) {
    regexpString += `(?<!${ignoreNextTo})`;
  }
  regexpString += `(${no})`;
  if (ignoreNextTo) {
    regexpString += `(?!${ignoreNextTo})`;
  }
  if (/\b$/.test(no)) {
    regexpString += '\\b';
  }
  regexpString += '(?!\\.\\w)';
  const re = new RegExp(regexpString, regexpFlags);
  const results = [];
  let result = re.exec(content);
  while (result) {
    if (result[1] !== val.yes) {
      let yes = val.yes;
      if (replaceCaptureGroups) {
        yes = result[1].replace(new RegExp(no), yes);
      }
      results.push({ result: result[1], index: result.index, yes });
    }
    result = re.exec(content);
  }
  return results
}
function prohibitedStrings (ast, file, strings) {
  const myLocation = location(file);
  visit(ast, 'text', checkText);
  function checkText (node) {
    const content = node.value;
    const initial = pointStart(node).offset;
    strings.forEach((val) => {
      const results = testProhibited(val, content);
      if (results.length) {
        results.forEach(({ result, index, yes }) => {
          const message = val.yes ? `Use "${yes}" instead of "${result}"` : `Do not use "${result}"`;
          file.message(message, {
            start: myLocation.toPoint(initial + index),
            end: myLocation.toPoint(initial + index + [...result].length)
          });
        });
      }
    });
  }
}

/**
 * remark-lint rule to warn when thematic breaks (horizontal rules) are
 * inconsistent.
 *
 * ## What is this?
 *
 * This package checks markers and whitespace of thematic rules.
 *
 * ## When should I use this?
 *
 * You can use this package to check that thematic breaks are consistent.
 *
 * ## API
 *
 * ### `unified().use(remarkLintRuleStyle[, options])`
 *
 * Warn when thematic breaks (horizontal rules) are inconsistent.
 *
 * ###### Parameters
 *
 * * `options` ([`Options`][api-options], default: `'consistent'`)
 *   — preferred style or whether to detect the first style and warn for
 *   further differences
 *
 * ### `Options`
 *
 * Configuration (TypeScript type).
 *
 * * `'consistent'`
 *   — detect the first used style and warn when further rules differ
 * * `string` (example: `'** * **'`, `'___'`)
 *   — thematic break to prefer
 *
 * ###### Type
 *
 * ```ts
 * type Options = string | 'consistent'
 * ```
 *
 * ## Recommendation
 *
 * Rules consist of a `*`, `-`, or `_` character,
 * which occurs at least three times with nothing else except for arbitrary
 * spaces or tabs on a single line.
 * Using spaces, tabs, or more than three markers is unnecessary work to type
 * out.
 * As asterisks can be used as a marker for more markdown constructs,
 * it’s recommended to use that for rules (and lists, emphasis, strong) too.
 * So it’s recommended to pass `'***'`.
 *
 * ## Fix
 *
 * [`remark-stringify`][github-remark-stringify] formats rules with `***` by
 * default.
 * There are three settings to control rules:
 *
 * * `rule` (default: `'*'`) — marker
 * * `ruleRepetition` (default: `3`) — repetitions
 * * `ruleSpaces` (default: `false`) — use spaces between markers
 *
 * [api-options]: #options
 * [api-remark-lint-rule-style]: #unifieduseremarklintrulestyle-options
 * [github-remark-stringify]: https://github.com/remarkjs/remark/tree/main/packages/remark-stringify
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module rule-style
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"name": "ok.md"}
 *
 *   Two rules:
 *
 *   * * *
 *
 *   * * *
 *
 * @example
 *   {"config": "_______", "name": "ok.md"}
 *
 *   _______
 *
 *   _______
 *
 * @example
 *   {"label": "input", "name": "not-ok.md"}
 *
 *   ***
 *
 *   * * *
 * @example
 *   {"label": "output", "name": "not-ok.md"}
 *
 *   3:1-3:6: Unexpected thematic rule `* * *`, expected `***`
 *
 * @example
 *   {"config": "🌍", "label": "output", "name": "not-ok.md", "positionless": true}
 *
 *   1:1: Unexpected value `🌍` for `options`, expected thematic rule or `'consistent'`
 */
const remarkLintRuleStyle = lintRule$1(
  {
    origin: 'remark-lint:rule-style',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-rule-style#readme'
  },
  function (tree, file, options) {
    const value = String(file);
    let expected;
    let cause;
    if (options === null || options === undefined || options === 'consistent') ; else if (
      /[^-_* ]/.test(options) ||
      options.at(0) === ' ' ||
      options.at(-1) === ' ' ||
      options.replaceAll(' ', '').length < 3
    ) {
      file.fail(
        'Unexpected value `' +
          options +
          "` for `options`, expected thematic rule or `'consistent'`"
      );
    } else {
      expected = options;
    }
    visitParents(tree, function (node, parents) {
      if (phrasing(node)) {
        return SKIP
      }
      if (node.type !== 'thematicBreak') return
      const end = pointEnd(node);
      const start = pointStart(node);
      if (
        start &&
        end &&
        typeof start.offset === 'number' &&
        typeof end.offset === 'number'
      ) {
        const place = {start, end};
        const actual = value.slice(start.offset, end.offset);
        if (expected) {
          if (actual !== expected) {
            file.message(
              'Unexpected thematic rule `' +
                actual +
                '`, expected `' +
                expected +
                '`',
              {ancestors: [...parents, node], cause, place}
            );
          }
        } else {
          expected = actual;
          cause = new VFileMessage(
            'Thematic rule style `' +
              expected +
              "` first defined for `'consistent'` here",
            {
              ancestors: [...parents, node],
              place,
              ruleId: 'rule-style',
              source: 'remark-lint'
            }
          );
        }
      }
    });
  }
);

/**
 * remark-lint rule to warn when strong markers are inconsistent.
 *
 * ## What is this?
 *
 * This package checks the style of strong markers.
 *
 * ## When should I use this?
 *
 * You can use this package to check that strong is consistent.
 *
 * ## API
 *
 * ### `unified().use(remarkLintStrongMarker[, options])`
 *
 * Warn when strong markers are inconsistent.
 *
 * ###### Parameters
 *
 * * `options` ([`Options`][api-options], default: `'consistent'`)
 *   — preferred style or whether to detect the first style and warn for
 *   further differences
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ### `Marker`
 *
 * Marker (TypeScript type).
 *
 * ###### Type
 *
 * ```ts
 * type Marker = '*' | '_'
 * ```
 *
 * ### `Options`
 *
 * Configuration (TypeScript type).
 *
 * ###### Type
 *
 * ```ts
 * type Options = Marker | 'consistent'
 * ```
 *
 * ## Recommendation
 *
 * Whether asterisks or underscores are used affects how and whether strong
 * works.
 * Underscores are sometimes used to represent normal underscores inside words,
 * so there are extra rules in markdown to support that.
 * Asterisks are not used in natural language,
 * so they don’t need these rules,
 * and thus can form strong in more cases.
 * Asterisks can also be used as the marker of more constructs than underscores:
 * lists.
 * Due to having simpler parsing rules,
 * looking more like syntax,
 * and that they can be used for more constructs,
 * it’s recommended to prefer asterisks.
 *
 * ## Fix
 *
 * [`remark-stringify`][github-remark-stringify] formats strong with asterisks
 * by default.
 * Pass `strong: '_'` to always use underscores.
 *
 * [api-marker]: #marker
 * [api-options]: #options
 * [api-remark-lint-strong-marker]: #unifieduseremarklintstrongmarker-options
 * [github-remark-stringify]: https://github.com/remarkjs/remark/tree/main/packages/remark-stringify
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module strong-marker
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"config": "*", "name": "ok-asterisk.md"}
 *
 *   **Mercury**.
 *
 * @example
 *   {"config": "*", "label": "input", "name": "not-ok-asterisk.md"}
 *
 *   __Mercury__.
 *
 * @example
 *   {"config": "*", "label": "output", "name": "not-ok-asterisk.md"}
 *
 *   1:1-1:12: Unexpected strong marker `_`, expected `*`
 *
 * @example
 *   {"config": "_", "name": "ok-underscore.md"}
 *
 *   __Mercury__.
 *
 * @example
 *   {"config": "_", "label": "input", "name": "not-ok-underscore.md"}
 *
 *   **Mercury**.
 *
 * @example
 *   {"config": "_", "label": "output", "name": "not-ok-underscore.md"}
 *
 *   1:1-1:12: Unexpected strong marker `*`, expected `_`
 *
 * @example
 *   {"label": "input", "name": "not-ok-consistent.md"}
 *
 *   **Mercury** and __Venus__.
 *
 * @example
 *   {"label": "output", "name": "not-ok-consistent.md"}
 *
 *   1:17-1:26: Unexpected strong marker `_`, expected `*`
 *
 * @example
 *   {"config": "🌍", "label": "output", "name": "not-ok.md", "positionless": true}
 *
 *   1:1: Unexpected value `🌍` for `options`, expected `'*'`, `'_'`, or `'consistent'`
 */
const remarkLintStrongMarker = lintRule$1(
  {
    origin: 'remark-lint:strong-marker',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-strong-marker#readme'
  },
  function (tree, file, options) {
    const value = String(file);
    let cause;
    let expected;
    if (options === null || options === undefined || options === 'consistent') ; else if (options === '*' || options === '_') {
      expected = options;
    } else {
      file.fail(
        'Unexpected value `' +
          options +
          "` for `options`, expected `'*'`, `'_'`, or `'consistent'`"
      );
    }
    visitParents(tree, 'strong', function (node, parents) {
      const start = pointStart(node);
      if (start && typeof start.offset === 'number') {
        const actual = value.charAt(start.offset);
        if (actual !== '*' && actual !== '_') return
        if (expected) {
          if (actual !== expected) {
            file.message(
              'Unexpected strong marker `' +
                actual +
                '`, expected `' +
                expected +
                '`',
              {ancestors: [...parents, node], cause, place: node.position}
            );
          }
        } else {
          expected = actual;
          cause = new VFileMessage(
            "Strong marker style `'" +
              actual +
              "'` first defined for `'consistent'` here",
            {
              ancestors: [...parents, node],
              place: node.position,
              ruleId: 'strong-marker',
              source: 'remark-lint'
            }
          );
        }
      }
    });
  }
);

/**
 * remark-lint rule to warn when GFM table cells are padded inconsistently.
 *
 * ## What is this?
 *
 * This package checks table cell padding.
 * Tables are a GFM feature enabled with [`remark-gfm`][github-remark-gfm].
 *
 * ## When should I use this?
 *
 * You can use this package to check that tables are consistent.
 *
 * ## API
 *
 * ### `unified().use(remarkLintTableCellPadding[, options])`
 *
 * Warn when GFM table cells are padded inconsistently.
 *
 * ###### Parameters
 *
 * * `options` ([`Options`][api-options], optional)
 *   — preferred style or whether to detect the first style and warn for
 *   further differences
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ### `Style`
 *
 * Style (TypeScript type).
 *
 * * `'compact'`
 *   — prefer zero spaces between pipes and content
 * * `'padded'`
 *   — prefer at least one space between pipes and content
 *
 * ###### Type
 *
 * ```ts
 * type Style = 'compact' | 'padded'
 * ```
 *
 * ### `Options`
 *
 * Configuration (TypeScript type).
 *
 * ###### Type
 *
 * ```ts
 * type Options = Style | 'consistent'
 * ```
 *
 * ## Recommendation
 *
 * It’s recommended to use at least one space between pipes and content for
 * legibility of the markup (`'padded'`).
 *
 * ## Fix
 *
 * [`remark-stringify`][github-remark-stringify] with
 * [`remark-gfm`][github-remark-gfm] formats all table cells as padded by
 * default.
 * Pass `tableCellPadding: false` to use a more compact style.
 *
 * [api-options]: #options
 * [api-style]: #style
 * [api-remark-lint-table-cell-padding]: #unifieduseremarklinttablecellpadding-options
 * [github-remark-gfm]: https://github.com/remarkjs/remark-gfm
 * [github-remark-stringify]: https://github.com/remarkjs/remark/tree/main/packages/remark-stringify
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module table-cell-padding
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"config": "padded", "gfm": true, "name": "ok.md"}
 *
 *   | Planet  | Symbol | Satellites | Mean anomaly (°) |
 *   | ------- | :----- | :--------: | ---------------: |
 *   | Mercury | ☿      |    None    |          174 796 |
 *
 *   | Planet | Symbol | Satellites | Mean anomaly (°) |
 *   | - | :- | :-: | -: |
 *   | Venus | ♀ | None | 50 115 |
 *
 * @example
 *   {"config": "padded", "gfm": true, "label": "input", "name": "not-ok.md"}
 *
 *   | Planet |
 *   | -------|
 *   | Mercury|
 *
 *   |Planet |
 *   |------ |
 *   |Venus  |
 *
 *   |  Planet  |
 *   |  ------  |
 *   |  Venus   |
 * @example
 *   {"config": "padded", "gfm": true, "label": "output", "name": "not-ok.md"}
 *
 *   2:10: Unexpected `0` spaces between cell content and edge, expected `1` space, add `1` space
 *   3:10: Unexpected `0` spaces between cell content and edge, expected `1` space, add `1` space
 *   5:2: Unexpected `0` spaces between cell edge and content, expected `1` space, add `1` space
 *   6:2: Unexpected `0` spaces between cell edge and content, expected `1` space, add `1` space
 *   7:2: Unexpected `0` spaces between cell edge and content, expected `1` space, add `1` space
 *   9:4: Unexpected `2` spaces between cell edge and content, expected `1` space, remove `1` space
 *   9:12: Unexpected `2` spaces between cell content and edge, expected `1` space, remove `1` space
 *   10:4: Unexpected `2` spaces between cell edge and content, expected `1` space, remove `1` space
 *   10:12: Unexpected `2` spaces between cell content and edge, expected `1` space, remove `1` space
 *   11:4: Unexpected `2` spaces between cell edge and content, expected `1` space, remove `1` space
 *   11:12: Unexpected `3` spaces between cell content and edge, expected between `1` (unaligned) and `2` (aligned) spaces, remove between `1` and `2` spaces
 *
 * @example
 *   {"config": "compact", "gfm": true, "name": "ok.md"}
 *
 *   |Planet |Symbol|Satellites|Mean anomaly (°)|
 *   |-------|:-----|:--------:|---------------:|
 *   |Mercury|☿     |   None   |         174 796|
 *
 *   |Planet|Symbol|Satellites|Mean anomaly (°)|
 *   |-|:-|:-:|-:|
 *   |Venus|♀|None|50 115|
 *
 * @example
 *   {"config": "compact", "gfm": true, "label": "input", "name": "not-ok.md"}
 *
 *   | Planet |
 *   | -------|
 *   | Mercury|
 *
 *   |Planet |
 *   |------ |
 *   |Venus  |
 *
 *   |  Planet  |
 *   |  ------  |
 *   |  Venus   |
 * @example
 *   {"config": "compact", "gfm": true, "label": "output", "name": "not-ok.md"}
 *
 *   1:3: Unexpected `1` space between cell edge and content, expected `0` spaces, remove `1` space
 *   2:3: Unexpected `1` space between cell edge and content, expected `0` spaces, remove `1` space
 *   3:3: Unexpected `1` space between cell edge and content, expected `0` spaces, remove `1` space
 *   5:9: Unexpected `1` space between cell content and edge, expected `0` spaces, remove `1` space
 *   6:9: Unexpected `1` space between cell content and edge, expected `0` spaces, remove `1` space
 *   7:9: Unexpected `2` spaces between cell content and edge, expected between `0` (unaligned) and `1` (aligned) space, remove between `1` and `2` spaces
 *   9:4: Unexpected `2` spaces between cell edge and content, expected `0` spaces, remove `2` spaces
 *   9:12: Unexpected `2` spaces between cell content and edge, expected `0` spaces, remove `2` spaces
 *   10:4: Unexpected `2` spaces between cell edge and content, expected `0` spaces, remove `2` spaces
 *   10:12: Unexpected `2` spaces between cell content and edge, expected `0` spaces, remove `2` spaces
 *   11:4: Unexpected `2` spaces between cell edge and content, expected `0` spaces, remove `2` spaces
 *   11:12: Unexpected `3` spaces between cell content and edge, expected between `0` (unaligned) and `1` (aligned) space, remove between `2` and `3` spaces
 *
 * @example
 *   {"gfm": true, "name": "consistent-padded-ok.md"}
 *
 *   | Planet |
 *   | - |
 *
 * @example
 *   {"gfm": true, "label": "input", "name": "consistent-padded-nok.md"}
 *
 *   | Planet|
 *   | - |
 * @example
 *   {"gfm": true, "label": "output", "name": "consistent-padded-nok.md"}
 *
 *   1:9: Unexpected `0` spaces between cell content and edge, expected `1` space, add `1` space
 *
 * @example
 *   {"gfm": true, "name": "consistent-compact-ok.md"}
 *
 *   |Planet|
 *   |-|
 *
 * @example
 *   {"gfm": true, "label": "input", "name": "consistent-compact-nok.md"}
 *
 *   |Planet |
 *   |-|
 * @example
 *   {"gfm": true, "label": "output", "name": "consistent-compact-nok.md"}
 *
 *   1:9: Unexpected `1` space between cell content and edge, expected `0` spaces, remove `1` space
 *
 * @example
 *   {"gfm": true, "name": "empty.md"}
 *
 *   | | Satellites |
 *   | - | - |
 *   | Mercury | |
 *
 * @example
 *   {"gfm": true, "name": "missing-cells.md"}
 *
 *   | Planet | Symbol | Satellites |
 *   | - | - | - |
 *   | Mercury |
 *   | Venus | ♀ |
 *   | Earth | 🜨 and ♁ | 1 |
 *   | Mars | ♂ | 2 | 19 412 |
 *
 * @example
 *   {"config": "padded", "gfm": true, "label": "input", "name": "missing-fences.md"}
 *
 *   ␠Planet|Symbol|Satellites
 *   ------:|:-----|----------
 *   Mercury|☿     |0
 *
 *   Planet|Symbol
 *   -----:|------
 *   ␠Venus|♀
 * @example
 *   {"config": "padded", "gfm": true, "label": "output", "name": "missing-fences.md"}
 *
 *   1:8: Unexpected `0` spaces between cell content and edge, expected `1` space, add `1` space
 *   1:9: Unexpected `0` spaces between cell edge and content, expected `1` space, add `1` space
 *   1:15: Unexpected `0` spaces between cell content and edge, expected `1` space, add `1` space
 *   1:16: Unexpected `0` spaces between cell edge and content, expected `1` space, add `1` space
 *   2:8: Unexpected `0` spaces between cell content and edge, expected `1` space, add `1` space
 *   2:9: Unexpected `0` spaces between cell edge and content, expected `1` space, add `1` space
 *   2:15: Unexpected `0` spaces between cell content and edge, expected `1` space, add `1` space
 *   2:16: Unexpected `0` spaces between cell edge and content, expected `1` space, add `1` space
 *   3:8: Unexpected `0` spaces between cell content and edge, expected `1` space, add `1` space
 *   3:9: Unexpected `0` spaces between cell edge and content, expected `1` space, add `1` space
 *   3:16: Unexpected `0` spaces between cell edge and content, expected `1` space, add `1` space
 *   5:7: Unexpected `0` spaces between cell content and edge, expected `1` space, add `1` space
 *   5:8: Unexpected `0` spaces between cell edge and content, expected `1` space, add `1` space
 *   6:7: Unexpected `0` spaces between cell content and edge, expected `1` space, add `1` space
 *   6:8: Unexpected `0` spaces between cell edge and content, expected `1` space, add `1` space
 *   7:7: Unexpected `0` spaces between cell content and edge, expected `1` space, add `1` space
 *   7:8: Unexpected `0` spaces between cell edge and content, expected `1` space, add `1` space
 *
 * @example
 *   {"config": "compact", "gfm": true, "label": "input", "name": "missing-fences.md"}
 *
 *   Planet | Symbol | Satellites
 *   -: | - | -
 *   Mercury | ☿ | 0
 *
 *   Planet | Symbol
 *   -----: | ------
 *   ␠Venus | ♀
 * @example
 *   {"config": "compact", "gfm": true, "label": "output", "name": "missing-fences.md"}
 *
 *   1:8: Unexpected `1` space between cell content and edge, expected `0` spaces, remove `1` space
 *   1:10: Unexpected `1` space between cell edge and content, expected `0` spaces, remove `1` space
 *   1:17: Unexpected `1` space between cell content and edge, expected `0` spaces, remove `1` space
 *   1:19: Unexpected `1` space between cell edge and content, expected `0` spaces, remove `1` space
 *   2:4: Unexpected `1` space between cell content and edge, expected `0` spaces, remove `1` space
 *   2:6: Unexpected `1` space between cell edge and content, expected `0` spaces, remove `1` space
 *   2:10: Unexpected `1` space between cell edge and content, expected `0` spaces, remove `1` space
 *   3:9: Unexpected `1` space between cell content and edge, expected `0` spaces, remove `1` space
 *   3:11: Unexpected `1` space between cell edge and content, expected `0` spaces, remove `1` space
 *   3:15: Unexpected `1` space between cell edge and content, expected `0` spaces, remove `1` space
 *   5:8: Unexpected `1` space between cell content and edge, expected `0` spaces, remove `1` space
 *   5:10: Unexpected `1` space between cell edge and content, expected `0` spaces, remove `1` space
 *   6:8: Unexpected `1` space between cell content and edge, expected `0` spaces, remove `1` space
 *   6:10: Unexpected `1` space between cell edge and content, expected `0` spaces, remove `1` space
 *   7:8: Unexpected `1` space between cell content and edge, expected `0` spaces, remove `1` space
 *   7:10: Unexpected `1` space between cell edge and content, expected `0` spaces, remove `1` space
 *
 * @example
 *   {"config": "compact", "gfm": true, "label": "input", "name": "trailing-spaces.md"}
 *
 *   Planet | Symbol␠
 *   -: | -␠
 *   Mercury | ☿␠␠
 *
 *   | Planet | Symbol |␠
 *   | ------ | ------ |␠
 *   | Venus  | ♀      |␠␠
 * @example
 *   {"config": "compact", "gfm": true, "label": "output", "name": "trailing-spaces.md"}
 *
 *   1:8: Unexpected `1` space between cell content and edge, expected `0` spaces, remove `1` space
 *   1:10: Unexpected `1` space between cell edge and content, expected `0` spaces, remove `1` space
 *   2:4: Unexpected `1` space between cell content and edge, expected `0` spaces, remove `1` space
 *   2:6: Unexpected `1` space between cell edge and content, expected `0` spaces, remove `1` space
 *   3:9: Unexpected `1` space between cell content and edge, expected `0` spaces, remove `1` space
 *   3:11: Unexpected `1` space between cell edge and content, expected `0` spaces, remove `1` space
 *   5:3: Unexpected `1` space between cell edge and content, expected `0` spaces, remove `1` space
 *   5:10: Unexpected `1` space between cell content and edge, expected `0` spaces, remove `1` space
 *   5:12: Unexpected `1` space between cell edge and content, expected `0` spaces, remove `1` space
 *   5:19: Unexpected `1` space between cell content and edge, expected `0` spaces, remove `1` space
 *   6:3: Unexpected `1` space between cell edge and content, expected `0` spaces, remove `1` space
 *   6:10: Unexpected `1` space between cell content and edge, expected `0` spaces, remove `1` space
 *   6:12: Unexpected `1` space between cell edge and content, expected `0` spaces, remove `1` space
 *   6:19: Unexpected `1` space between cell content and edge, expected `0` spaces, remove `1` space
 *   7:3: Unexpected `1` space between cell edge and content, expected `0` spaces, remove `1` space
 *   7:10: Unexpected `2` spaces between cell content and edge, expected between `0` (unaligned) and `1` (aligned) space, remove between `1` and `2` spaces
 *   7:12: Unexpected `1` space between cell edge and content, expected `0` spaces, remove `1` space
 *   7:19: Unexpected `6` spaces between cell content and edge, expected between `0` (unaligned) and `5` (aligned) spaces, remove between `1` and `6` spaces
 *
 * @example
 *   {"config": "compact", "gfm": true, "label": "input", "name": "nothing.md"}
 *
 *   |   |   |   |
 *   | - | - | - |
 *   |   |   |   |
 * @example
 *   {"config": "compact", "gfm": true, "label": "output", "name": "nothing.md"}
 *
 *   1:5: Unexpected `3` spaces between cell edge and content, expected between `0` (unaligned) and `1` (aligned) space, remove between `2` and `3` spaces
 *   1:9: Unexpected `3` spaces between cell edge and content, expected between `0` (unaligned) and `1` (aligned) space, remove between `2` and `3` spaces
 *   1:13: Unexpected `3` spaces between cell edge and content, expected between `0` (unaligned) and `1` (aligned) space, remove between `2` and `3` spaces
 *   2:3: Unexpected `1` space between cell edge and content, expected `0` spaces, remove `1` space
 *   2:5: Unexpected `1` space between cell content and edge, expected `0` spaces, remove `1` space
 *   2:7: Unexpected `1` space between cell edge and content, expected `0` spaces, remove `1` space
 *   2:9: Unexpected `1` space between cell content and edge, expected `0` spaces, remove `1` space
 *   2:11: Unexpected `1` space between cell edge and content, expected `0` spaces, remove `1` space
 *   2:13: Unexpected `1` space between cell content and edge, expected `0` spaces, remove `1` space
 *   3:5: Unexpected `3` spaces between cell edge and content, expected between `0` (unaligned) and `1` (aligned) space, remove between `2` and `3` spaces
 *   3:9: Unexpected `3` spaces between cell edge and content, expected between `0` (unaligned) and `1` (aligned) space, remove between `2` and `3` spaces
 *   3:13: Unexpected `3` spaces between cell edge and content, expected between `0` (unaligned) and `1` (aligned) space, remove between `2` and `3` spaces
 *
 * @example
 *   {"config": "padded", "gfm": true, "label": "input", "name": "nothing.md"}
 *
 *   ||||
 *   |-|-|-|
 *   ||||
 * @example
 *   {"config": "padded", "gfm": true, "label": "output", "name": "nothing.md"}
 *
 *   1:2: Unexpected `0` spaces between cell edge and content, expected between `1` (unaligned) and `3` (aligned) spaces, add between `3` and `1` space
 *   1:3: Unexpected `0` spaces between cell edge and content, expected between `1` (unaligned) and `3` (aligned) spaces, add between `3` and `1` space
 *   1:4: Unexpected `0` spaces between cell edge and content, expected between `1` (unaligned) and `3` (aligned) spaces, add between `3` and `1` space
 *   2:2: Unexpected `0` spaces between cell edge and content, expected `1` space, add `1` space
 *   2:3: Unexpected `0` spaces between cell content and edge, expected `1` space, add `1` space
 *   2:4: Unexpected `0` spaces between cell edge and content, expected `1` space, add `1` space
 *   2:5: Unexpected `0` spaces between cell content and edge, expected `1` space, add `1` space
 *   2:6: Unexpected `0` spaces between cell edge and content, expected `1` space, add `1` space
 *   2:7: Unexpected `0` spaces between cell content and edge, expected `1` space, add `1` space
 *   3:2: Unexpected `0` spaces between cell edge and content, expected between `1` (unaligned) and `3` (aligned) spaces, add between `3` and `1` space
 *   3:3: Unexpected `0` spaces between cell edge and content, expected between `1` (unaligned) and `3` (aligned) spaces, add between `3` and `1` space
 *   3:4: Unexpected `0` spaces between cell edge and content, expected between `1` (unaligned) and `3` (aligned) spaces, add between `3` and `1` space
 *
 * @example
 *   {"config": "padded", "gfm": true, "label": "input", "name": "more-weirdness.md"}
 *
 *   Mercury
 *   |-
 *
 *   Venus
 *   -|
 * @example
 *   {"config": "padded", "gfm": true, "label": "output", "name": "more-weirdness.md"}
 *
 *   2:2: Unexpected `0` spaces between cell edge and content, expected `1` space, add `1` space
 *   5:2: Unexpected `0` spaces between cell content and edge, expected between `1` (unaligned) and `5` (aligned) spaces, add between `5` and `1` space
 *
 * @example
 *   {"config": "padded", "gfm": true, "label": "input", "name": "containers.md"}
 *
 *   > | Mercury|
 *   > | - |
 *
 *   * | Venus|
 *     | - |
 *
 *   > * > | Earth|
 *   >   > | - |
 * @example
 *   {"config": "padded", "gfm": true, "label": "output", "name": "containers.md"}
 *
 *   1:12: Unexpected `0` spaces between cell content and edge, expected `1` space, add `1` space
 *   4:10: Unexpected `0` spaces between cell content and edge, expected `1` space, add `1` space
 *   7:14: Unexpected `0` spaces between cell content and edge, expected `1` space, add `1` space
 *
 * @example
 *   {"config": "padded", "gfm": true, "label": "input", "name": "windows.md"}
 *
 *   | Mercury|␍␊| --- |␍␊| None |
 * @example
 *   {"config": "padded", "gfm": true, "label": "output", "name": "windows.md"}
 *
 *   1:10: Unexpected `0` spaces between cell content and edge, expected `1` space, add `1` space
 *
 * @example
 *   {"config": "🌍", "gfm": true, "label": "output", "name": "not-ok.md", "positionless": true}
 *
 *   1:1: Unexpected value `🌍` for `options`, expected `'compact'`, `'padded'`, or `'consistent'`
 */
const remarkLintTableCellPadding = lintRule$1(
  {
    origin: 'remark-lint:table-cell-padding',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-table-cell-padding#readme'
  },
  function (tree, file, options) {
    const value = String(file);
    let expected;
    let cause;
    if (options === null || options === undefined || options === 'consistent') ; else if (options === 'compact' || options === 'padded') {
      expected = options;
    } else {
      file.fail(
        'Unexpected value `' +
          options +
          "` for `options`, expected `'compact'`, `'padded'`, or `'consistent'`"
      );
    }
    visitParents(tree, function (table, parents) {
      if (phrasing(table)) {
        return SKIP
      }
      if (table.type !== 'table') return
      const entries = inferTable([...parents, table]);
      const sizes = [];
      for (const entry of entries) {
        if (
          entry.size &&
          (sizes[entry.column] === undefined ||
            entry.size.middle > sizes[entry.column])
        ) {
          sizes[entry.column] = entry.size.middle;
        }
      }
      if (!expected) {
        for (const info of entries) {
          if (
            info.size &&
            info.size.middle &&
            info.size.middle === sizes[info.column]
          ) {
            const node = info.ancestors.at(-1);
            expected = info.size.left ? 'padded' : 'compact';
            cause = new VFileMessage(
              "Cell padding style `'" +
                expected +
                "'` first defined for `'consistent'` here",
              {
                ancestors: info.ancestors,
                place: node.position,
                ruleId: 'table-cell-padding',
                source: 'remark-lint'
              }
            );
          }
        }
      }
      if (!expected) return
      for (const info of entries) {
        checkSide('left', info, sizes);
        checkSide('right', info, sizes);
      }
      return SKIP
    });
    function checkSide(side, info, sizes) {
      if (!info.size) {
        return
      }
      const actual = info.size[side];
      if (actual === undefined) {
        return
      }
      const alignSpaces = sizes[info.column] - info.size.middle;
      const min = expected === 'compact' ? 0 : 1;
      let max = min;
      if (info.align === 'center') {
        max += Math.ceil(alignSpaces / 2);
      } else if (info.align === 'right' ? side === 'left' : side === 'right') {
        max += alignSpaces;
      }
      if (info.size.middle === 0) {
        if (side === 'right') return
        max = Math.max(max, sizes[info.column] + 2 * min);
      }
      if (actual < min || actual > max) {
        const differenceMin = min - actual;
        const differenceMinAbsolute = Math.abs(differenceMin);
        const differenceMax = max - actual;
        const differenceMaxAbsolute = Math.abs(differenceMax);
        file.message(
          'Unexpected `' +
            actual +
            '` ' +
            pluralize('space', actual) +
            ' between cell ' +
            (side === 'left' ? 'edge' : 'content') +
            ' and ' +
            (side === 'left' ? 'content' : 'edge') +
            ', expected ' +
            (min === max ? '' : 'between `' + min + '` (unaligned) and ') +
            '`' +
            max +
            '` ' +
            (min === max ? '' : '(aligned) ') +
            pluralize('space', max) +
            ', ' +
            (differenceMin < 0 ? 'remove' : 'add') +
            (differenceMin === differenceMax
              ? ''
              : ' between `' + differenceMaxAbsolute + '` and') +
            ' `' +
            differenceMinAbsolute +
            '` ' +
            pluralize('space', differenceMinAbsolute),
          {
            ancestors: info.ancestors,
            cause,
            place: side === 'left' ? info.size.leftPoint : info.size.rightPoint
          }
        );
      }
    }
    function inferTable(ancestors) {
      const node = ancestors.at(-1);
      ok$1(node.type === 'table');
      const align = node.align || [];
      const result = [];
      let rowIndex = -1;
      while (++rowIndex < node.children.length) {
        const row = node.children[rowIndex];
        let column = -1;
        while (++column < row.children.length) {
          const node = row.children[column];
          result.push({
            align: align[column],
            ancestors: [...ancestors, row, node],
            column,
            size: inferSize(
              pointStart(node),
              pointEnd(node),
              column === row.children.length - 1
            )
          });
        }
        if (rowIndex === 0) {
          const alignRow = inferAlignRow(ancestors, align);
          if (alignRow) result.push(...alignRow);
        }
      }
      return result
    }
    function inferAlignRow(ancestors, align) {
      const node = ancestors.at(-1);
      ok$1(node.type === 'table');
      const headEnd = pointEnd(node.children[0]);
      if (!headEnd || typeof headEnd.offset !== 'number') return
      let index = headEnd.offset;
      if (value.charCodeAt(index) === 13 ) index++;
      if (value.charCodeAt(index) !== 10 ) return
      index++;
      const result = [];
      const line = headEnd.line + 1;
      let code = value.charCodeAt(index);
      while (
        code === 9  ||
        code === 32  ||
        code === 62
      ) {
        index++;
        code = value.charCodeAt(index);
      }
      if (
        code !== 45  &&
        code !== 58  &&
        code !== 124
      ) {
        return
      }
      let lineEndOffset = value.indexOf('\n', index);
      if (lineEndOffset === -1) lineEndOffset = value.length;
      if (value.charCodeAt(lineEndOffset - 1) === 13 ) lineEndOffset--;
      let column = 0;
      let cellStart = index;
      let cellEnd = value.indexOf('|', index + (code === 124 ? 1 : 0));
      if (cellEnd === -1 || cellEnd > lineEndOffset) {
        cellEnd = lineEndOffset;
      }
      while (cellStart !== cellEnd) {
        let nextCellEnd = value.indexOf('|', cellEnd + 1);
        if (nextCellEnd === -1 || nextCellEnd > lineEndOffset) {
          nextCellEnd = lineEndOffset;
        }
        if (nextCellEnd === lineEndOffset) {
          let maybeEnd = lineEndOffset;
          let code = value.charCodeAt(maybeEnd - 1);
          while (code === 9  || code === 32 ) {
            maybeEnd--;
            code = value.charCodeAt(maybeEnd - 1);
          }
          if (cellEnd + 1 === maybeEnd) {
            cellEnd = lineEndOffset;
          }
        }
        result.push({
          align: align[column],
          ancestors,
          column,
          size: inferSize(
            {
              line,
              column: cellStart - index + 1,
              offset: cellStart
            },
            {line, column: cellEnd - index + 1, offset: cellEnd},
            cellEnd === lineEndOffset
          )
        });
        cellStart = cellEnd;
        cellEnd = nextCellEnd;
        column++;
      }
      return result
    }
    function inferSize(start, end, tailCell) {
      if (
        end &&
        start &&
        typeof end.offset === 'number' &&
        typeof start.offset === 'number'
      ) {
        let leftIndex = start.offset;
        let left;
        let right;
        if (value.charCodeAt(leftIndex) === 124 ) {
          left = 0;
          leftIndex++;
          while (value.charCodeAt(leftIndex) === 32) {
            left++;
            leftIndex++;
          }
        }
        let rightIndex = end.offset;
        if (tailCell) {
          while (value.charCodeAt(rightIndex - 1) === 32) {
            rightIndex--;
          }
          if (
            rightIndex > leftIndex &&
            value.charCodeAt(rightIndex - 1) === 124
          ) {
            rightIndex--;
          }
          else {
            rightIndex = end.offset;
          }
        }
        const rightEdgeIndex = rightIndex;
        if (value.charCodeAt(rightIndex) === 124 ) {
          right = 0;
          while (
            rightIndex - 1 > leftIndex &&
            value.charCodeAt(rightIndex - 1) === 32
          ) {
            right++;
            rightIndex--;
          }
        }
        return {
          left,
          leftPoint: {
            line: start.line,
            column: start.column + (leftIndex - start.offset),
            offset: leftIndex
          },
          middle: rightIndex - leftIndex,
          right,
          rightPoint: {
            line: end.line,
            column: end.column - (end.offset - rightEdgeIndex),
            offset: rightEdgeIndex
          }
        }
      }
    }
  }
);

/**
 * remark-lint rule to warn when GFM table rows have no initial or
 * final cell delimiter.
 *
 * ## What is this?
 *
 * This package checks that table rows have initial and final delimiters.
 * Tables are a GFM feature enabled with [`remark-gfm`][github-remark-gfm].
 *
 * ## When should I use this?
 *
 * You can use this package to check that tables are consistent.
 *
 * ## API
 *
 * ### `unified().use(remarkLintTablePipes)`
 *
 * Warn when GFM table rows have no initial or final cell delimiter.
 *
 * ###### Parameters
 *
 * There are no options.
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ## Recommendation
 *
 * While tables don’t require initial or final delimiters (the pipes before the
 * first and after the last cells in a row),
 * it arguably does look weird without.
 *
 * ## Fix
 *
 * [`remark-stringify`][github-remark-stringify] with
 * [`remark-gfm`][github-remark-gfm] formats all tables with initial and final
 * delimiters.
 *
 * [api-remark-lint-table-pipes]: #unifieduseremarklinttablepipes
 * [github-remark-gfm]: https://github.com/remarkjs/remark-gfm
 * [github-remark-stringify]: https://github.com/remarkjs/remark/tree/main/packages/remark-stringify
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module table-pipes
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 *
 * @example
 *   {"name": "ok.md", "gfm": true}
 *
 *   Small table:
 *
 *   | Planet | Mean anomaly (°) |
 *   | :- | -: |
 *   | Mercury | 174 796 |
 *
 * @example
 *   {"name": "not-ok.md", "label": "input", "gfm": true}
 *
 *   Planet | Mean anomaly (°)
 *   :- | -:
 *   Mercury | 174 796
 * @example
 *   {"name": "not-ok.md", "label": "output", "gfm": true}
 *
 *   1:1: Unexpected missing closing pipe in row, expected `|`
 *   1:26: Unexpected missing opening pipe in row, expected `|`
 *   2:1: Unexpected missing closing pipe in row, expected `|`
 *   2:8: Unexpected missing opening pipe in row, expected `|`
 *   3:1: Unexpected missing closing pipe in row, expected `|`
 *   3:18: Unexpected missing opening pipe in row, expected `|`
 *
 * @example
 *   {"gfm": true, "label": "input", "name": "missing-cells.md"}
 *
 *   Planet | Symbol | Satellites
 *   :- | - | -
 *   Mercury
 *   Venus | ♀
 *   Earth | ♁ | 1
 *   Mars | ♂ | 2 | 19 412
 * @example
 *   {"gfm": true, "label": "output", "name": "missing-cells.md"}
 *
 *   1:1: Unexpected missing closing pipe in row, expected `|`
 *   1:29: Unexpected missing opening pipe in row, expected `|`
 *   2:1: Unexpected missing closing pipe in row, expected `|`
 *   2:11: Unexpected missing opening pipe in row, expected `|`
 *   3:1: Unexpected missing closing pipe in row, expected `|`
 *   3:8: Unexpected missing opening pipe in row, expected `|`
 *   4:1: Unexpected missing closing pipe in row, expected `|`
 *   4:10: Unexpected missing opening pipe in row, expected `|`
 *   5:1: Unexpected missing closing pipe in row, expected `|`
 *   5:14: Unexpected missing opening pipe in row, expected `|`
 *   6:1: Unexpected missing closing pipe in row, expected `|`
 *   6:22: Unexpected missing opening pipe in row, expected `|`
 *
 * @example
 *   {"gfm": true, "label": "input", "name": "trailing-spaces.md"}
 *
 *   ␠␠Planet␠␠
 *   ␠-:␠
 *
 *   ␠␠| Planet |␠␠
 *   ␠| -: |␠
 * @example
 *   {"gfm": true, "label": "output", "name": "trailing-spaces.md"}
 *
 *   1:3: Unexpected missing closing pipe in row, expected `|`
 *   1:11: Unexpected missing opening pipe in row, expected `|`
 *   2:2: Unexpected missing closing pipe in row, expected `|`
 *   2:5: Unexpected missing opening pipe in row, expected `|`
 *
 * @example
 *   {"gfm": true, "label": "input", "name": "windows.md"}
 *
 *   Mercury␍␊:-␍␊None
 * @example
 *   {"gfm": true, "label": "output", "name": "windows.md"}
 *
 *   1:1: Unexpected missing closing pipe in row, expected `|`
 *   1:8: Unexpected missing opening pipe in row, expected `|`
 *   2:1: Unexpected missing closing pipe in row, expected `|`
 *   2:3: Unexpected missing opening pipe in row, expected `|`
 *   3:1: Unexpected missing closing pipe in row, expected `|`
 *   3:5: Unexpected missing opening pipe in row, expected `|`
 */
const remarkLintTablePipes = lintRule$1(
  {
    origin: 'remark-lint:table-pipes',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-table-pipes#readme'
  },
  function (tree, file) {
    const value = String(file);
    visitParents(tree, function (node, parents) {
      if (phrasing(node)) {
        return SKIP
      }
      if (node.type !== 'table') return
      let index = -1;
      while (++index < node.children.length) {
        const row = node.children[index];
        const start = pointStart(row);
        const end = pointEnd(row);
        if (start && typeof start.offset === 'number') {
          checkStart(start.offset, start, [...parents, node, row]);
        }
        if (end && typeof end.offset === 'number') {
          checkEnd(end.offset, end, [...parents, node, row]);
          if (index === 0) {
            let index = end.offset;
            if (value.charCodeAt(index) === 13 ) index++;
            if (value.charCodeAt(index) !== 10 ) continue
            index++;
            const lineStart = index;
            let code = value.charCodeAt(index);
            while (
              code === 9  ||
              code === 32  ||
              code === 62
            ) {
              index++;
              code = value.charCodeAt(index);
            }
            checkStart(
              index,
              {
                line: end.line + 1,
                column: index - lineStart + 1,
                offset: index
              },
              [...parents, node]
            );
            index = value.indexOf('\n', index);
            if (index === -1) index = value.length;
            if (value.charCodeAt(index - 1) === 13 ) index--;
            checkEnd(
              index,
              {
                line: end.line + 1,
                column: index - lineStart + 1,
                offset: index
              },
              [...parents, node]
            );
          }
        }
      }
      return SKIP
    });
    function checkStart(index, place, ancestors) {
      let code = value.charCodeAt(index);
      while (code === 9  || code === 32 ) {
        code = value.charCodeAt(++index);
      }
      if (code !== 124 ) {
        file.message('Unexpected missing closing pipe in row, expected `|`', {
          ancestors,
          place
        });
      }
    }
    function checkEnd(index, place, ancestors) {
      let code = value.charCodeAt(index - 1);
      while (code === 9  || code === 32 ) {
        index--;
        code = value.charCodeAt(index - 1);
      }
      if (code !== 124 ) {
        file.message('Unexpected missing opening pipe in row, expected `|`', {
          ancestors,
          place
        });
      }
    }
  }
);

/**
 * remark-lint rule to warn when unordered list markers are inconsistent.
 *
 * ## What is this?
 *
 * This package checks unordered list markers.
 *
 * ## When should I use this?
 *
 * You can use this package to check unordered lists.
 *
 * ## API
 *
 * ### `unified().use(remarkLintUnorderedListMarkerStyle[, options])`
 *
 * Warn when unordered list markers are inconsistent.
 *
 * ###### Parameters
 *
 * * `options` ([`Options`][api-options], default: `'consistent'`)
 *   — preferred style or whether to detect the first style and warn for
 *   further differences
 *
 * ###### Returns
 *
 * Transform ([`Transformer` from `unified`][github-unified-transformer]).
 *
 * ### `Options`
 *
 * Configuration (TypeScript type).
 *
 * ###### Type
 *
 * ```ts
 * type Options = Style | 'consistent'
 * ```
 *
 * ### `Style`
 *
 * Style (TypeScript type).
 *
 * ###### Type
 *
 * ```ts
 * type Style = '*' | '+' | '-'
 * ```
 *
 * ## Recommendation
 *
 * Because asterisks can be used as a marker for more markdown constructs,
 * it’s recommended to use that for lists (and thematic breaks, emphasis,
 * strong) too.
 *
 * ## Fix
 *
 * [`remark-stringify`][github-remark-stringify] formats unordered lists with
 * asterisks by default.
 * Pass `bullet: '+'` or `bullet: '-'` to use a different marker.
 *
 * [api-options]: #options
 * [api-style]: #style
 * [api-remark-lint-unordered-list-marker-style]: #unifieduseremarklintunorderedlistmarkerstyle-options
 * [github-remark-stringify]: https://github.com/remarkjs/remark/tree/main/packages/remark-stringify
 * [github-unified-transformer]: https://github.com/unifiedjs/unified#transformer
 *
 * @module unordered-list-marker-style
 * @author Titus Wormer
 * @copyright 2015 Titus Wormer
 * @license MIT
 * @example
 *   {"name": "ok.md"}
 *
 *   * Mercury
 *
 *   1. Venus
 *
 *   * Earth
 *
 * @example
 *   {"name": "ok.md", "config": "*"}
 *
 *   * Mercury
 *
 * @example
 *   {"name": "ok.md", "config": "-"}
 *
 *   - Mercury
 *
 * @example
 *   {"name": "ok.md", "config": "+"}
 *
 *   + Mercury
 *
 * @example
 *   {"name": "not-ok.md", "label": "input"}
 *
 *   * Mercury
 *
 *   - Venus
 *
 *   + Earth
 * @example
 *   {"name": "not-ok.md", "label": "output"}
 *
 *   3:1: Unexpected unordered list marker `-`, expected `*`
 *   5:1: Unexpected unordered list marker `+`, expected `*`
 *
 * @example
 *   {"name": "not-ok.md", "label": "output", "config": "🌍", "positionless": true}
 *
 *   1:1: Unexpected value `🌍` for `options`, expected `'*'`, `'+'`, `'-'`, or `'consistent'`
 */
const remarkLintUnorderedListMarkerStyle = lintRule$1(
  {
    origin: 'remark-lint:unordered-list-marker-style',
    url: 'https://github.com/remarkjs/remark-lint/tree/main/packages/remark-lint-unordered-list-marker-style#readme'
  },
  function (tree, file, options) {
    const value = String(file);
    let expected;
    let cause;
    if (options === null || options === undefined || options === 'consistent') ; else if (options === '*' || options === '+' || options === '-') {
      expected = options;
    } else {
      file.fail(
        'Unexpected value `' +
          options +
          "` for `options`, expected `'*'`, `'+'`, `'-'`, or `'consistent'`"
      );
    }
    visitParents(tree, function (node, parents) {
      if (phrasing(node)) {
        return SKIP
      }
      if (node.type !== 'listItem') return
      const parent = parents.at(-1);
      if (!parent || parent.type !== 'list' || parent.ordered) return
      const place = pointStart(node);
      if (!place || typeof place.offset !== 'number') return
      const code = value.charCodeAt(place.offset);
      const actual =
        code === 42
          ? '*'
          : code === 43
            ? '+'
            : code === 45
              ? '-'
              :
                undefined;
      if (!actual) return
      if (expected) {
        if (actual !== expected) {
          file.message(
            'Unexpected unordered list marker `' +
              actual +
              '`, expected `' +
              expected +
              '`',
            {ancestors: [...parents, node], cause, place}
          );
        }
      } else {
        expected = actual;
        cause = new VFileMessage(
          'Unordered list marker style `' +
            expected +
            "` first defined for `'consistent'` here",
          {
            ancestors: [...parents, node],
            place,
            ruleId: 'unordered-list-marker-style',
            source: 'remark-lint'
          }
        );
      }
    });
  }
);

const plugins = [
  remarkGfm,
  remarkPresetLintRecommended,
  [remarkLintBlockquoteIndentation, 2],
  [remarkLintCheckboxCharacterStyle, { checked: "x", unchecked: " " }],
  remarkLintCheckboxContentIndent,
  [remarkLintCodeBlockStyle, "fenced"],
  remarkLintDefinitionSpacing,
  [
    remarkLintFencedCodeFlag,
    {
      flags: [
        "bash",
        "c",
        "cjs",
        "coffee",
        "console",
        "cpp",
        "diff",
        "http",
        "js",
        "json",
        "markdown",
        "mjs",
        "powershell",
        "r",
        "text",
        "ts",
      ],
    },
  ],
  [remarkLintFencedCodeMarker, "`"],
  [remarkLintFileExtension, "md"],
  remarkLintFinalDefinition,
  [remarkLintFirstHeadingLevel, 1],
  [remarkLintHeadingStyle, "atx"],
  [remarkLintMaximumLineLength, 120],
  remarkLintNoConsecutiveBlankLines,
  remarkLintNoFileNameArticles,
  remarkLintNoFileNameConsecutiveDashes,
  remarkLintNofileNameOuterDashes,
  remarkLintNoHeadingIndent,
  remarkLintNoMultipleToplevelHeadings,
  remarkLintNoShellDollars,
  remarkLintNoTableIndentation,
  remarkLintNoTabs,
  remarkLintNoTrailingSpaces$1,
  remarkLintNodejsLinks,
  remarkLintNodejsYamlComments,
  [
    remarkLintProhibitedStrings,
    [
      { yes: "End-of-Life" },
      { no: "filesystem", yes: "file system" },
      { yes: "GitHub" },
      { no: "hostname", yes: "host name" },
      { yes: "JavaScript" },
      { no: "[Ll]ong[ -][Tt]erm [Ss]upport", yes: "Long Term Support" },
      { no: "Node", yes: "Node.js", ignoreNextTo: "-API" },
      { yes: "Node.js" },
      { no: "Node[Jj][Ss]", yes: "Node.js" },
      { no: "Node\\.js's?", yes: "the Node.js" },
      { no: "[Nn]ote that", yes: "<nothing>" },
      { yes: "RFC" },
      { no: "[Rr][Ff][Cc]\\d+", yes: "RFC <number>" },
      { yes: "TypeScript" },
      { yes: "Unix" },
      { yes: "Valgrind" },
      { yes: "V8" },
    ],
  ],
  remarkLintRuleStyle,
  [remarkLintStrongMarker, "*"],
  [remarkLintTableCellPadding, "padded"],
  remarkLintTablePipes,
  [remarkLintUnorderedListMarkerStyle, "*"],
];
const settings = {
  emphasis: "_",
  tightDefinitions: true,
};
const remarkPresetLintNode = { plugins, settings };

function read(description, options, callback) {
  const file = toVFile(description);
  {
    return new Promise(executor)
  }
  function executor(resolve, reject) {
    let fp;
    try {
      fp = path$1.resolve(file.cwd, file.path);
    } catch (error) {
      const exception =  (error);
      return reject(exception)
    }
    fs$1.readFile(fp, options, done);
    function done(error, result) {
      if (error) {
        reject(error);
      } else {
        file.value = result;
        resolve(file);
      }
    }
  }
}
function toVFile(description) {
  if (typeof description === 'string' || description instanceof URL) {
    description = {path: description};
  } else if (isUint8Array(description)) {
    description = {path: new TextDecoder().decode(description)};
  }
  return looksLikeAVFile(description) ? description : new VFile(description)
}
function looksLikeAVFile(value) {
  return Boolean(
    value &&
      typeof value === 'object' &&
      'message' in value &&
      'messages' in value
  )
}
function isUint8Array(value) {
  return Boolean(
    value &&
      typeof value === 'object' &&
      'byteLength' in value &&
      'byteOffset' in value
  )
}

function ansiRegex({onlyFirst = false} = {}) {
	const pattern = [
	    '[\\u001B\\u009B][[\\]()#;?]*(?:(?:(?:(?:;[-a-zA-Z\\d\\/#&.:=?%@~_]+)*|[a-zA-Z\\d]+(?:;[-a-zA-Z\\d\\/#&.:=?%@~_]*)*)?\\u0007)',
		'(?:(?:\\d{1,4}(?:;\\d{0,4})*)?[\\dA-PR-TZcf-ntqry=><~]))'
	].join('|');
	return new RegExp(pattern, onlyFirst ? undefined : 'g');
}

const regex = ansiRegex();
function stripAnsi(string) {
	if (typeof string !== 'string') {
		throw new TypeError(`Expected a \`string\`, got \`${typeof string}\``);
	}
	return string.replace(regex, '');
}

var eastasianwidth = {exports: {}};

(function (module) {
	var eaw = {};
	{
	  module.exports = eaw;
	}
	eaw.eastAsianWidth = function(character) {
	  var x = character.charCodeAt(0);
	  var y = (character.length == 2) ? character.charCodeAt(1) : 0;
	  var codePoint = x;
	  if ((0xD800 <= x && x <= 0xDBFF) && (0xDC00 <= y && y <= 0xDFFF)) {
	    x &= 0x3FF;
	    y &= 0x3FF;
	    codePoint = (x << 10) | y;
	    codePoint += 0x10000;
	  }
	  if ((0x3000 == codePoint) ||
	      (0xFF01 <= codePoint && codePoint <= 0xFF60) ||
	      (0xFFE0 <= codePoint && codePoint <= 0xFFE6)) {
	    return 'F';
	  }
	  if ((0x20A9 == codePoint) ||
	      (0xFF61 <= codePoint && codePoint <= 0xFFBE) ||
	      (0xFFC2 <= codePoint && codePoint <= 0xFFC7) ||
	      (0xFFCA <= codePoint && codePoint <= 0xFFCF) ||
	      (0xFFD2 <= codePoint && codePoint <= 0xFFD7) ||
	      (0xFFDA <= codePoint && codePoint <= 0xFFDC) ||
	      (0xFFE8 <= codePoint && codePoint <= 0xFFEE)) {
	    return 'H';
	  }
	  if ((0x1100 <= codePoint && codePoint <= 0x115F) ||
	      (0x11A3 <= codePoint && codePoint <= 0x11A7) ||
	      (0x11FA <= codePoint && codePoint <= 0x11FF) ||
	      (0x2329 <= codePoint && codePoint <= 0x232A) ||
	      (0x2E80 <= codePoint && codePoint <= 0x2E99) ||
	      (0x2E9B <= codePoint && codePoint <= 0x2EF3) ||
	      (0x2F00 <= codePoint && codePoint <= 0x2FD5) ||
	      (0x2FF0 <= codePoint && codePoint <= 0x2FFB) ||
	      (0x3001 <= codePoint && codePoint <= 0x303E) ||
	      (0x3041 <= codePoint && codePoint <= 0x3096) ||
	      (0x3099 <= codePoint && codePoint <= 0x30FF) ||
	      (0x3105 <= codePoint && codePoint <= 0x312D) ||
	      (0x3131 <= codePoint && codePoint <= 0x318E) ||
	      (0x3190 <= codePoint && codePoint <= 0x31BA) ||
	      (0x31C0 <= codePoint && codePoint <= 0x31E3) ||
	      (0x31F0 <= codePoint && codePoint <= 0x321E) ||
	      (0x3220 <= codePoint && codePoint <= 0x3247) ||
	      (0x3250 <= codePoint && codePoint <= 0x32FE) ||
	      (0x3300 <= codePoint && codePoint <= 0x4DBF) ||
	      (0x4E00 <= codePoint && codePoint <= 0xA48C) ||
	      (0xA490 <= codePoint && codePoint <= 0xA4C6) ||
	      (0xA960 <= codePoint && codePoint <= 0xA97C) ||
	      (0xAC00 <= codePoint && codePoint <= 0xD7A3) ||
	      (0xD7B0 <= codePoint && codePoint <= 0xD7C6) ||
	      (0xD7CB <= codePoint && codePoint <= 0xD7FB) ||
	      (0xF900 <= codePoint && codePoint <= 0xFAFF) ||
	      (0xFE10 <= codePoint && codePoint <= 0xFE19) ||
	      (0xFE30 <= codePoint && codePoint <= 0xFE52) ||
	      (0xFE54 <= codePoint && codePoint <= 0xFE66) ||
	      (0xFE68 <= codePoint && codePoint <= 0xFE6B) ||
	      (0x1B000 <= codePoint && codePoint <= 0x1B001) ||
	      (0x1F200 <= codePoint && codePoint <= 0x1F202) ||
	      (0x1F210 <= codePoint && codePoint <= 0x1F23A) ||
	      (0x1F240 <= codePoint && codePoint <= 0x1F248) ||
	      (0x1F250 <= codePoint && codePoint <= 0x1F251) ||
	      (0x20000 <= codePoint && codePoint <= 0x2F73F) ||
	      (0x2B740 <= codePoint && codePoint <= 0x2FFFD) ||
	      (0x30000 <= codePoint && codePoint <= 0x3FFFD)) {
	    return 'W';
	  }
	  if ((0x0020 <= codePoint && codePoint <= 0x007E) ||
	      (0x00A2 <= codePoint && codePoint <= 0x00A3) ||
	      (0x00A5 <= codePoint && codePoint <= 0x00A6) ||
	      (0x00AC == codePoint) ||
	      (0x00AF == codePoint) ||
	      (0x27E6 <= codePoint && codePoint <= 0x27ED) ||
	      (0x2985 <= codePoint && codePoint <= 0x2986)) {
	    return 'Na';
	  }
	  if ((0x00A1 == codePoint) ||
	      (0x00A4 == codePoint) ||
	      (0x00A7 <= codePoint && codePoint <= 0x00A8) ||
	      (0x00AA == codePoint) ||
	      (0x00AD <= codePoint && codePoint <= 0x00AE) ||
	      (0x00B0 <= codePoint && codePoint <= 0x00B4) ||
	      (0x00B6 <= codePoint && codePoint <= 0x00BA) ||
	      (0x00BC <= codePoint && codePoint <= 0x00BF) ||
	      (0x00C6 == codePoint) ||
	      (0x00D0 == codePoint) ||
	      (0x00D7 <= codePoint && codePoint <= 0x00D8) ||
	      (0x00DE <= codePoint && codePoint <= 0x00E1) ||
	      (0x00E6 == codePoint) ||
	      (0x00E8 <= codePoint && codePoint <= 0x00EA) ||
	      (0x00EC <= codePoint && codePoint <= 0x00ED) ||
	      (0x00F0 == codePoint) ||
	      (0x00F2 <= codePoint && codePoint <= 0x00F3) ||
	      (0x00F7 <= codePoint && codePoint <= 0x00FA) ||
	      (0x00FC == codePoint) ||
	      (0x00FE == codePoint) ||
	      (0x0101 == codePoint) ||
	      (0x0111 == codePoint) ||
	      (0x0113 == codePoint) ||
	      (0x011B == codePoint) ||
	      (0x0126 <= codePoint && codePoint <= 0x0127) ||
	      (0x012B == codePoint) ||
	      (0x0131 <= codePoint && codePoint <= 0x0133) ||
	      (0x0138 == codePoint) ||
	      (0x013F <= codePoint && codePoint <= 0x0142) ||
	      (0x0144 == codePoint) ||
	      (0x0148 <= codePoint && codePoint <= 0x014B) ||
	      (0x014D == codePoint) ||
	      (0x0152 <= codePoint && codePoint <= 0x0153) ||
	      (0x0166 <= codePoint && codePoint <= 0x0167) ||
	      (0x016B == codePoint) ||
	      (0x01CE == codePoint) ||
	      (0x01D0 == codePoint) ||
	      (0x01D2 == codePoint) ||
	      (0x01D4 == codePoint) ||
	      (0x01D6 == codePoint) ||
	      (0x01D8 == codePoint) ||
	      (0x01DA == codePoint) ||
	      (0x01DC == codePoint) ||
	      (0x0251 == codePoint) ||
	      (0x0261 == codePoint) ||
	      (0x02C4 == codePoint) ||
	      (0x02C7 == codePoint) ||
	      (0x02C9 <= codePoint && codePoint <= 0x02CB) ||
	      (0x02CD == codePoint) ||
	      (0x02D0 == codePoint) ||
	      (0x02D8 <= codePoint && codePoint <= 0x02DB) ||
	      (0x02DD == codePoint) ||
	      (0x02DF == codePoint) ||
	      (0x0300 <= codePoint && codePoint <= 0x036F) ||
	      (0x0391 <= codePoint && codePoint <= 0x03A1) ||
	      (0x03A3 <= codePoint && codePoint <= 0x03A9) ||
	      (0x03B1 <= codePoint && codePoint <= 0x03C1) ||
	      (0x03C3 <= codePoint && codePoint <= 0x03C9) ||
	      (0x0401 == codePoint) ||
	      (0x0410 <= codePoint && codePoint <= 0x044F) ||
	      (0x0451 == codePoint) ||
	      (0x2010 == codePoint) ||
	      (0x2013 <= codePoint && codePoint <= 0x2016) ||
	      (0x2018 <= codePoint && codePoint <= 0x2019) ||
	      (0x201C <= codePoint && codePoint <= 0x201D) ||
	      (0x2020 <= codePoint && codePoint <= 0x2022) ||
	      (0x2024 <= codePoint && codePoint <= 0x2027) ||
	      (0x2030 == codePoint) ||
	      (0x2032 <= codePoint && codePoint <= 0x2033) ||
	      (0x2035 == codePoint) ||
	      (0x203B == codePoint) ||
	      (0x203E == codePoint) ||
	      (0x2074 == codePoint) ||
	      (0x207F == codePoint) ||
	      (0x2081 <= codePoint && codePoint <= 0x2084) ||
	      (0x20AC == codePoint) ||
	      (0x2103 == codePoint) ||
	      (0x2105 == codePoint) ||
	      (0x2109 == codePoint) ||
	      (0x2113 == codePoint) ||
	      (0x2116 == codePoint) ||
	      (0x2121 <= codePoint && codePoint <= 0x2122) ||
	      (0x2126 == codePoint) ||
	      (0x212B == codePoint) ||
	      (0x2153 <= codePoint && codePoint <= 0x2154) ||
	      (0x215B <= codePoint && codePoint <= 0x215E) ||
	      (0x2160 <= codePoint && codePoint <= 0x216B) ||
	      (0x2170 <= codePoint && codePoint <= 0x2179) ||
	      (0x2189 == codePoint) ||
	      (0x2190 <= codePoint && codePoint <= 0x2199) ||
	      (0x21B8 <= codePoint && codePoint <= 0x21B9) ||
	      (0x21D2 == codePoint) ||
	      (0x21D4 == codePoint) ||
	      (0x21E7 == codePoint) ||
	      (0x2200 == codePoint) ||
	      (0x2202 <= codePoint && codePoint <= 0x2203) ||
	      (0x2207 <= codePoint && codePoint <= 0x2208) ||
	      (0x220B == codePoint) ||
	      (0x220F == codePoint) ||
	      (0x2211 == codePoint) ||
	      (0x2215 == codePoint) ||
	      (0x221A == codePoint) ||
	      (0x221D <= codePoint && codePoint <= 0x2220) ||
	      (0x2223 == codePoint) ||
	      (0x2225 == codePoint) ||
	      (0x2227 <= codePoint && codePoint <= 0x222C) ||
	      (0x222E == codePoint) ||
	      (0x2234 <= codePoint && codePoint <= 0x2237) ||
	      (0x223C <= codePoint && codePoint <= 0x223D) ||
	      (0x2248 == codePoint) ||
	      (0x224C == codePoint) ||
	      (0x2252 == codePoint) ||
	      (0x2260 <= codePoint && codePoint <= 0x2261) ||
	      (0x2264 <= codePoint && codePoint <= 0x2267) ||
	      (0x226A <= codePoint && codePoint <= 0x226B) ||
	      (0x226E <= codePoint && codePoint <= 0x226F) ||
	      (0x2282 <= codePoint && codePoint <= 0x2283) ||
	      (0x2286 <= codePoint && codePoint <= 0x2287) ||
	      (0x2295 == codePoint) ||
	      (0x2299 == codePoint) ||
	      (0x22A5 == codePoint) ||
	      (0x22BF == codePoint) ||
	      (0x2312 == codePoint) ||
	      (0x2460 <= codePoint && codePoint <= 0x24E9) ||
	      (0x24EB <= codePoint && codePoint <= 0x254B) ||
	      (0x2550 <= codePoint && codePoint <= 0x2573) ||
	      (0x2580 <= codePoint && codePoint <= 0x258F) ||
	      (0x2592 <= codePoint && codePoint <= 0x2595) ||
	      (0x25A0 <= codePoint && codePoint <= 0x25A1) ||
	      (0x25A3 <= codePoint && codePoint <= 0x25A9) ||
	      (0x25B2 <= codePoint && codePoint <= 0x25B3) ||
	      (0x25B6 <= codePoint && codePoint <= 0x25B7) ||
	      (0x25BC <= codePoint && codePoint <= 0x25BD) ||
	      (0x25C0 <= codePoint && codePoint <= 0x25C1) ||
	      (0x25C6 <= codePoint && codePoint <= 0x25C8) ||
	      (0x25CB == codePoint) ||
	      (0x25CE <= codePoint && codePoint <= 0x25D1) ||
	      (0x25E2 <= codePoint && codePoint <= 0x25E5) ||
	      (0x25EF == codePoint) ||
	      (0x2605 <= codePoint && codePoint <= 0x2606) ||
	      (0x2609 == codePoint) ||
	      (0x260E <= codePoint && codePoint <= 0x260F) ||
	      (0x2614 <= codePoint && codePoint <= 0x2615) ||
	      (0x261C == codePoint) ||
	      (0x261E == codePoint) ||
	      (0x2640 == codePoint) ||
	      (0x2642 == codePoint) ||
	      (0x2660 <= codePoint && codePoint <= 0x2661) ||
	      (0x2663 <= codePoint && codePoint <= 0x2665) ||
	      (0x2667 <= codePoint && codePoint <= 0x266A) ||
	      (0x266C <= codePoint && codePoint <= 0x266D) ||
	      (0x266F == codePoint) ||
	      (0x269E <= codePoint && codePoint <= 0x269F) ||
	      (0x26BE <= codePoint && codePoint <= 0x26BF) ||
	      (0x26C4 <= codePoint && codePoint <= 0x26CD) ||
	      (0x26CF <= codePoint && codePoint <= 0x26E1) ||
	      (0x26E3 == codePoint) ||
	      (0x26E8 <= codePoint && codePoint <= 0x26FF) ||
	      (0x273D == codePoint) ||
	      (0x2757 == codePoint) ||
	      (0x2776 <= codePoint && codePoint <= 0x277F) ||
	      (0x2B55 <= codePoint && codePoint <= 0x2B59) ||
	      (0x3248 <= codePoint && codePoint <= 0x324F) ||
	      (0xE000 <= codePoint && codePoint <= 0xF8FF) ||
	      (0xFE00 <= codePoint && codePoint <= 0xFE0F) ||
	      (0xFFFD == codePoint) ||
	      (0x1F100 <= codePoint && codePoint <= 0x1F10A) ||
	      (0x1F110 <= codePoint && codePoint <= 0x1F12D) ||
	      (0x1F130 <= codePoint && codePoint <= 0x1F169) ||
	      (0x1F170 <= codePoint && codePoint <= 0x1F19A) ||
	      (0xE0100 <= codePoint && codePoint <= 0xE01EF) ||
	      (0xF0000 <= codePoint && codePoint <= 0xFFFFD) ||
	      (0x100000 <= codePoint && codePoint <= 0x10FFFD)) {
	    return 'A';
	  }
	  return 'N';
	};
	eaw.characterLength = function(character) {
	  var code = this.eastAsianWidth(character);
	  if (code == 'F' || code == 'W' || code == 'A') {
	    return 2;
	  } else {
	    return 1;
	  }
	};
	function stringToArray(string) {
	  return string.match(/[\uD800-\uDBFF][\uDC00-\uDFFF]|[^\uD800-\uDFFF]/g) || [];
	}
	eaw.length = function(string) {
	  var characters = stringToArray(string);
	  var len = 0;
	  for (var i = 0; i < characters.length; i++) {
	    len = len + this.characterLength(characters[i]);
	  }
	  return len;
	};
	eaw.slice = function(text, start, end) {
	  textLen = eaw.length(text);
	  start = start ? start : 0;
	  end = end ? end : 1;
	  if (start < 0) {
	      start = textLen + start;
	  }
	  if (end < 0) {
	      end = textLen + end;
	  }
	  var result = '';
	  var eawLen = 0;
	  var chars = stringToArray(text);
	  for (var i = 0; i < chars.length; i++) {
	    var char = chars[i];
	    var charLen = eaw.length(char);
	    if (eawLen >= start - (charLen == 2 ? 1 : 0)) {
	        if (eawLen + charLen <= end) {
	            result += char;
	        } else {
	            break;
	        }
	    }
	    eawLen += charLen;
	  }
	  return result;
	};
} (eastasianwidth));
var eastasianwidthExports = eastasianwidth.exports;
var eastAsianWidth = getDefaultExportFromCjs(eastasianwidthExports);

var emojiRegex = () => {
	return /[#*0-9]\uFE0F?\u20E3|[\xA9\xAE\u203C\u2049\u2122\u2139\u2194-\u2199\u21A9\u21AA\u231A\u231B\u2328\u23CF\u23ED-\u23EF\u23F1\u23F2\u23F8-\u23FA\u24C2\u25AA\u25AB\u25B6\u25C0\u25FB\u25FC\u25FE\u2600-\u2604\u260E\u2611\u2614\u2615\u2618\u2620\u2622\u2623\u2626\u262A\u262E\u262F\u2638-\u263A\u2640\u2642\u2648-\u2653\u265F\u2660\u2663\u2665\u2666\u2668\u267B\u267E\u267F\u2692\u2694-\u2697\u2699\u269B\u269C\u26A0\u26A7\u26AA\u26B0\u26B1\u26BD\u26BE\u26C4\u26C8\u26CF\u26D1\u26E9\u26F0-\u26F5\u26F7\u26F8\u26FA\u2702\u2708\u2709\u270F\u2712\u2714\u2716\u271D\u2721\u2733\u2734\u2744\u2747\u2757\u2763\u27A1\u2934\u2935\u2B05-\u2B07\u2B1B\u2B1C\u2B55\u3030\u303D\u3297\u3299]\uFE0F?|[\u261D\u270C\u270D](?:\uFE0F|\uD83C[\uDFFB-\uDFFF])?|[\u270A\u270B](?:\uD83C[\uDFFB-\uDFFF])?|[\u23E9-\u23EC\u23F0\u23F3\u25FD\u2693\u26A1\u26AB\u26C5\u26CE\u26D4\u26EA\u26FD\u2705\u2728\u274C\u274E\u2753-\u2755\u2795-\u2797\u27B0\u27BF\u2B50]|\u26D3\uFE0F?(?:\u200D\uD83D\uDCA5)?|\u26F9(?:\uFE0F|\uD83C[\uDFFB-\uDFFF])?(?:\u200D[\u2640\u2642]\uFE0F?)?|\u2764\uFE0F?(?:\u200D(?:\uD83D\uDD25|\uD83E\uDE79))?|\uD83C(?:[\uDC04\uDD70\uDD71\uDD7E\uDD7F\uDE02\uDE37\uDF21\uDF24-\uDF2C\uDF36\uDF7D\uDF96\uDF97\uDF99-\uDF9B\uDF9E\uDF9F\uDFCD\uDFCE\uDFD4-\uDFDF\uDFF5\uDFF7]\uFE0F?|[\uDF85\uDFC2\uDFC7](?:\uD83C[\uDFFB-\uDFFF])?|[\uDFC4\uDFCA](?:\uD83C[\uDFFB-\uDFFF])?(?:\u200D[\u2640\u2642]\uFE0F?)?|[\uDFCB\uDFCC](?:\uFE0F|\uD83C[\uDFFB-\uDFFF])?(?:\u200D[\u2640\u2642]\uFE0F?)?|[\uDCCF\uDD8E\uDD91-\uDD9A\uDE01\uDE1A\uDE2F\uDE32-\uDE36\uDE38-\uDE3A\uDE50\uDE51\uDF00-\uDF20\uDF2D-\uDF35\uDF37-\uDF43\uDF45-\uDF4A\uDF4C-\uDF7C\uDF7E-\uDF84\uDF86-\uDF93\uDFA0-\uDFC1\uDFC5\uDFC6\uDFC8\uDFC9\uDFCF-\uDFD3\uDFE0-\uDFF0\uDFF8-\uDFFF]|\uDDE6\uD83C[\uDDE8-\uDDEC\uDDEE\uDDF1\uDDF2\uDDF4\uDDF6-\uDDFA\uDDFC\uDDFD\uDDFF]|\uDDE7\uD83C[\uDDE6\uDDE7\uDDE9-\uDDEF\uDDF1-\uDDF4\uDDF6-\uDDF9\uDDFB\uDDFC\uDDFE\uDDFF]|\uDDE8\uD83C[\uDDE6\uDDE8\uDDE9\uDDEB-\uDDEE\uDDF0-\uDDF5\uDDF7\uDDFA-\uDDFF]|\uDDE9\uD83C[\uDDEA\uDDEC\uDDEF\uDDF0\uDDF2\uDDF4\uDDFF]|\uDDEA\uD83C[\uDDE6\uDDE8\uDDEA\uDDEC\uDDED\uDDF7-\uDDFA]|\uDDEB\uD83C[\uDDEE-\uDDF0\uDDF2\uDDF4\uDDF7]|\uDDEC\uD83C[\uDDE6\uDDE7\uDDE9-\uDDEE\uDDF1-\uDDF3\uDDF5-\uDDFA\uDDFC\uDDFE]|\uDDED\uD83C[\uDDF0\uDDF2\uDDF3\uDDF7\uDDF9\uDDFA]|\uDDEE\uD83C[\uDDE8-\uDDEA\uDDF1-\uDDF4\uDDF6-\uDDF9]|\uDDEF\uD83C[\uDDEA\uDDF2\uDDF4\uDDF5]|\uDDF0\uD83C[\uDDEA\uDDEC-\uDDEE\uDDF2\uDDF3\uDDF5\uDDF7\uDDFC\uDDFE\uDDFF]|\uDDF1\uD83C[\uDDE6-\uDDE8\uDDEE\uDDF0\uDDF7-\uDDFB\uDDFE]|\uDDF2\uD83C[\uDDE6\uDDE8-\uDDED\uDDF0-\uDDFF]|\uDDF3\uD83C[\uDDE6\uDDE8\uDDEA-\uDDEC\uDDEE\uDDF1\uDDF4\uDDF5\uDDF7\uDDFA\uDDFF]|\uDDF4\uD83C\uDDF2|\uDDF5\uD83C[\uDDE6\uDDEA-\uDDED\uDDF0-\uDDF3\uDDF7-\uDDF9\uDDFC\uDDFE]|\uDDF6\uD83C\uDDE6|\uDDF7\uD83C[\uDDEA\uDDF4\uDDF8\uDDFA\uDDFC]|\uDDF8\uD83C[\uDDE6-\uDDEA\uDDEC-\uDDF4\uDDF7-\uDDF9\uDDFB\uDDFD-\uDDFF]|\uDDF9\uD83C[\uDDE6\uDDE8\uDDE9\uDDEB-\uDDED\uDDEF-\uDDF4\uDDF7\uDDF9\uDDFB\uDDFC\uDDFF]|\uDDFA\uD83C[\uDDE6\uDDEC\uDDF2\uDDF3\uDDF8\uDDFE\uDDFF]|\uDDFB\uD83C[\uDDE6\uDDE8\uDDEA\uDDEC\uDDEE\uDDF3\uDDFA]|\uDDFC\uD83C[\uDDEB\uDDF8]|\uDDFD\uD83C\uDDF0|\uDDFE\uD83C[\uDDEA\uDDF9]|\uDDFF\uD83C[\uDDE6\uDDF2\uDDFC]|\uDF44(?:\u200D\uD83D\uDFEB)?|\uDF4B(?:\u200D\uD83D\uDFE9)?|\uDFC3(?:\uD83C[\uDFFB-\uDFFF])?(?:\u200D(?:[\u2640\u2642]\uFE0F?(?:\u200D\u27A1\uFE0F?)?|\u27A1\uFE0F?))?|\uDFF3\uFE0F?(?:\u200D(?:\u26A7\uFE0F?|\uD83C\uDF08))?|\uDFF4(?:\u200D\u2620\uFE0F?|\uDB40\uDC67\uDB40\uDC62\uDB40(?:\uDC65\uDB40\uDC6E\uDB40\uDC67|\uDC73\uDB40\uDC63\uDB40\uDC74|\uDC77\uDB40\uDC6C\uDB40\uDC73)\uDB40\uDC7F)?)|\uD83D(?:[\uDC3F\uDCFD\uDD49\uDD4A\uDD6F\uDD70\uDD73\uDD76-\uDD79\uDD87\uDD8A-\uDD8D\uDDA5\uDDA8\uDDB1\uDDB2\uDDBC\uDDC2-\uDDC4\uDDD1-\uDDD3\uDDDC-\uDDDE\uDDE1\uDDE3\uDDE8\uDDEF\uDDF3\uDDFA\uDECB\uDECD-\uDECF\uDEE0-\uDEE5\uDEE9\uDEF0\uDEF3]\uFE0F?|[\uDC42\uDC43\uDC46-\uDC50\uDC66\uDC67\uDC6B-\uDC6D\uDC72\uDC74-\uDC76\uDC78\uDC7C\uDC83\uDC85\uDC8F\uDC91\uDCAA\uDD7A\uDD95\uDD96\uDE4C\uDE4F\uDEC0\uDECC](?:\uD83C[\uDFFB-\uDFFF])?|[\uDC6E\uDC70\uDC71\uDC73\uDC77\uDC81\uDC82\uDC86\uDC87\uDE45-\uDE47\uDE4B\uDE4D\uDE4E\uDEA3\uDEB4\uDEB5](?:\uD83C[\uDFFB-\uDFFF])?(?:\u200D[\u2640\u2642]\uFE0F?)?|[\uDD74\uDD90](?:\uFE0F|\uD83C[\uDFFB-\uDFFF])?|[\uDC00-\uDC07\uDC09-\uDC14\uDC16-\uDC25\uDC27-\uDC3A\uDC3C-\uDC3E\uDC40\uDC44\uDC45\uDC51-\uDC65\uDC6A\uDC79-\uDC7B\uDC7D-\uDC80\uDC84\uDC88-\uDC8E\uDC90\uDC92-\uDCA9\uDCAB-\uDCFC\uDCFF-\uDD3D\uDD4B-\uDD4E\uDD50-\uDD67\uDDA4\uDDFB-\uDE2D\uDE2F-\uDE34\uDE37-\uDE41\uDE43\uDE44\uDE48-\uDE4A\uDE80-\uDEA2\uDEA4-\uDEB3\uDEB7-\uDEBF\uDEC1-\uDEC5\uDED0-\uDED2\uDED5-\uDED7\uDEDC-\uDEDF\uDEEB\uDEEC\uDEF4-\uDEFC\uDFE0-\uDFEB\uDFF0]|\uDC08(?:\u200D\u2B1B)?|\uDC15(?:\u200D\uD83E\uDDBA)?|\uDC26(?:\u200D(?:\u2B1B|\uD83D\uDD25))?|\uDC3B(?:\u200D\u2744\uFE0F?)?|\uDC41\uFE0F?(?:\u200D\uD83D\uDDE8\uFE0F?)?|\uDC68(?:\u200D(?:[\u2695\u2696\u2708]\uFE0F?|\u2764\uFE0F?\u200D\uD83D(?:\uDC8B\u200D\uD83D)?\uDC68|\uD83C[\uDF3E\uDF73\uDF7C\uDF93\uDFA4\uDFA8\uDFEB\uDFED]|\uD83D(?:[\uDC68\uDC69]\u200D\uD83D(?:\uDC66(?:\u200D\uD83D\uDC66)?|\uDC67(?:\u200D\uD83D[\uDC66\uDC67])?)|[\uDCBB\uDCBC\uDD27\uDD2C\uDE80\uDE92]|\uDC66(?:\u200D\uD83D\uDC66)?|\uDC67(?:\u200D\uD83D[\uDC66\uDC67])?)|\uD83E(?:[\uDDAF\uDDBC\uDDBD](?:\u200D\u27A1\uFE0F?)?|[\uDDB0-\uDDB3]))|\uD83C(?:\uDFFB(?:\u200D(?:[\u2695\u2696\u2708]\uFE0F?|\u2764\uFE0F?\u200D\uD83D(?:\uDC8B\u200D\uD83D)?\uDC68\uD83C[\uDFFB-\uDFFF]|\uD83C[\uDF3E\uDF73\uDF7C\uDF93\uDFA4\uDFA8\uDFEB\uDFED]|\uD83D[\uDCBB\uDCBC\uDD27\uDD2C\uDE80\uDE92]|\uD83E(?:[\uDDAF\uDDBC\uDDBD](?:\u200D\u27A1\uFE0F?)?|[\uDDB0-\uDDB3]|\uDD1D\u200D\uD83D\uDC68\uD83C[\uDFFC-\uDFFF])))?|\uDFFC(?:\u200D(?:[\u2695\u2696\u2708]\uFE0F?|\u2764\uFE0F?\u200D\uD83D(?:\uDC8B\u200D\uD83D)?\uDC68\uD83C[\uDFFB-\uDFFF]|\uD83C[\uDF3E\uDF73\uDF7C\uDF93\uDFA4\uDFA8\uDFEB\uDFED]|\uD83D[\uDCBB\uDCBC\uDD27\uDD2C\uDE80\uDE92]|\uD83E(?:[\uDDAF\uDDBC\uDDBD](?:\u200D\u27A1\uFE0F?)?|[\uDDB0-\uDDB3]|\uDD1D\u200D\uD83D\uDC68\uD83C[\uDFFB\uDFFD-\uDFFF])))?|\uDFFD(?:\u200D(?:[\u2695\u2696\u2708]\uFE0F?|\u2764\uFE0F?\u200D\uD83D(?:\uDC8B\u200D\uD83D)?\uDC68\uD83C[\uDFFB-\uDFFF]|\uD83C[\uDF3E\uDF73\uDF7C\uDF93\uDFA4\uDFA8\uDFEB\uDFED]|\uD83D[\uDCBB\uDCBC\uDD27\uDD2C\uDE80\uDE92]|\uD83E(?:[\uDDAF\uDDBC\uDDBD](?:\u200D\u27A1\uFE0F?)?|[\uDDB0-\uDDB3]|\uDD1D\u200D\uD83D\uDC68\uD83C[\uDFFB\uDFFC\uDFFE\uDFFF])))?|\uDFFE(?:\u200D(?:[\u2695\u2696\u2708]\uFE0F?|\u2764\uFE0F?\u200D\uD83D(?:\uDC8B\u200D\uD83D)?\uDC68\uD83C[\uDFFB-\uDFFF]|\uD83C[\uDF3E\uDF73\uDF7C\uDF93\uDFA4\uDFA8\uDFEB\uDFED]|\uD83D[\uDCBB\uDCBC\uDD27\uDD2C\uDE80\uDE92]|\uD83E(?:[\uDDAF\uDDBC\uDDBD](?:\u200D\u27A1\uFE0F?)?|[\uDDB0-\uDDB3]|\uDD1D\u200D\uD83D\uDC68\uD83C[\uDFFB-\uDFFD\uDFFF])))?|\uDFFF(?:\u200D(?:[\u2695\u2696\u2708]\uFE0F?|\u2764\uFE0F?\u200D\uD83D(?:\uDC8B\u200D\uD83D)?\uDC68\uD83C[\uDFFB-\uDFFF]|\uD83C[\uDF3E\uDF73\uDF7C\uDF93\uDFA4\uDFA8\uDFEB\uDFED]|\uD83D[\uDCBB\uDCBC\uDD27\uDD2C\uDE80\uDE92]|\uD83E(?:[\uDDAF\uDDBC\uDDBD](?:\u200D\u27A1\uFE0F?)?|[\uDDB0-\uDDB3]|\uDD1D\u200D\uD83D\uDC68\uD83C[\uDFFB-\uDFFE])))?))?|\uDC69(?:\u200D(?:[\u2695\u2696\u2708]\uFE0F?|\u2764\uFE0F?\u200D\uD83D(?:\uDC8B\u200D\uD83D)?[\uDC68\uDC69]|\uD83C[\uDF3E\uDF73\uDF7C\uDF93\uDFA4\uDFA8\uDFEB\uDFED]|\uD83D(?:[\uDCBB\uDCBC\uDD27\uDD2C\uDE80\uDE92]|\uDC66(?:\u200D\uD83D\uDC66)?|\uDC67(?:\u200D\uD83D[\uDC66\uDC67])?|\uDC69\u200D\uD83D(?:\uDC66(?:\u200D\uD83D\uDC66)?|\uDC67(?:\u200D\uD83D[\uDC66\uDC67])?))|\uD83E(?:[\uDDAF\uDDBC\uDDBD](?:\u200D\u27A1\uFE0F?)?|[\uDDB0-\uDDB3]))|\uD83C(?:\uDFFB(?:\u200D(?:[\u2695\u2696\u2708]\uFE0F?|\u2764\uFE0F?\u200D\uD83D(?:[\uDC68\uDC69]|\uDC8B\u200D\uD83D[\uDC68\uDC69])\uD83C[\uDFFB-\uDFFF]|\uD83C[\uDF3E\uDF73\uDF7C\uDF93\uDFA4\uDFA8\uDFEB\uDFED]|\uD83D[\uDCBB\uDCBC\uDD27\uDD2C\uDE80\uDE92]|\uD83E(?:[\uDDAF\uDDBC\uDDBD](?:\u200D\u27A1\uFE0F?)?|[\uDDB0-\uDDB3]|\uDD1D\u200D\uD83D[\uDC68\uDC69]\uD83C[\uDFFC-\uDFFF])))?|\uDFFC(?:\u200D(?:[\u2695\u2696\u2708]\uFE0F?|\u2764\uFE0F?\u200D\uD83D(?:[\uDC68\uDC69]|\uDC8B\u200D\uD83D[\uDC68\uDC69])\uD83C[\uDFFB-\uDFFF]|\uD83C[\uDF3E\uDF73\uDF7C\uDF93\uDFA4\uDFA8\uDFEB\uDFED]|\uD83D[\uDCBB\uDCBC\uDD27\uDD2C\uDE80\uDE92]|\uD83E(?:[\uDDAF\uDDBC\uDDBD](?:\u200D\u27A1\uFE0F?)?|[\uDDB0-\uDDB3]|\uDD1D\u200D\uD83D[\uDC68\uDC69]\uD83C[\uDFFB\uDFFD-\uDFFF])))?|\uDFFD(?:\u200D(?:[\u2695\u2696\u2708]\uFE0F?|\u2764\uFE0F?\u200D\uD83D(?:[\uDC68\uDC69]|\uDC8B\u200D\uD83D[\uDC68\uDC69])\uD83C[\uDFFB-\uDFFF]|\uD83C[\uDF3E\uDF73\uDF7C\uDF93\uDFA4\uDFA8\uDFEB\uDFED]|\uD83D[\uDCBB\uDCBC\uDD27\uDD2C\uDE80\uDE92]|\uD83E(?:[\uDDAF\uDDBC\uDDBD](?:\u200D\u27A1\uFE0F?)?|[\uDDB0-\uDDB3]|\uDD1D\u200D\uD83D[\uDC68\uDC69]\uD83C[\uDFFB\uDFFC\uDFFE\uDFFF])))?|\uDFFE(?:\u200D(?:[\u2695\u2696\u2708]\uFE0F?|\u2764\uFE0F?\u200D\uD83D(?:[\uDC68\uDC69]|\uDC8B\u200D\uD83D[\uDC68\uDC69])\uD83C[\uDFFB-\uDFFF]|\uD83C[\uDF3E\uDF73\uDF7C\uDF93\uDFA4\uDFA8\uDFEB\uDFED]|\uD83D[\uDCBB\uDCBC\uDD27\uDD2C\uDE80\uDE92]|\uD83E(?:[\uDDAF\uDDBC\uDDBD](?:\u200D\u27A1\uFE0F?)?|[\uDDB0-\uDDB3]|\uDD1D\u200D\uD83D[\uDC68\uDC69]\uD83C[\uDFFB-\uDFFD\uDFFF])))?|\uDFFF(?:\u200D(?:[\u2695\u2696\u2708]\uFE0F?|\u2764\uFE0F?\u200D\uD83D(?:[\uDC68\uDC69]|\uDC8B\u200D\uD83D[\uDC68\uDC69])\uD83C[\uDFFB-\uDFFF]|\uD83C[\uDF3E\uDF73\uDF7C\uDF93\uDFA4\uDFA8\uDFEB\uDFED]|\uD83D[\uDCBB\uDCBC\uDD27\uDD2C\uDE80\uDE92]|\uD83E(?:[\uDDAF\uDDBC\uDDBD](?:\u200D\u27A1\uFE0F?)?|[\uDDB0-\uDDB3]|\uDD1D\u200D\uD83D[\uDC68\uDC69]\uD83C[\uDFFB-\uDFFE])))?))?|\uDC6F(?:\u200D[\u2640\u2642]\uFE0F?)?|\uDD75(?:\uFE0F|\uD83C[\uDFFB-\uDFFF])?(?:\u200D[\u2640\u2642]\uFE0F?)?|\uDE2E(?:\u200D\uD83D\uDCA8)?|\uDE35(?:\u200D\uD83D\uDCAB)?|\uDE36(?:\u200D\uD83C\uDF2B\uFE0F?)?|\uDE42(?:\u200D[\u2194\u2195]\uFE0F?)?|\uDEB6(?:\uD83C[\uDFFB-\uDFFF])?(?:\u200D(?:[\u2640\u2642]\uFE0F?(?:\u200D\u27A1\uFE0F?)?|\u27A1\uFE0F?))?)|\uD83E(?:[\uDD0C\uDD0F\uDD18-\uDD1F\uDD30-\uDD34\uDD36\uDD77\uDDB5\uDDB6\uDDBB\uDDD2\uDDD3\uDDD5\uDEC3-\uDEC5\uDEF0\uDEF2-\uDEF8](?:\uD83C[\uDFFB-\uDFFF])?|[\uDD26\uDD35\uDD37-\uDD39\uDD3D\uDD3E\uDDB8\uDDB9\uDDCD\uDDCF\uDDD4\uDDD6-\uDDDD](?:\uD83C[\uDFFB-\uDFFF])?(?:\u200D[\u2640\u2642]\uFE0F?)?|[\uDDDE\uDDDF](?:\u200D[\u2640\u2642]\uFE0F?)?|[\uDD0D\uDD0E\uDD10-\uDD17\uDD20-\uDD25\uDD27-\uDD2F\uDD3A\uDD3F-\uDD45\uDD47-\uDD76\uDD78-\uDDB4\uDDB7\uDDBA\uDDBC-\uDDCC\uDDD0\uDDE0-\uDDFF\uDE70-\uDE7C\uDE80-\uDE88\uDE90-\uDEBD\uDEBF-\uDEC2\uDECE-\uDEDB\uDEE0-\uDEE8]|\uDD3C(?:\u200D[\u2640\u2642]\uFE0F?|\uD83C[\uDFFB-\uDFFF])?|\uDDCE(?:\uD83C[\uDFFB-\uDFFF])?(?:\u200D(?:[\u2640\u2642]\uFE0F?(?:\u200D\u27A1\uFE0F?)?|\u27A1\uFE0F?))?|\uDDD1(?:\u200D(?:[\u2695\u2696\u2708]\uFE0F?|\uD83C[\uDF3E\uDF73\uDF7C\uDF84\uDF93\uDFA4\uDFA8\uDFEB\uDFED]|\uD83D[\uDCBB\uDCBC\uDD27\uDD2C\uDE80\uDE92]|\uD83E(?:[\uDDAF\uDDBC\uDDBD](?:\u200D\u27A1\uFE0F?)?|[\uDDB0-\uDDB3]|\uDD1D\u200D\uD83E\uDDD1|\uDDD1\u200D\uD83E\uDDD2(?:\u200D\uD83E\uDDD2)?|\uDDD2(?:\u200D\uD83E\uDDD2)?))|\uD83C(?:\uDFFB(?:\u200D(?:[\u2695\u2696\u2708]\uFE0F?|\u2764\uFE0F?\u200D(?:\uD83D\uDC8B\u200D)?\uD83E\uDDD1\uD83C[\uDFFC-\uDFFF]|\uD83C[\uDF3E\uDF73\uDF7C\uDF84\uDF93\uDFA4\uDFA8\uDFEB\uDFED]|\uD83D[\uDCBB\uDCBC\uDD27\uDD2C\uDE80\uDE92]|\uD83E(?:[\uDDAF\uDDBC\uDDBD](?:\u200D\u27A1\uFE0F?)?|[\uDDB0-\uDDB3]|\uDD1D\u200D\uD83E\uDDD1\uD83C[\uDFFB-\uDFFF])))?|\uDFFC(?:\u200D(?:[\u2695\u2696\u2708]\uFE0F?|\u2764\uFE0F?\u200D(?:\uD83D\uDC8B\u200D)?\uD83E\uDDD1\uD83C[\uDFFB\uDFFD-\uDFFF]|\uD83C[\uDF3E\uDF73\uDF7C\uDF84\uDF93\uDFA4\uDFA8\uDFEB\uDFED]|\uD83D[\uDCBB\uDCBC\uDD27\uDD2C\uDE80\uDE92]|\uD83E(?:[\uDDAF\uDDBC\uDDBD](?:\u200D\u27A1\uFE0F?)?|[\uDDB0-\uDDB3]|\uDD1D\u200D\uD83E\uDDD1\uD83C[\uDFFB-\uDFFF])))?|\uDFFD(?:\u200D(?:[\u2695\u2696\u2708]\uFE0F?|\u2764\uFE0F?\u200D(?:\uD83D\uDC8B\u200D)?\uD83E\uDDD1\uD83C[\uDFFB\uDFFC\uDFFE\uDFFF]|\uD83C[\uDF3E\uDF73\uDF7C\uDF84\uDF93\uDFA4\uDFA8\uDFEB\uDFED]|\uD83D[\uDCBB\uDCBC\uDD27\uDD2C\uDE80\uDE92]|\uD83E(?:[\uDDAF\uDDBC\uDDBD](?:\u200D\u27A1\uFE0F?)?|[\uDDB0-\uDDB3]|\uDD1D\u200D\uD83E\uDDD1\uD83C[\uDFFB-\uDFFF])))?|\uDFFE(?:\u200D(?:[\u2695\u2696\u2708]\uFE0F?|\u2764\uFE0F?\u200D(?:\uD83D\uDC8B\u200D)?\uD83E\uDDD1\uD83C[\uDFFB-\uDFFD\uDFFF]|\uD83C[\uDF3E\uDF73\uDF7C\uDF84\uDF93\uDFA4\uDFA8\uDFEB\uDFED]|\uD83D[\uDCBB\uDCBC\uDD27\uDD2C\uDE80\uDE92]|\uD83E(?:[\uDDAF\uDDBC\uDDBD](?:\u200D\u27A1\uFE0F?)?|[\uDDB0-\uDDB3]|\uDD1D\u200D\uD83E\uDDD1\uD83C[\uDFFB-\uDFFF])))?|\uDFFF(?:\u200D(?:[\u2695\u2696\u2708]\uFE0F?|\u2764\uFE0F?\u200D(?:\uD83D\uDC8B\u200D)?\uD83E\uDDD1\uD83C[\uDFFB-\uDFFE]|\uD83C[\uDF3E\uDF73\uDF7C\uDF84\uDF93\uDFA4\uDFA8\uDFEB\uDFED]|\uD83D[\uDCBB\uDCBC\uDD27\uDD2C\uDE80\uDE92]|\uD83E(?:[\uDDAF\uDDBC\uDDBD](?:\u200D\u27A1\uFE0F?)?|[\uDDB0-\uDDB3]|\uDD1D\u200D\uD83E\uDDD1\uD83C[\uDFFB-\uDFFF])))?))?|\uDEF1(?:\uD83C(?:\uDFFB(?:\u200D\uD83E\uDEF2\uD83C[\uDFFC-\uDFFF])?|\uDFFC(?:\u200D\uD83E\uDEF2\uD83C[\uDFFB\uDFFD-\uDFFF])?|\uDFFD(?:\u200D\uD83E\uDEF2\uD83C[\uDFFB\uDFFC\uDFFE\uDFFF])?|\uDFFE(?:\u200D\uD83E\uDEF2\uD83C[\uDFFB-\uDFFD\uDFFF])?|\uDFFF(?:\u200D\uD83E\uDEF2\uD83C[\uDFFB-\uDFFE])?))?)/g;
};

function stringWidth(string, options) {
	if (typeof string !== 'string' || string.length === 0) {
		return 0;
	}
	options = {
		ambiguousIsNarrow: true,
		countAnsiEscapeCodes: false,
		...options,
	};
	if (!options.countAnsiEscapeCodes) {
		string = stripAnsi(string);
	}
	if (string.length === 0) {
		return 0;
	}
	const ambiguousCharacterWidth = options.ambiguousIsNarrow ? 1 : 2;
	let width = 0;
	for (const {segment: character} of new Intl.Segmenter().segment(string)) {
		const codePoint = character.codePointAt(0);
		if (codePoint <= 0x1F || (codePoint >= 0x7F && codePoint <= 0x9F)) {
			continue;
		}
		if (codePoint >= 0x3_00 && codePoint <= 0x3_6F) {
			continue;
		}
		if (emojiRegex().test(character)) {
			width += 2;
			continue;
		}
		const code = eastAsianWidth.eastAsianWidth(character);
		switch (code) {
			case 'F':
			case 'W': {
				width += 2;
				break;
			}
			case 'A': {
				width += ambiguousCharacterWidth;
				break;
			}
			default: {
				width += 1;
			}
		}
	}
	return width;
}

function compareFile(a, b) {
  return compareString(a, b, 'path')
}
function compareMessage(a, b) {
  return (
    compareNumber(a, b, 'line') ||
    compareNumber(a, b, 'column') ||
    compareBoolean(a, b, 'fatal') ||
    compareString(a, b, 'source') ||
    compareString(a, b, 'ruleId') ||
    compareString(a, b, 'reason')
  )
}
function compareBoolean(a, b, field) {
  return scoreNullableBoolean(a[field]) - scoreNullableBoolean(b[field])
}
function compareNumber(a, b, field) {
  return (a[field] || 0) - (b[field] || 0)
}
function compareString(a, b, field) {
  return String(a[field] || '').localeCompare(String(b[field] || ''))
}
function scoreNullableBoolean(value) {
  return value ? 0 : value === false ? 1 : 2
}

function statistics(value) {
  const result = {fatal: 0, warn: 0, info: 0};
  if (!value) {
    throw new TypeError(
      'Expected file or message for `value`, not `' + value + '`'
    )
  }
  if (Array.isArray(value)) {
    list(value);
  } else {
    one(value);
  }
  return {
    fatal: result.fatal,
    nonfatal: result.warn + result.info,
    warn: result.warn,
    info: result.info,
    total: result.fatal + result.warn + result.info
  }
  function list(value) {
    let index = -1;
    while (++index < value.length) {
      one(value[index]);
    }
  }
  function one(value) {
    if ('messages' in value) return list(value.messages)
    result[value.fatal ? 'fatal' : value.fatal === false ? 'warn' : 'info']++;
  }
}

function hasFlag(flag, argv = globalThis.Deno ? globalThis.Deno.args : process$1.argv) {
	const prefix = flag.startsWith('-') ? '' : (flag.length === 1 ? '-' : '--');
	const position = argv.indexOf(prefix + flag);
	const terminatorPosition = argv.indexOf('--');
	return position !== -1 && (terminatorPosition === -1 || position < terminatorPosition);
}
const {env} = process$1;
let flagForceColor;
if (
	hasFlag('no-color')
	|| hasFlag('no-colors')
	|| hasFlag('color=false')
	|| hasFlag('color=never')
) {
	flagForceColor = 0;
} else if (
	hasFlag('color')
	|| hasFlag('colors')
	|| hasFlag('color=true')
	|| hasFlag('color=always')
) {
	flagForceColor = 1;
}
function envForceColor() {
	if ('FORCE_COLOR' in env) {
		if (env.FORCE_COLOR === 'true') {
			return 1;
		}
		if (env.FORCE_COLOR === 'false') {
			return 0;
		}
		return env.FORCE_COLOR.length === 0 ? 1 : Math.min(Number.parseInt(env.FORCE_COLOR, 10), 3);
	}
}
function translateLevel(level) {
	if (level === 0) {
		return false;
	}
	return {
		level,
		hasBasic: true,
		has256: level >= 2,
		has16m: level >= 3,
	};
}
function _supportsColor(haveStream, {streamIsTTY, sniffFlags = true} = {}) {
	const noFlagForceColor = envForceColor();
	if (noFlagForceColor !== undefined) {
		flagForceColor = noFlagForceColor;
	}
	const forceColor = sniffFlags ? flagForceColor : noFlagForceColor;
	if (forceColor === 0) {
		return 0;
	}
	if (sniffFlags) {
		if (hasFlag('color=16m')
			|| hasFlag('color=full')
			|| hasFlag('color=truecolor')) {
			return 3;
		}
		if (hasFlag('color=256')) {
			return 2;
		}
	}
	if ('TF_BUILD' in env && 'AGENT_NAME' in env) {
		return 1;
	}
	if (haveStream && !streamIsTTY && forceColor === undefined) {
		return 0;
	}
	const min = forceColor || 0;
	if (env.TERM === 'dumb') {
		return min;
	}
	if (process$1.platform === 'win32') {
		const osRelease = os.release().split('.');
		if (
			Number(osRelease[0]) >= 10
			&& Number(osRelease[2]) >= 10_586
		) {
			return Number(osRelease[2]) >= 14_931 ? 3 : 2;
		}
		return 1;
	}
	if ('CI' in env) {
		if ('GITHUB_ACTIONS' in env || 'GITEA_ACTIONS' in env) {
			return 3;
		}
		if (['TRAVIS', 'CIRCLECI', 'APPVEYOR', 'GITLAB_CI', 'BUILDKITE', 'DRONE'].some(sign => sign in env) || env.CI_NAME === 'codeship') {
			return 1;
		}
		return min;
	}
	if ('TEAMCITY_VERSION' in env) {
		return /^(9\.(0*[1-9]\d*)\.|\d{2,}\.)/.test(env.TEAMCITY_VERSION) ? 1 : 0;
	}
	if (env.COLORTERM === 'truecolor') {
		return 3;
	}
	if (env.TERM === 'xterm-kitty') {
		return 3;
	}
	if ('TERM_PROGRAM' in env) {
		const version = Number.parseInt((env.TERM_PROGRAM_VERSION || '').split('.')[0], 10);
		switch (env.TERM_PROGRAM) {
			case 'iTerm.app': {
				return version >= 3 ? 3 : 2;
			}
			case 'Apple_Terminal': {
				return 2;
			}
		}
	}
	if (/-256(color)?$/i.test(env.TERM)) {
		return 2;
	}
	if (/^screen|^xterm|^vt100|^vt220|^rxvt|color|ansi|cygwin|linux/i.test(env.TERM)) {
		return 1;
	}
	if ('COLORTERM' in env) {
		return 1;
	}
	return min;
}
function createSupportsColor(stream, options = {}) {
	const level = _supportsColor(stream, {
		streamIsTTY: stream && stream.isTTY,
		...options,
	});
	return translateLevel(level);
}
const supportsColor = {
	stdout: createSupportsColor({isTTY: tty.isatty(1)}),
	stderr: createSupportsColor({isTTY: tty.isatty(2)}),
};

const color = supportsColor.stderr.hasBasic;

const eol = /\r?\n|\r/;
function reporter(files, options) {
  if (
    !files ||
    ('name' in files && 'message' in files)
  ) {
    throw new TypeError(
      'Unexpected value for `files`, expected one or more `VFile`s'
    )
  }
  const settings = {};
  const colorEnabled =
    typeof settings.color === 'boolean' ? settings.color : color;
  let oneFileMode = false;
  if (Array.isArray(files)) ; else {
    oneFileMode = true;
    files = [files];
  }
  return serializeRows(
    createRows(
      {
        defaultName: settings.defaultName || undefined,
        oneFileMode,
        quiet: settings.quiet || false,
        silent: settings.silent || false,
        traceLimit:
          typeof settings.traceLimit === 'number' ? settings.traceLimit : 10,
        verbose: settings.verbose || false,
        bold: colorEnabled ? '\u001B[1m' : '',
        underline: colorEnabled ? '\u001B[4m' : '',
        normalIntensity: colorEnabled ? '\u001B[22m' : '',
        noUnderline: colorEnabled ? '\u001B[24m' : '',
        red: colorEnabled ? '\u001B[31m' : '',
        cyan: colorEnabled ? '\u001B[36m' : '',
        green: colorEnabled ? '\u001B[32m' : '',
        yellow: colorEnabled ? '\u001B[33m' : '',
        defaultColor: colorEnabled ? '\u001B[39m' : ''
      },
      files
    )
  )
}
function createAncestorsLines(state, ancestors) {
  const min =
    ancestors.length > state.traceLimit
      ? ancestors.length - state.traceLimit
      : 0;
  let index = ancestors.length;
  const lines = [];
  if (index > min) {
    lines.unshift('  ' + state.bold + '[trace]' + state.normalIntensity + ':');
  }
  while (index-- > min) {
    const node = ancestors[index];
    const value = node;
    const name =
      typeof value.tagName === 'string'
        ? value.tagName
        :
          typeof value.name === 'string'
          ? value.name
          : undefined;
    const position = stringifyPosition(node.position);
    lines.push(
      '    at ' +
        state.yellow +
        node.type +
        (name ? '<' + name + '>' : '') +
        state.defaultColor +
        (position ? ' (' + position + ')' : '')
    );
  }
  return lines
}
function createByline(state, stats) {
  let result = '';
  if (stats.fatal) {
    result =
      state.red +
      '✖' +
      state.defaultColor +
      ' ' +
      stats.fatal +
      ' ' +
      (fatalToLabel(true) + (stats.fatal === 1 ? '' : 's'));
  }
  if (stats.warn) {
    result =
      (result ? result + ', ' : '') +
      (state.yellow + '⚠' + state.defaultColor) +
      ' ' +
      stats.warn +
      ' ' +
      (fatalToLabel(false) + (stats.warn === 1 ? '' : 's'));
  }
  if (stats.total !== stats.fatal && stats.total !== stats.warn) {
    result = stats.total + ' messages (' + result + ')';
  }
  return result
}
function createCauseLines(state, cause) {
  const lines = ['  ' + state.bold + '[cause]' + state.normalIntensity + ':'];
  let foundReasonableCause = false;
  if (cause !== null && typeof cause === 'object') {
    const stackValue =
      ('stack' in cause ? String(cause.stack) : undefined) ||
      ('message' in cause ? String(cause.message) : undefined);
    if (typeof stackValue === 'string') {
      foundReasonableCause = true;
      let causeLines;
      if ('file' in cause && 'fatal' in cause) {
        causeLines = createMessageLine(
          state,
           (cause)
        );
      }
      else {
        causeLines = stackValue.split(eol);
        if ('cause' in cause && cause.cause) {
          causeLines.push(...createCauseLines(state, cause.cause));
        }
      }
      const head = causeLines[0];
      if (typeof head === 'string') {
        causeLines[0] = '    ' + head;
      } else {
        head[0] = '    ' + head[0];
      }
      lines.push(...causeLines);
    }
  }
  if (!foundReasonableCause) {
    lines.push('    ' + cause);
  }
  return lines
}
function createFileLine(state, file) {
  const stats = statistics(file.messages);
  const fromPath = file.history[0];
  const toPath = file.path;
  let left = '';
  let right = '';
  if (!state.oneFileMode || state.defaultName || fromPath) {
    const name = fromPath || state.defaultName || '<stdin>';
    left =
      state.underline +
      (stats.fatal ? state.red : stats.total ? state.yellow : state.green) +
      name +
      state.defaultColor +
      state.noUnderline +
      (file.stored && name !== toPath ? ' > ' + toPath : '');
  }
  if (file.stored) {
    right = state.yellow + 'written' + state.defaultColor;
  } else if (!stats.total) {
    right = 'no issues found';
  }
  return left && right ? left + ': ' + right : left + right
}
function createNoteLines(state, note) {
  const noteLines = note.split(eol);
  let index = -1;
  while (++index < noteLines.length) {
    noteLines[index] = '    ' + noteLines[index];
  }
  return [
    '  ' + state.bold + '[note]' + state.normalIntensity + ':',
    ...noteLines
  ]
}
function createMessageLine(state, message) {
  const label = fatalToLabel(message.fatal);
  let reason = message.stack || message.message;
  const match = eol.exec(reason);
  let rest = [];
  if (match) {
    rest = reason.slice(match.index + 1).split(eol);
    reason = reason.slice(0, match.index);
  }
  const place = message.place || message.position;
  const row = [
    stringifyPosition(place),
    (label === 'error' ? state.red : state.yellow) + label + state.defaultColor,
    formatReason(state, reason),
    message.ruleId || '',
    message.source || ''
  ];
  if (message.cause) {
    rest.push(...createCauseLines(state, message.cause));
  }
  if (state.verbose && message.url) {
    rest.push(...createUrlLines(state, message.url));
  }
  if (state.verbose && message.note) {
    rest.push(...createNoteLines(state, message.note));
  }
  if (state.verbose && message.ancestors) {
    rest.push(...createAncestorsLines(state, message.ancestors));
  }
  return [row, ...rest]
}
function createRows(state, files) {
  const sortedFiles = [...files].sort(compareFile);
  const all = [];
  let index = -1;
  const rows = [];
  let lastWasMessage = false;
  while (++index < sortedFiles.length) {
    const file = sortedFiles[index];
    const messages = [...file.messages].sort(compareMessage);
    const messageRows = [];
    let offset = -1;
    while (++offset < messages.length) {
      const message = messages[offset];
      if (!state.silent || message.fatal) {
        all.push(message);
        messageRows.push(...createMessageLine(state, message));
      }
    }
    if ((!state.quiet && !state.silent) || messageRows.length > 0) {
      const line = createFileLine(state, file);
      if (lastWasMessage && line) rows.push('');
      if (line) rows.push(line);
      if (messageRows.length > 0) rows.push(...messageRows);
      lastWasMessage = messageRows.length > 0;
    }
  }
  const stats = statistics(all);
  if (stats.fatal || stats.warn) {
    rows.push('', createByline(state, stats));
  }
  return rows
}
function createUrlLines(state, url) {
  return [
    '  ' + state.bold + '[url]' + state.normalIntensity + ':',
    '    ' + url
  ]
}
function formatReason(state, reason) {
  const result = [];
  const splits = [];
  let index = reason.indexOf('`');
  while (index !== -1) {
    const split = {index, size: 1};
    splits.push(split);
    while (reason.codePointAt(index + 1) === 96) {
      split.size++;
      index++;
    }
    index = reason.indexOf('`', index + 1);
  }
  index = -1;
  let textStart = 0;
  while (++index < splits.length) {
    let closeIndex = index;
    let close;
    while (++closeIndex < splits.length) {
      if (splits[index].size === splits[closeIndex].size) {
        close = splits[closeIndex];
        break
      }
    }
    if (close) {
      const codeStart = splits[index].index;
      const codeEnd = close.index + close.size;
      result.push(
        reason.slice(textStart, codeStart) +
          state.cyan +
          reason.slice(codeStart, codeEnd) +
          state.defaultColor
      );
      textStart = codeEnd;
      index = closeIndex;
    }
  }
  result.push(reason.slice(textStart));
  return state.bold + result.join('') + state.normalIntensity
}
function fatalToLabel(value) {
  return value ? 'error' : value === false ? 'warning' : 'info'
}
function serializeRows(rows) {
  const sizes = [];
  let index = -1;
  while (++index < rows.length) {
    const row = rows[index];
    if (typeof row === 'string') ; else {
      let cellIndex = -1;
      while (++cellIndex < row.length) {
        const current = sizes[cellIndex] || 0;
        const size = stringWidth(row[cellIndex]);
        if (size > current) {
          sizes[cellIndex] = size;
        }
      }
    }
  }
  const lines = [];
  index = -1;
  while (++index < rows.length) {
    const row = rows[index];
    let line = '';
    if (typeof row === 'string') {
      line = row;
    } else {
      let cellIndex = -1;
      while (++cellIndex < row.length) {
        const cell = row[cellIndex] || '';
        const max = (sizes[cellIndex] || 0) + 1;
        line += cell + ' '.repeat(max - stringWidth(cell));
      }
    }
    lines.push(line.trimEnd());
  }
  return lines.join('\n')
}

const paths = process.argv.slice(2);
if (!paths.length) {
  console.error('Usage: lint-md.mjs <path> [<path> ...]');
  process.exit(1);
}
let format = false;
if (paths[0] === '--format') {
  paths.shift();
  format = true;
}
const linter = unified()
  .use(remarkParse)
  .use(remarkPresetLintNode)
  .use(remarkStringify);
paths.forEach(async (path) => {
  const file = await read(path);
  const fileContents = file.toString();
  const result = await linter.process(file);
  const isDifferent = fileContents !== result.toString();
  if (format) {
    if (isDifferent) {
      fs.writeFileSync(path, result.toString());
    }
  } else {
    if (isDifferent) {
      process.exitCode = 1;
      const cmd = process.platform === 'win32' ? 'vcbuild' : 'make';
      console.error(`${path} is not formatted. Please run '${cmd} format-md'.`);
    }
    if (result.messages.length) {
      process.exitCode = 1;
      console.error(reporter(result));
    }
  }
});
