'use strict';

const {
  ArrayPrototypeIndexOf,
  ArrayPrototypeJoin,
  ArrayPrototypeMap,
  ErrorPrototypeToString,
  RegExpPrototypeSymbolSplit,
  SafeStringIterator,
  StringPrototypeRepeat,
  StringPrototypeSlice,
  StringPrototypeStartsWith,
} = primordials;

let debug = require('internal/util/debuglog').debuglog('source_map', (fn) => {
  debug = fn;
});
const { getStringWidth } = require('internal/util/inspect');
const { readFileSync } = require('fs');
const { findSourceMap } = require('internal/source_map/source_map_cache');
const {
  kIsNodeError,
} = require('internal/errors');
const { fileURLToPath } = require('internal/url');
const { setGetSourceMapErrorSource } = internalBinding('errors');

const kStackLineAt = '\n    at ';

// Create a prettified stacktrace, inserting context from source maps
// if possible.
function prepareStackTraceWithSourceMaps(error, trace) {
  let errorString;
  if (kIsNodeError in error) {
    errorString = `${error.name} [${error.code}]: ${error.message}`;
  } else {
    errorString = ErrorPrototypeToString(error);
  }

  if (trace.length === 0) {
    return errorString;
  }

  let lastSourceMap;
  let lastFileName;
  const preparedTrace = ArrayPrototypeJoin(ArrayPrototypeMap(trace, (callSite, i) => {
    try {
      // A stack trace will often have several call sites in a row within the
      // same file, cache the source map and file content accordingly:
      let fileName = callSite.getFileName();
      if (fileName === undefined) {
        fileName = callSite.getEvalOrigin();
      }
      const sm = fileName === lastFileName ?
        lastSourceMap :
        findSourceMap(fileName);
      // Only when a source map is found, cache it for the next iteration.
      // This is a performance optimization to avoid interleaving with JS builtin function
      // invalidating the cache.
      // - at myFunc (file:///path/to/file.js:1:2)
      // - at Array.map (<anonymous>)
      // - at myFunc (file:///path/to/file.js:3:4)
      if (sm) {
        lastSourceMap = sm;
        lastFileName = fileName;
        return `${kStackLineAt}${serializeJSStackFrame(sm, callSite, trace[i + 1])}`;
      }
    } catch (err) {
      debug(err);
    }
    return `${kStackLineAt}${callSite}`;
  }), '');
  return `${errorString}${preparedTrace}`;
}

/**
 * Serialize a single call site in the stack trace.
 * Refer to SerializeJSStackFrame in deps/v8/src/objects/call-site-info.cc for
 * more details about the default ToString(CallSite).
 * The CallSite API is documented at https://v8.dev/docs/stack-trace-api.
 * @param {import('internal/source_map/source_map').SourceMap} sm
 * @param {CallSite} callSite - the CallSite object to be serialized
 * @param {CallSite} callerCallSite - caller site info
 * @returns {string} - the serialized call site
 */
function serializeJSStackFrame(sm, callSite, callerCallSite) {
  // Source Map V3 lines/columns start at 0/0 whereas stack traces
  // start at 1/1:
  const {
    originalLine,
    originalColumn,
    originalSource,
  } = sm.findEntry(callSite.getLineNumber() - 1, callSite.getColumnNumber() - 1);
  if (originalSource === undefined || originalLine === undefined ||
      originalColumn === undefined) {
    return `${callSite}`;
  }
  const name = getOriginalSymbolName(sm, callSite, callerCallSite);
  const originalSourceNoScheme =
    StringPrototypeStartsWith(originalSource, 'file://') ?
      fileURLToPath(originalSource) : originalSource;
  // Construct call site name based on: v8.dev/docs/stack-trace-api:
  const fnName = callSite.getFunctionName() ?? callSite.getMethodName();

  let prefix = '';
  if (callSite.isAsync()) {
    // Promise aggregation operation frame has no locations. This must be an
    // async stack frame.
    prefix = 'async ';
  } else if (callSite.isConstructor()) {
    prefix = 'new ';
  }

  const typeName = callSite.getTypeName();
  const namePrefix = typeName !== null && typeName !== 'global' ? `${typeName}.` : '';
  const originalName = `${namePrefix}${fnName || '<anonymous>'}`;
  // The original call site may have a different symbol name
  // associated with it, use it:
  const mappedName = (name && name !== originalName) ?
    `${name}` :
    `${originalName}`;
  const hasName = !!(name || originalName);
  // Replace the transpiled call site with the original:
  return `${prefix}${mappedName}${hasName ? ' (' : ''}` +
    `${originalSourceNoScheme}:${originalLine + 1}:` +
    `${originalColumn + 1}${hasName ? ')' : ''}`;
}

// Transpilers may have removed the original symbol name used in the stack
// trace, if possible restore it from the names field of the source map:
function getOriginalSymbolName(sourceMap, callSite, callerCallSite) {
  // First check for a symbol name associated with the enclosing function:
  const enclosingEntry = sourceMap.findEntry(
    callSite.getEnclosingLineNumber() - 1,
    callSite.getEnclosingColumnNumber() - 1,
  );
  if (enclosingEntry.name) return enclosingEntry.name;
  // Fallback to using the symbol name attached to the caller site:
  const currentFileName = callSite.getFileName();
  if (callerCallSite && currentFileName === callerCallSite.getFileName()) {
    const { name } = sourceMap.findEntry(
      callerCallSite.getLineNumber() - 1,
      callerCallSite.getColumnNumber() - 1,
    );
    return name;
  }
}

/**
 * Return a snippet of code from where the exception was originally thrown
 * above the stack trace. This called from GetErrorSource in node_errors.cc.
 * @param {import('internal/source_map/source_map').SourceMap} sourceMap - the source map to be used
 * @param {string} originalSourcePath - path or url of the original source
 * @param {number} originalLine - line number in the original source
 * @param {number} originalColumn - column number in the original source
 * @returns {string | undefined} - the exact line in the source content or undefined if file not found
 */
function getErrorSource(
  sourceMap,
  originalSourcePath,
  originalLine,
  originalColumn,
) {
  const originalSourcePathNoScheme =
    StringPrototypeStartsWith(originalSourcePath, 'file://') ?
      fileURLToPath(originalSourcePath) : originalSourcePath;
  const source = getOriginalSource(
    sourceMap.payload,
    originalSourcePath,
  );
  if (typeof source !== 'string') {
    return;
  }
  const lines = RegExpPrototypeSymbolSplit(/\r?\n/, source, originalLine + 1);
  const line = lines[originalLine];
  if (!line) {
    return;
  }

  // Display ^ in appropriate position, regardless of whether tabs or
  // spaces are used:
  let prefix = '';
  for (const character of new SafeStringIterator(
    StringPrototypeSlice(line, 0, originalColumn + 1))) {
    prefix += character === '\t' ? '\t' :
      StringPrototypeRepeat(' ', getStringWidth(character));
  }
  prefix = StringPrototypeSlice(prefix, 0, -1); // The last character is '^'.

  const exceptionLine =
   `${originalSourcePathNoScheme}:${originalLine + 1}\n${line}\n${prefix}^\n\n`;
  return exceptionLine;
}

/**
 * Retrieve the original source code from the source map's `sources` list or disk.
 * @param {import('internal/source_map/source_map').SourceMap.payload} payload
 * @param {string} originalSourcePath - path or url of the original source
 * @returns {string | undefined} - the source content or undefined if file not found
 */
function getOriginalSource(payload, originalSourcePath) {
  let source;
  // payload.sources has been normalized to be an array of absolute urls.
  const sourceContentIndex =
    ArrayPrototypeIndexOf(payload.sources, originalSourcePath);
  if (payload.sourcesContent?.[sourceContentIndex]) {
    // First we check if the original source content was provided in the
    // source map itself:
    source = payload.sourcesContent[sourceContentIndex];
  } else if (StringPrototypeStartsWith(originalSourcePath, 'file://')) {
    // If no sourcesContent was found, attempt to load the original source
    // from disk:
    debug(`read source of ${originalSourcePath} from filesystem`);
    const originalSourcePathNoScheme = fileURLToPath(originalSourcePath);
    try {
      source = readFileSync(originalSourcePathNoScheme, 'utf8');
    } catch (err) {
      debug(err);
    }
  }
  return source;
}

/**
 * Retrieve exact line in the original source code from the source map's `sources` list or disk.
 * @param {string} fileName - actual file name
 * @param {number} lineNumber - actual line number
 * @param {number} columnNumber - actual column number
 * @returns {string | undefined} - the source content or undefined if file not found
 */
function getSourceMapErrorSource(fileName, lineNumber, columnNumber) {
  const sm = findSourceMap(fileName);
  if (sm === undefined) {
    return;
  }
  const {
    originalLine,
    originalColumn,
    originalSource,
  } = sm.findEntry(lineNumber - 1, columnNumber);
  const errorSource = getErrorSource(sm, originalSource, originalLine, originalColumn);
  return errorSource;
}

setGetSourceMapErrorSource(getSourceMapErrorSource);

module.exports = {
  prepareStackTraceWithSourceMaps,
};
