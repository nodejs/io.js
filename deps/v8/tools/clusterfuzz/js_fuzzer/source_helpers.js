// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Source loader.
 */

const assert = require('assert');
const fs = require('fs');
const fsPath = require('path');

const { EOL } = require('os');

const babelGenerator = require('@babel/generator').default;
const babelTraverse = require('@babel/traverse').default;
const babelTypes = require('@babel/types');
const babylon = require('@babel/parser');

const exceptions = require('./exceptions.js');

const SCRIPT = Symbol('SCRIPT');
const MODULE = Symbol('MODULE');

const V8_BUILTIN_PREFIX = '__V8Builtin';
const V8_REPLACE_BUILTIN_REGEXP = new RegExp(
    V8_BUILTIN_PREFIX + '(\\w+)\\(', 'g');

const V8_MJSUNIT_REL = fsPath.join('test', 'mjsunit');
const V8_MJSUNIT_HARNESS_REL = fsPath.join(V8_MJSUNIT_REL, 'mjsunit.js');

const BABYLON_OPTIONS = {
    sourceType: 'script',
    allowReturnOutsideFunction: true,
    tokens: false,
    ranges: false,
    plugins: [
        'asyncGenerators',
        'bigInt',
        'classPrivateMethods',
        'classPrivateProperties',
        'classProperties',
        'doExpressions',
        'exportDefaultFrom',
        'nullishCoalescingOperator',
        'numericSeparator',
        'objectRestSpread',
        'optionalCatchBinding',
        'optionalChaining',
    ],
}

const BABYLON_REPLACE_VAR_OPTIONS = Object.assign({}, BABYLON_OPTIONS);
BABYLON_REPLACE_VAR_OPTIONS['placeholderPattern'] = /^VAR_[0-9]+$/;

function _idEquals(exp, name) {
  return babelTypes.isIdentifier(exp) && exp.name == name;
}

function _isV8NewAPIExecute(exp) {
  // exp is a member expression resolving to 'd8.file.execute'
  return (_hasMemberProperty(exp, 'execute') &&
          _hasMemberProperty(exp.object, 'file') &&
          _idEquals(exp.object.object, 'd8'));
}

function _hasMemberProperty(exp, name) {
  // 'exp' is a member expression with property <name>
  return babelTypes.isMemberExpression(exp) && _idEquals(exp.property, name);
}

function _isV8OrSpiderMonkeyLoad(path) {
  // 'load' and 'loadRelativeToScript' used by V8's old API and SpiderMonkey.
  // 'd8.file.execute' used by V8's new API.
  return ((_idEquals(path.node.callee, 'load') ||
           _idEquals(path.node.callee, 'loadRelativeToScript') ||
           _isV8NewAPIExecute(path.node.callee)) &&
          path.node.arguments.length == 1 &&
          babelTypes.isStringLiteral(path.node.arguments[0]));
}

function _isChakraLoad(path) {
  // 'WScript.LoadScriptFile' used by Chakra.
  // TODO(ochang): The optional second argument can change semantics ("self",
  // "samethread", "crossthread" etc).
  // Investigate whether if it still makes sense to include them.
  return (babelTypes.isMemberExpression(path.node.callee) &&
          babelTypes.isIdentifier(path.node.callee.property) &&
          path.node.callee.property.name == 'LoadScriptFile' &&
          path.node.arguments.length >= 1 &&
          babelTypes.isStringLiteral(path.node.arguments[0]));
}

function _findPath(path, caseSensitive=true) {
  // If the path exists, return the path. Otherwise return null. Used to handle
  // case insensitive matches for Chakra tests.
  if (caseSensitive) {
    return fs.existsSync(path) ? path : null;
  }

  path = fsPath.normalize(fsPath.resolve(path));
  const pathComponents = path.split(fsPath.sep);
  let realPath = fsPath.resolve(fsPath.sep);

  for (let i = 1; i < pathComponents.length; i++) {
    // For each path component, do a directory listing to see if there is a case
    // insensitive match.
    const curListing = fs.readdirSync(realPath);
    let realComponent = null;
    for (const component of curListing) {
      if (i < pathComponents.length - 1 &&
          !fs.statSync(fsPath.join(realPath, component)).isDirectory()) {
        continue;
      }

      if (component.toLowerCase() == pathComponents[i].toLowerCase()) {
        realComponent = component;
        break;
      }
    }

    if (!realComponent) {
      return null;
    }

    realPath = fsPath.join(realPath, realComponent);
  }

  return realPath;
}

function _findDependentCodePath(filePath, baseDirectory, caseSensitive=true) {
  while (fsPath.dirname(baseDirectory) != baseDirectory) {
    // Walk up the directory tree.
    const testPath = fsPath.join(baseDirectory, filePath);
    const realPath = _findPath(testPath, caseSensitive)
    if (realPath) {
      return realPath;
    }

    baseDirectory = fsPath.dirname(baseDirectory);
  }

  return null;
}

/**
 * Resolves dependencies calculated from the sources and returns their string
 * values in an array.
 *
 * Removes V8/Spidermonkey/Chakra load expressions in a source AST. Calculates
 * if a Sandbox stub is needed.
 *
 * @param {string} originalFilePath Absolute path to file.
 * @param {AST} ast Babel AST of the sources.
 */
function resolveDependencies(originalFilePath, ast) {
  const dependencies = new Set();

  babelTraverse(ast, {
    CallExpression(path) {
      const isV8OrSpiderMonkeyLoad = _isV8OrSpiderMonkeyLoad(path);
      const isChakraLoad = _isChakraLoad(path);
      if (!isV8OrSpiderMonkeyLoad && !isChakraLoad) {
        return;
      }

      let loadValue = path.node.arguments[0].extra.rawValue;

      // Remove load call.
      path.remove();

      // Normalize Windows path separators.
      loadValue = loadValue.replace(/\\/g, fsPath.sep);

      // Ignore the mjsunit harness file as we already include an altered
      // version for fuzzing.
      if (loadValue == V8_MJSUNIT_HARNESS_REL) {
        return;
      }

      // Locate mjsunit dependencies relative to the V8 corpus. Like that we
      // resolve to a valid file even if the test file is from fuzzilli or
      // crash tests.
      if (loadValue.startsWith(V8_MJSUNIT_REL)) {
        loadValue = fsPath.join('v8', loadValue);
      }

      const resolvedPath = _findDependentCodePath(
          loadValue, fsPath.dirname(originalFilePath), !isChakraLoad);
      if (!resolvedPath) {
        console.log('ERROR: Could not find dependent path for', loadValue);
        return;
      }

      if (exceptions.isTestSkippedAbs(resolvedPath)) {
        // Dependency is skipped.
        return;
      }

      // Add the dependency path.
      dependencies.add(resolvedPath);
    },

    MemberExpression(path) {
      const object = path.node.object;
      if (babelTypes.isIdentifier(object) && object.name == 'Sandbox') {
        dependencies.add(
            fsPath.join(__dirname, 'resources', 'sandbox.js'));
      }
    }
  });
  return Array.from(dependencies);
}

function isStrictDirective(directive) {
  return (directive.value &&
          babelTypes.isDirectiveLiteral(directive.value) &&
          directive.value.value === 'use strict');
}

function replaceV8Builtins(code) {
  return code.replace(/%(\w+)\(/g, V8_BUILTIN_PREFIX + '$1(');
}

function restoreV8Builtins(code) {
  return code.replace(V8_REPLACE_BUILTIN_REGEXP, '%$1(');
}

function maybeUseStict(code, useStrict) {
  if (useStrict) {
    return `'use strict';${EOL}${EOL}${code}`;
  }
  return code;
}

class BaseCorpus {
  constructor(inputDir) {
    assert(fsPath.dirname(inputDir) != inputDir,
           `Require an absolute, non-root path to corpus. Got ${inputDir}`)
    this.inputDir = inputDir;
  }

  /**
   * If specified, sources from this corpus will use this as universal
   * label annotating the code in differential fuzzing.
   */
  get diffFuzzLabel() {
    return undefined;
  }

  /**
   * Load flags from meta data specific to a particular test case.
   */
  loadFlags(relPath, data) {
    return [];
  }
}

const BASE_CORPUS = new BaseCorpus(__dirname);

class Source {
  constructor(corpus, relPath, flags, dependentPaths) {
    this.corpus = corpus;
    this.relPath = relPath;
    this.flags = flags;
    this.dependentPaths = dependentPaths;
    this.sloppy = exceptions.isTestSloppyRel(relPath);
  }

  get absPath() {
    return fsPath.join(this.corpus.inputDir, this.relPath);
  }

  /**
   * Specifies the path used to label sources in differential fuzzing.
   */
  get diffFuzzPath() {
    return this.corpus.diffFuzzLabel || this.relPath;
  }

  /**
   * Specifies if the source isn't compatible with strict mode.
   */
  isSloppy() {
    return this.sloppy;
  }

  /**
   * Specifies if the source has a top-level 'use strict' directive.
   */
  isStrict() {
    throw Error('Not implemented');
  }

  /**
   * Generates the code as a string without any top-level 'use strict'
   * directives. V8 natives that were replaced before parsing are restored.
   */
  generateNoStrict() {
    throw Error('Not implemented');
  }

  /**
   * Recursively adds dependencies of a this source file.
   *
   * @param {Map} dependencies Dependency map to which to add new, parsed
   *     dependencies unless they are already in the map.
   * @param {Map} visitedDependencies A set for avoiding loops.
   */
  loadDependencies(dependencies, visitedDependencies) {
    visitedDependencies = visitedDependencies || new Set();

    for (const absPath of this.dependentPaths) {
      if (dependencies.has(absPath) ||
          visitedDependencies.has(absPath)) {
        // Already added.
        continue;
      }

      // Prevent infinite loops.
      visitedDependencies.add(absPath);

      // Recursively load dependencies.
      const dependency = loadDependencyAbs(this.corpus, absPath);
      dependency.loadDependencies(dependencies, visitedDependencies);

      // Add the dependency.
      dependencies.set(absPath, dependency);
    }
  }
}

/**
 * Represents sources whose AST can be manipulated.
 */
class ParsedSource extends Source {
  constructor(ast, corpus, relPath, flags, dependentPaths) {
    super(corpus, relPath, flags, dependentPaths);
    this.ast = ast;
  }

  isStrict() {
    return !!this.ast.program.directives.filter(isStrictDirective).length;
  }

  generateNoStrict() {
    const allDirectives = this.ast.program.directives;
    this.ast.program.directives = this.ast.program.directives.filter(
        directive => !isStrictDirective(directive));
    try {
      const code = babelGenerator(this.ast.program, {comments: true}).code;
      return restoreV8Builtins(code);
    } finally {
      this.ast.program.directives = allDirectives;
    }
  }
}

/**
 * Represents sources with cached code.
 */
class CachedSource extends Source {
  constructor(source) {
    super(source.corpus, source.relPath, source.flags, source.dependentPaths);
    this.use_strict = source.isStrict();
    this.code = source.generateNoStrict();
  }

  isStrict() {
    return this.use_strict;
  }

  generateNoStrict() {
    return this.code;
  }
}

/**
 * Read file path into an AST.
 *
 * Post-processes the AST by replacing V8 natives and removing disallowed
 * natives, as well as removing load expressions and adding the paths-to-load
 * as meta data.
 */
function loadSource(corpus, relPath, parseStrict=false) {
  const absPath = fsPath.resolve(fsPath.join(corpus.inputDir, relPath));
  const data = fs.readFileSync(absPath, 'utf-8');

  if (guessType(data) !== SCRIPT) {
    return null;
  }

  const preprocessed = maybeUseStict(replaceV8Builtins(data), parseStrict);
  const ast = babylon.parse(preprocessed, BABYLON_OPTIONS);

  removeComments(ast);
  cleanAsserts(ast);
  annotateWithOriginalPath(ast, relPath);

  const flags = corpus.loadFlags(relPath, data);
  const dependentPaths = resolveDependencies(absPath, ast);

  return new ParsedSource(ast, corpus, relPath, flags, dependentPaths);
}

function guessType(data) {
  if (data.includes('// MODULE')) {
    return MODULE;
  }

  return SCRIPT;
}

/**
 * Remove existing comments.
 */
function removeComments(ast) {
  babelTraverse(ast, {
    enter(path) {
      babelTypes.removeComments(path.node);
    }
  });
}

/**
 * Removes "Assert" from strings in spidermonkey shells or from older
 * crash tests: https://crbug.com/1068268
 */
function cleanAsserts(ast) {
  function replace(string) {
    return string == null ? null : string.replace(/[Aa]ssert/g, '*****t');
  }
  babelTraverse(ast, {
    StringLiteral(path) {
      path.node.value = replace(path.node.value);
      path.node.extra.raw = replace(path.node.extra.raw);
      path.node.extra.rawValue = replace(path.node.extra.rawValue);
    },
    TemplateElement(path) {
      path.node.value.cooked = replace(path.node.value.cooked);
      path.node.value.raw = replace(path.node.value.raw);
    },
  });
}

/**
 * Annotate code with top-level comment.
 */
function annotateWithComment(ast, comment) {
  if (ast.program && ast.program.body && ast.program.body.length > 0) {
    babelTypes.addComment(
        ast.program.body[0], 'leading', comment, true);
  }
}

/**
 * Annotate code with original file path.
 */
function annotateWithOriginalPath(ast, relPath) {
  annotateWithComment(ast, ' Original: ' + relPath);
}

const dependencyCache = new Map();

function loadDependency(corpus, relPath) {
  const absPath = fsPath.join(corpus.inputDir, relPath);
  let dependency = dependencyCache.get(absPath);
  if (!dependency) {
    const source = loadSource(corpus, relPath);
    dependency = new CachedSource(source);
    dependencyCache.set(absPath, dependency);
  }
  return dependency;
}

function loadDependencyAbs(corpus, absPath) {
  return loadDependency(corpus, fsPath.relative(corpus.inputDir, absPath));
}

// Convenience helper to load a file from the resources directory.
function loadResource(fileName) {
  return loadDependency(BASE_CORPUS, fsPath.join('resources', fileName));
}

function generateCode(source, dependencies=[]) {
  const allSources = dependencies.concat([source]);
  const codePieces = allSources.map(
      source => source.generateNoStrict());

  if (allSources.some(source => source.isStrict()) &&
      !allSources.some(source => source.isSloppy())) {
    codePieces.unshift('\'use strict\';');
  }

  return codePieces.join(EOL + EOL);
}

module.exports = {
  BABYLON_OPTIONS: BABYLON_OPTIONS,
  BABYLON_REPLACE_VAR_OPTIONS: BABYLON_REPLACE_VAR_OPTIONS,
  BASE_CORPUS: BASE_CORPUS,
  annotateWithComment: annotateWithComment,
  BaseCorpus: BaseCorpus,
  generateCode: generateCode,
  loadDependencyAbs: loadDependencyAbs,
  loadResource: loadResource,
  loadSource: loadSource,
  ParsedSource: ParsedSource,
}
