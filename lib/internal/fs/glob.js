'use strict';

const {
  ArrayFrom,
  ArrayIsArray,
  ArrayPrototypeAt,
  ArrayPrototypeFlatMap,
  ArrayPrototypeMap,
  ArrayPrototypePop,
  ArrayPrototypePush,
  ArrayPrototypeSome,
  PromisePrototypeThen,
  SafeMap,
  SafeSet,
  StringPrototypeEndsWith,
} = primordials;

const { lstatSync, readdirSync } = require('fs');
const { lstat, readdir } = require('fs/promises');
const { join, resolve, basename, isAbsolute, dirname } = require('path');

const {
  kEmptyObject,
  isWindows,
  isMacOS,
} = require('internal/util');
const {
  validateObject,
  validateString,
  validateStringArray,
} = require('internal/validators');
const { DirentFromStats } = require('internal/fs/utils');
const {
  codes: {
    ERR_INVALID_ARG_TYPE,
  },
  hideStackFrames,
} = require('internal/errors');
const assert = require('internal/assert');

let minimatch;
function lazyMinimatch() {
  minimatch ??= require('internal/deps/minimatch/index');
  return minimatch;
}

/**
 * @param {string} path
 * @returns {Promise<DirentFromStats|null>}
 */
async function getDirent(path) {
  let stat;
  try {
    stat = await lstat(path);
  } catch {
    return null;
  }
  return new DirentFromStats(basename(path), stat, dirname(path));
}

/**
 * @param {string} path
 * @returns {DirentFromStats|null}
 */
function getDirentSync(path) {
  const stat = lstatSync(path, { throwIfNoEntry: false });
  if (stat === undefined) {
    return null;
  }
  return new DirentFromStats(basename(path), stat, dirname(path));
}

/**
 * @callback validateStringArrayOrFunction
 * @param {*} value
 * @param {string} name
 */
const validateStringArrayOrFunction = hideStackFrames((value, name) => {
  if (ArrayIsArray(value)) {
    for (let i = 0; i < value.length; ++i) {
      if (typeof value[i] !== 'string') {
        throw new ERR_INVALID_ARG_TYPE(`${name}[${i}]`, 'string', value[i]);
      }
    }
    return;
  }
  if (typeof value !== 'function') {
    throw new ERR_INVALID_ARG_TYPE(name, ['string[]', 'function'], value);
  }
});

/**
 * @param {string} pattern
 * @param {options} options
 * @returns {Minimatch}
 */
function createMatcher(pattern, options = kEmptyObject) {
  const opts = {
    __proto__: null,
    nocase: isWindows || isMacOS,
    windowsPathsNoEscape: true,
    nonegate: true,
    nocomment: true,
    optimizationLevel: 2,
    platform: process.platform,
    nocaseMagicOnly: true,
    ...options,
  };
  return new (lazyMinimatch().Minimatch)(pattern, opts);
}

class Cache {
  #cache = new SafeMap();
  #statsCache = new SafeMap();
  #readdirCache = new SafeMap();

  stat(path) {
    const cached = this.#statsCache.get(path);
    if (cached) {
      return cached;
    }
    const promise = getDirent(path);
    this.#statsCache.set(path, promise);
    return promise;
  }
  statSync(path) {
    const cached = this.#statsCache.get(path);
    if (cached) {
      return cached;
    }
    const val = getDirentSync(path);
    this.#statsCache.set(path, val);
    return val;
  }
  addToStatCache(path, val) {
    this.#statsCache.set(path, val);
  }
  async readdir(path) {
    const cached = this.#readdirCache.get(path);
    if (cached) {
      return cached;
    }
    const promise = PromisePrototypeThen(readdir(path, { __proto__: null, withFileTypes: true }), null, () => null);
    this.#readdirCache.set(path, promise);
    return promise;
  }
  readdirSync(path) {
    const cached = this.#readdirCache.get(path);
    if (cached) {
      return cached;
    }
    let val;
    try {
      val = readdirSync(path, { __proto__: null, withFileTypes: true });
    } catch {
      val = [];
    }
    this.#readdirCache.set(path, val);
    return val;
  }
  add(path, pattern) {
    let cache = this.#cache.get(path);
    if (!cache) {
      cache = new SafeSet();
      this.#cache.set(path, cache);
    }
    const originalSize = cache.size;
    pattern.indexes.forEach((index) => cache.add(pattern.cacheKey(index)));
    return cache.size !== originalSize + pattern.indexes.size;
  }
  seen(path, pattern, index) {
    return this.#cache.get(path)?.has(pattern.cacheKey(index));
  }
}

class Pattern {
  #pattern;
  #globStrings;
  indexes;
  symlinks;
  last;

  constructor(pattern, globStrings, indexes, symlinks) {
    this.#pattern = pattern;
    this.#globStrings = globStrings;
    this.indexes = indexes;
    this.symlinks = symlinks;
    this.last = pattern.length - 1;
  }

  isLast(isDirectory) {
    return this.indexes.has(this.last) ||
      (this.at(-1) === '' && isDirectory &&
      this.indexes.has(this.last - 1) && this.at(-2) === lazyMinimatch().GLOBSTAR);
  }
  isFirst() {
    return this.indexes.has(0);
  }
  get hasSeenSymlinks() {
    return ArrayPrototypeSome(ArrayFrom(this.indexes), (i) => !this.symlinks.has(i));
  }
  at(index) {
    return ArrayPrototypeAt(this.#pattern, index);
  }
  child(indexes, symlinks = new SafeSet()) {
    return new Pattern(this.#pattern, this.#globStrings, indexes, symlinks);
  }
  test(index, path) {
    if (index > this.#pattern.length) {
      return false;
    }
    const pattern = this.#pattern[index];
    if (pattern === lazyMinimatch().GLOBSTAR) {
      return true;
    }
    if (typeof pattern === 'string') {
      return pattern === path;
    }
    if (typeof pattern?.test === 'function') {
      return pattern.test(path);
    }
    return false;
  }

  cacheKey(index) {
    let key = '';
    for (let i = index; i < this.#globStrings.length; i++) {
      key += this.#globStrings[i];
      if (i !== this.#globStrings.length - 1) {
        key += '/';
      }
    }
    return key;
  }
}

class ResultSet extends SafeSet {
  #root = '.';
  #isExcluded = () => false;
  constructor(i) { super(i); } // eslint-disable-line no-useless-constructor

  setup(root, isExcludedFn) {
    this.#root = root;
    this.#isExcluded = isExcludedFn;
  }

  add(value) {
    if (this.#isExcluded(resolve(this.#root, value))) {
      return false;
    }
    super.add(value);
    return true;
  }
}

class Glob {
  #root;
  #exclude;
  #cache = new Cache();
  #results = new ResultSet();
  #queue = [];
  #subpatterns = new SafeMap();
  #patterns;
  #withFileTypes;
  #isExcluded = () => false;
  constructor(pattern, options = kEmptyObject) {
    validateObject(options, 'options');
    const { exclude, cwd, withFileTypes } = options;
    this.#root = cwd ?? '.';
    this.#withFileTypes = !!withFileTypes;
    if (exclude != null) {
      validateStringArrayOrFunction(exclude, 'options.exclude');
      if (ArrayIsArray(exclude)) {
        assert(typeof this.#root === 'string');
        // Convert the path part of exclude patterns to absolute paths for
        // consistent comparison before instantiating matchers.
        const matchers = exclude
          .map((pattern) => resolve(this.#root, pattern))
          .map((pattern) => createMatcher(pattern));
        this.#isExcluded = (value) =>
          matchers.some((matcher) => matcher.match(value));
        this.#results.setup(this.#root, this.#isExcluded);
      } else {
        this.#exclude = exclude;
      }
    }
    let patterns;
    if (typeof pattern === 'object') {
      validateStringArray(pattern, 'patterns');
      patterns = pattern;
    } else {
      validateString(pattern, 'patterns');
      patterns = [pattern];
    }
    this.matchers = ArrayPrototypeMap(patterns, (pattern) => createMatcher(pattern));
    this.#patterns = ArrayPrototypeFlatMap(this.matchers, (matcher) => ArrayPrototypeMap(matcher.set,
                                                                                         (pattern, i) => new Pattern(
                                                                                           pattern,
                                                                                           matcher.globParts[i],
                                                                                           new SafeSet().add(0),
                                                                                           new SafeSet(),
                                                                                         )));
  }

  globSync() {
    ArrayPrototypePush(this.#queue, { __proto__: null, path: '.', patterns: this.#patterns });
    while (this.#queue.length > 0) {
      const item = ArrayPrototypePop(this.#queue);
      for (let i = 0; i < item.patterns.length; i++) {
        this.#addSubpatterns(item.path, item.patterns[i]);
      }
      this.#subpatterns
        .forEach((patterns, path) => ArrayPrototypePush(this.#queue, { __proto__: null, path, patterns }));
      this.#subpatterns.clear();
    }
    return ArrayFrom(
      this.#results,
      this.#withFileTypes ? (path) => this.#cache.statSync(
        isAbsolute(path) ?
          path :
          join(this.#root, path),
      ) : undefined,
    );
  }
  #addSubpattern(path, pattern) {
    if (this.#isExcluded(path)) {
      return;
    }
    if (!this.#subpatterns.has(path)) {
      this.#subpatterns.set(path, [pattern]);
    } else {
      ArrayPrototypePush(this.#subpatterns.get(path), pattern);
    }
  }
  #addSubpatterns(path, pattern) {
    const seen = this.#cache.add(path, pattern);
    if (seen) {
      return;
    }
    const fullpath = resolve(this.#root, path);
    const stat = this.#cache.statSync(fullpath);
    const last = pattern.last;
    const isDirectory = stat?.isDirectory() || (stat?.isSymbolicLink() && pattern.hasSeenSymlinks);
    const isLast = pattern.isLast(isDirectory);
    const isFirst = pattern.isFirst();

    if (this.#isExcluded(fullpath)) {
      return;
    }
    if (isFirst && isWindows && typeof pattern.at(0) === 'string' && StringPrototypeEndsWith(pattern.at(0), ':')) {
      // Absolute path, go to root
      this.#addSubpattern(`${pattern.at(0)}\\`, pattern.child(new SafeSet().add(1)));
      return;
    }
    if (isFirst && pattern.at(0) === '') {
      // Absolute path, go to root
      this.#addSubpattern('/', pattern.child(new SafeSet().add(1)));
      return;
    }
    if (isFirst && pattern.at(0) === '..') {
      // Start with .., go to parent
      this.#addSubpattern('../', pattern.child(new SafeSet().add(1)));
      return;
    }
    if (isFirst && pattern.at(0) === '.') {
      // Start with ., proceed
      this.#addSubpattern('.', pattern.child(new SafeSet().add(1)));
      return;
    }

    if (isLast && typeof pattern.at(-1) === 'string') {
      // Add result if it exists
      const p = pattern.at(-1);
      const stat = this.#cache.statSync(join(fullpath, p));
      if (stat && (p || isDirectory)) {
        this.#results.add(join(path, p));
      }
      if (pattern.indexes.size === 1 && pattern.indexes.has(last)) {
        return;
      }
    } else if (isLast && pattern.at(-1) === lazyMinimatch().GLOBSTAR &&
     (path !== '.' || pattern.at(0) === '.' || (last === 0 && stat))) {
      // If pattern ends with **, add to results
      // if path is ".", add it only if pattern starts with "." or pattern is exactly "**"
      this.#results.add(path);
    }

    if (!isDirectory) {
      return;
    }

    let children;
    const firstPattern = pattern.indexes.size === 1 && pattern.at(pattern.indexes.values().next().value);
    if (typeof firstPattern === 'string') {
      const stat = this.#cache.statSync(join(fullpath, firstPattern));
      if (stat) {
        stat.name = firstPattern;
        children = [stat];
      } else {
        return;
      }
    } else {
      children = this.#cache.readdirSync(fullpath);
    }

    for (let i = 0; i < children.length; i++) {
      const entry = children[i];
      const entryPath = join(path, entry.name);
      this.#cache.addToStatCache(join(fullpath, entry.name), entry);

      const subPatterns = new SafeSet();
      const nSymlinks = new SafeSet();
      for (const index of pattern.indexes) {
        // For each child, check potential patterns
        if (this.#cache.seen(entryPath, pattern, index) || this.#cache.seen(entryPath, pattern, index + 1)) {
          return;
        }
        const current = pattern.at(index);
        const nextIndex = index + 1;
        const next = pattern.at(nextIndex);
        const fromSymlink = pattern.symlinks.has(index);

        if (current === lazyMinimatch().GLOBSTAR) {
          if (entry.name[0] === '.' || (this.#exclude && this.#exclude(this.#withFileTypes ? entry : entry.name))) {
            continue;
          }
          if (!fromSymlink && entry.isDirectory()) {
            // If directory, add ** to its potential patterns
            subPatterns.add(index);
          } else if (!fromSymlink && index === last) {
            // If ** is last, add to results
            this.#results.add(entryPath);
          }

          // Any pattern after ** is also a potential pattern
          // so we can already test it here
          const nextMatches = pattern.test(nextIndex, entry.name);
          if (nextMatches && nextIndex === last && !isLast) {
            // If next pattern is the last one, add to results
            this.#results.add(entryPath);
          } else if (nextMatches && entry.isDirectory()) {
            // Pattern matched, meaning two patterns forward
            // are also potential patterns
            // e.g **/b/c when entry is a/b - add c to potential patterns
            subPatterns.add(index + 2);
          }
          if ((nextMatches || pattern.at(0) === '.') &&
           (entry.isDirectory() || entry.isSymbolicLink()) && !fromSymlink) {
            // If pattern after ** matches, or pattern starts with "."
            // and entry is a directory or symlink, add to potential patterns
            subPatterns.add(nextIndex);
          }

          if (entry.isSymbolicLink()) {
            nSymlinks.add(index);
          }

          if (next === '..' && entry.isDirectory()) {
            // In case pattern is "**/..",
            // both parent and current directory should be added to the queue
            // if this is the last pattern, add to results instead
            const parent = join(path, '..');
            if (nextIndex < last) {
              if (!this.#subpatterns.has(path) && !this.#cache.seen(path, pattern, nextIndex + 1)) {
                this.#subpatterns.set(path, [pattern.child(new SafeSet().add(nextIndex + 1))]);
              }
              if (!this.#subpatterns.has(parent) && !this.#cache.seen(parent, pattern, nextIndex + 1)) {
                this.#subpatterns.set(parent, [pattern.child(new SafeSet().add(nextIndex + 1))]);
              }
            } else {
              if (!this.#cache.seen(path, pattern, nextIndex)) {
                this.#cache.add(path, pattern.child(new SafeSet().add(nextIndex)));
                this.#results.add(path);
              }
              if (!this.#cache.seen(path, pattern, nextIndex) || !this.#cache.seen(parent, pattern, nextIndex)) {
                this.#cache.add(parent, pattern.child(new SafeSet().add(nextIndex)));
                this.#results.add(parent);
              }
            }
          }
        }
        if (typeof current === 'string') {
          if (pattern.test(index, entry.name) && index !== last) {
            // If current pattern matches entry name
            // the next pattern is a potential pattern
            subPatterns.add(nextIndex);
          } else if (current === '.' && pattern.test(nextIndex, entry.name)) {
            // If current pattern is ".", proceed to test next pattern
            if (nextIndex === last) {
              this.#results.add(entryPath);
            } else {
              subPatterns.add(nextIndex + 1);
            }
          }
        }
        if (typeof current === 'object' && pattern.test(index, entry.name)) {
          // If current pattern is a regex that matches entry name (e.g *.js)
          // add next pattern to potential patterns, or to results if it's the last pattern
          if (index === last) {
            this.#results.add(entryPath);
          } else if (entry.isDirectory()) {
            subPatterns.add(nextIndex);
          }
        }
      }
      if (subPatterns.size > 0) {
        // If there are potential patterns, add to queue
        this.#addSubpattern(entryPath, pattern.child(subPatterns, nSymlinks));
      }
    }
  }


  async* glob() {
    ArrayPrototypePush(this.#queue, { __proto__: null, path: '.', patterns: this.#patterns });
    while (this.#queue.length > 0) {
      const item = ArrayPrototypePop(this.#queue);
      for (let i = 0; i < item.patterns.length; i++) {
        yield* this.#iterateSubpatterns(item.path, item.patterns[i]);
      }
      this.#subpatterns
        .forEach((patterns, path) => ArrayPrototypePush(this.#queue, { __proto__: null, path, patterns }));
      this.#subpatterns.clear();
    }
  }
  async* #iterateSubpatterns(path, pattern) {
    const seen = this.#cache.add(path, pattern);
    if (seen) {
      return;
    }
    const fullpath = resolve(this.#root, path);
    const stat = await this.#cache.stat(fullpath);
    const last = pattern.last;
    const isDirectory = stat?.isDirectory() || (stat?.isSymbolicLink() && pattern.hasSeenSymlinks);
    const isLast = pattern.isLast(isDirectory);
    const isFirst = pattern.isFirst();

    if (this.#isExcluded(fullpath)) {
      return;
    }
    if (isFirst && isWindows && typeof pattern.at(0) === 'string' && StringPrototypeEndsWith(pattern.at(0), ':')) {
      // Absolute path, go to root
      this.#addSubpattern(`${pattern.at(0)}\\`, pattern.child(new SafeSet().add(1)));
      return;
    }
    if (isFirst && pattern.at(0) === '') {
      // Absolute path, go to root
      this.#addSubpattern('/', pattern.child(new SafeSet().add(1)));
      return;
    }
    if (isFirst && pattern.at(0) === '..') {
      // Start with .., go to parent
      this.#addSubpattern('../', pattern.child(new SafeSet().add(1)));
      return;
    }
    if (isFirst && pattern.at(0) === '.') {
      // Start with ., proceed
      this.#addSubpattern('.', pattern.child(new SafeSet().add(1)));
      return;
    }

    if (isLast && typeof pattern.at(-1) === 'string') {
      // Add result if it exists
      const p = pattern.at(-1);
      const stat = await this.#cache.stat(join(fullpath, p));
      if (stat && (p || isDirectory)) {
        const result = join(path, p);
        if (!this.#results.has(result)) {
          if (this.#results.add(result)) {
            yield this.#withFileTypes ? stat : result;
          }
        }
      }
      if (pattern.indexes.size === 1 && pattern.indexes.has(last)) {
        return;
      }
    } else if (isLast && pattern.at(-1) === lazyMinimatch().GLOBSTAR &&
     (path !== '.' || pattern.at(0) === '.' || (last === 0 && stat))) {
      // If pattern ends with **, add to results
      // if path is ".", add it only if pattern starts with "." or pattern is exactly "**"
      if (!this.#results.has(path)) {
        if (this.#results.add(path)) {
          yield this.#withFileTypes ? stat : path;
        }
      }
    }

    if (!isDirectory) {
      return;
    }

    let children;
    const firstPattern = pattern.indexes.size === 1 && pattern.at(pattern.indexes.values().next().value);
    if (typeof firstPattern === 'string') {
      const stat = await this.#cache.stat(join(fullpath, firstPattern));
      if (stat) {
        stat.name = firstPattern;
        children = [stat];
      } else {
        return;
      }
    } else {
      children = await this.#cache.readdir(fullpath);
    }

    for (let i = 0; i < children.length; i++) {
      const entry = children[i];
      const entryPath = join(path, entry.name);
      this.#cache.addToStatCache(join(fullpath, entry.name), entry);

      const subPatterns = new SafeSet();
      const nSymlinks = new SafeSet();
      for (const index of pattern.indexes) {
        // For each child, check potential patterns
        if (this.#cache.seen(entryPath, pattern, index) || this.#cache.seen(entryPath, pattern, index + 1)) {
          return;
        }
        const current = pattern.at(index);
        const nextIndex = index + 1;
        const next = pattern.at(nextIndex);
        const fromSymlink = pattern.symlinks.has(index);

        if (current === lazyMinimatch().GLOBSTAR) {
          if (entry.name[0] === '.' || (this.#exclude && this.#exclude(this.#withFileTypes ? entry : entry.name))) {
            continue;
          }
          if (!fromSymlink && entry.isDirectory()) {
            // If directory, add ** to its potential patterns
            subPatterns.add(index);
          } else if (!fromSymlink && index === last) {
            // If ** is last, add to results
            if (!this.#results.has(entryPath)) {
              if (this.#results.add(entryPath)) {
                yield this.#withFileTypes ? entry : entryPath;
              }
            }
          }

          // Any pattern after ** is also a potential pattern
          // so we can already test it here
          const nextMatches = pattern.test(nextIndex, entry.name);
          if (nextMatches && nextIndex === last && !isLast) {
            // If next pattern is the last one, add to results
            if (!this.#results.has(entryPath)) {
              if (this.#results.add(entryPath)) {
                yield this.#withFileTypes ? entry : entryPath;
              }
            }
          } else if (nextMatches && entry.isDirectory()) {
            // Pattern matched, meaning two patterns forward
            // are also potential patterns
            // e.g **/b/c when entry is a/b - add c to potential patterns
            subPatterns.add(index + 2);
          }
          if ((nextMatches || pattern.at(0) === '.') &&
           (entry.isDirectory() || entry.isSymbolicLink()) && !fromSymlink) {
            // If pattern after ** matches, or pattern starts with "."
            // and entry is a directory or symlink, add to potential patterns
            subPatterns.add(nextIndex);
          }

          if (entry.isSymbolicLink()) {
            nSymlinks.add(index);
          }

          if (next === '..' && entry.isDirectory()) {
            // In case pattern is "**/..",
            // both parent and current directory should be added to the queue
            // if this is the last pattern, add to results instead
            const parent = join(path, '..');
            if (nextIndex < last) {
              if (!this.#subpatterns.has(path) && !this.#cache.seen(path, pattern, nextIndex + 1)) {
                this.#subpatterns.set(path, [pattern.child(new SafeSet().add(nextIndex + 1))]);
              }
              if (!this.#subpatterns.has(parent) && !this.#cache.seen(parent, pattern, nextIndex + 1)) {
                this.#subpatterns.set(parent, [pattern.child(new SafeSet().add(nextIndex + 1))]);
              }
            } else {
              if (!this.#cache.seen(path, pattern, nextIndex)) {
                this.#cache.add(path, pattern.child(new SafeSet().add(nextIndex)));
                if (!this.#results.has(path)) {
                  if (this.#results.add(path)) {
                    yield this.#withFileTypes ? this.#cache.statSync(fullpath) : path;
                  }
                }
              }
              if (!this.#cache.seen(path, pattern, nextIndex) || !this.#cache.seen(parent, pattern, nextIndex)) {
                this.#cache.add(parent, pattern.child(new SafeSet().add(nextIndex)));
                if (!this.#results.has(parent)) {
                  if (this.#results.add(parent)) {
                    yield this.#withFileTypes ? this.#cache.statSync(join(this.#root, parent)) : parent;
                  }
                }
              }
            }
          }
        }
        if (typeof current === 'string') {
          if (pattern.test(index, entry.name) && index !== last) {
            // If current pattern matches entry name
            // the next pattern is a potential pattern
            subPatterns.add(nextIndex);
          } else if (current === '.' && pattern.test(nextIndex, entry.name)) {
            // If current pattern is ".", proceed to test next pattern
            if (nextIndex === last) {
              if (!this.#results.has(entryPath)) {
                if (this.#results.add(entryPath)) {
                  yield this.#withFileTypes ? entry : entryPath;
                }
              }
            } else {
              subPatterns.add(nextIndex + 1);
            }
          }
        }
        if (typeof current === 'object' && pattern.test(index, entry.name)) {
          // If current pattern is a regex that matches entry name (e.g *.js)
          // add next pattern to potential patterns, or to results if it's the last pattern
          if (index === last) {
            if (!this.#results.has(entryPath)) {
              if (this.#results.add(entryPath)) {
                yield this.#withFileTypes ? entry : entryPath;
              }
            }
          } else if (entry.isDirectory()) {
            subPatterns.add(nextIndex);
          }
        }
      }
      if (subPatterns.size > 0) {
        // If there are potential patterns, add to queue
        this.#addSubpattern(entryPath, pattern.child(subPatterns, nSymlinks));
      }
    }
  }
}

/**
 * Check if a path matches a glob pattern
 * @param {string} path the path to check
 * @param {string} pattern the glob pattern to match
 * @param {boolean} windows whether the path is on a Windows system, defaults to `isWindows`
 * @returns {boolean}
 */
function matchGlobPattern(path, pattern, windows = isWindows) {
  validateString(path, 'path');
  validateString(pattern, 'pattern');
  return lazyMinimatch().minimatch(path, pattern, {
    kEmptyObject,
    nocase: isMacOS || isWindows,
    windowsPathsNoEscape: true,
    nonegate: true,
    nocomment: true,
    optimizationLevel: 2,
    platform: windows ? 'win32' : 'posix',
    nocaseMagicOnly: true,
  });
}

module.exports = {
  __proto__: null,
  Glob,
  matchGlobPattern,
};
