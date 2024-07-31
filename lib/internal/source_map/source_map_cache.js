'use strict';

const {
  ArrayPrototypePush,
  JSONParse,
  RegExpPrototypeExec,
  SafeMap,
  StringPrototypeCodePointAt,
  StringPrototypeSplit,
} = primordials;

// See https://sourcemaps.info/spec.html for SourceMap V3 specification.
const { Buffer } = require('buffer');
let debug = require('internal/util/debuglog').debuglog('source_map', (fn) => {
  debug = fn;
});

const { validateBoolean } = require('internal/validators');
const {
  setSourceMapsEnabled: setSourceMapsNative,
} = internalBinding('errors');
const {
  setInternalPrepareStackTrace,
} = require('internal/errors');
const { getLazy } = require('internal/util');

const getModuleSourceMapCache = getLazy(() => {
  const { SourceMapCacheMap } = require('internal/source_map/source_map_cache_map');
  return new SourceMapCacheMap();
});

// The generated source module/script instance is not accessible, so we can use
// a Map without memory concerns. Separate generated source entries with the module
// source entries to avoid overriding the module source entries with arbitrary
// source url magic comments.
const generatedSourceMapCache = new SafeMap();
const kLeadingProtocol = /^\w+:\/\//;
const kSourceMappingURLMagicComment = /\/[*/]#\s+sourceMappingURL=(?<sourceMappingURL>[^\s]+)/g;
const kSourceURLMagicComment = /\/[*/]#\s+sourceURL=(?<sourceURL>[^\s]+)/g;

const { isAbsolute } = require('path');
const { fileURLToPath, pathToFileURL, URL, URLParse } = require('internal/url');

let SourceMap;

// This is configured with --enable-source-maps during pre-execution.
let sourceMapsEnabled = false;
function getSourceMapsEnabled() {
  return sourceMapsEnabled;
}

/**
 * Enables or disables source maps programmatically.
 * @param {boolean} val
 */
function setSourceMapsEnabled(val) {
  validateBoolean(val, 'val');

  setSourceMapsNative(val);
  if (val) {
    const {
      prepareStackTraceWithSourceMaps,
    } = require('internal/source_map/prepare_stack_trace');
    setInternalPrepareStackTrace(prepareStackTraceWithSourceMaps);
  } else if (sourceMapsEnabled !== undefined) {
    // Reset prepare stack trace callback only when disabling source maps.
    const {
      defaultPrepareStackTrace,
    } = require('internal/errors');
    setInternalPrepareStackTrace(defaultPrepareStackTrace);
  }

  sourceMapsEnabled = val;
}

/**
 * Extracts the source url from the content if present. For example
 * //# sourceURL=file:///path/to/file
 *
 * Read more at: https://tc39.es/source-map-spec/#linking-evald-code-to-named-generated-code
 * @param {string} content - source content
 * @returns {string | null} source url or null if not present
 */
function extractSourceURLMagicComment(content) {
  let match;
  let matchSourceURL;
  // A while loop is used here to get the last occurrence of sourceURL.
  // This is needed so that we don't match sourceURL in string literals.
  while ((match = RegExpPrototypeExec(kSourceURLMagicComment, content))) {
    matchSourceURL = match;
  }
  if (matchSourceURL == null) {
    return null;
  }
  let sourceURL = matchSourceURL.groups.sourceURL;
  if (sourceURL != null && RegExpPrototypeExec(kLeadingProtocol, sourceURL) === null) {
    sourceURL = pathToFileURL(sourceURL).href;
  }
  return sourceURL;
}

/**
 * Extracts the source map url from the content if present. For example
 * //# sourceMappingURL=file:///path/to/file
 *
 * Read more at: https://tc39.es/source-map-spec/#linking-generated-code
 * @param {string} content - source content
 * @returns {string | null} source map url or null if not present
 */
function extractSourceMapURLMagicComment(content) {
  let match;
  let lastMatch;
  // A while loop is used here to get the last occurrence of sourceMappingURL.
  // This is needed so that we don't match sourceMappingURL in string literals.
  while ((match = RegExpPrototypeExec(kSourceMappingURLMagicComment, content))) {
    lastMatch = match;
  }
  if (lastMatch == null) {
    return null;
  }
  return lastMatch.groups.sourceMappingURL;
}

/**
 * Caches the source map if it is present in the content, with the given filename, moduleInstance, and sourceURL.
 * @param {string} filename - the actual filename
 * @param {string} content - the actual source content
 * @param {import('internal/modules/cjs/loader').Module | ModuleWrap} moduleInstance - a module instance that
 * associated with the source, once this is reclaimed, the source map entry will be removed from the cache
 * @param {boolean} isGeneratedSource - if the source was generated and evaluated with the global eval
 * @param {string | undefined} sourceURL - the source url
 * @param {string | undefined} sourceMapURL - the source map url
 */
function maybeCacheSourceMap(filename, content, moduleInstance, isGeneratedSource, sourceURL, sourceMapURL) {
  const sourceMapsEnabled = getSourceMapsEnabled();
  if (!(process.env.NODE_V8_COVERAGE || sourceMapsEnabled)) return;
  const { normalizeReferrerURL } = require('internal/modules/helpers');
  filename = normalizeReferrerURL(filename);
  if (filename === undefined) {
    // This is most likely an invalid filename in sourceURL of [eval]-wrapper.
    return;
  }

  if (sourceMapURL === undefined) {
    sourceMapURL = extractSourceMapURLMagicComment(content);
  }

  // Bail out when there is no source map url.
  if (typeof sourceMapURL !== 'string') {
    return;
  }

  // FIXME: callers should obtain sourceURL from v8 and pass it
  // rather than leaving it undefined and extract by regex.
  if (sourceURL === undefined) {
    sourceURL = extractSourceURLMagicComment(content);
  }

  const data = dataFromUrl(filename, sourceMapURL);
  const entry = {
    __proto__: null,
    lineLengths: lineLengths(content),
    data,
    // Save the source map url if it is not a data url.
    sourceMapURL: data ? null : sourceMapURL,
    sourceURL,
  };

  if (isGeneratedSource) {
    generatedSourceMapCache.set(filename, entry);
    if (sourceURL) {
      generatedSourceMapCache.set(sourceURL, entry);
    }
    return;
  }
  // If it is not a generated source, we assume we are in a "cjs/esm"
  // context.
  const keys = sourceURL ? [filename, sourceURL] : [filename];
  getModuleSourceMapCache().set(keys, entry, moduleInstance);
}

/**
 * Caches the source map if it is present in the eval'd source.
 * @param {string} content - the eval'd source code
 */
function maybeCacheGeneratedSourceMap(content) {
  const sourceMapsEnabled = getSourceMapsEnabled();
  if (!(process.env.NODE_V8_COVERAGE || sourceMapsEnabled)) return;

  const sourceURL = extractSourceURLMagicComment(content);
  if (sourceURL === null) {
    return;
  }
  try {
    maybeCacheSourceMap(sourceURL, content, null, true, sourceURL);
  } catch (err) {
    // This can happen if the filename is not a valid URL.
    // If we fail to cache the source map, we should not fail the whole process.
    debug(err);
  }
}

/**
 * Resolves source map payload data from the source url and source map url.
 * If the source map url is a data url, the data is returned.
 * Otherwise the source map url is resolved to a file path and the file is read.
 * @param {string} sourceURL - url of the source file
 * @param {string} sourceMappingURL - url of the source map
 * @returns {object} deserialized source map JSON object
 */
function dataFromUrl(sourceURL, sourceMappingURL) {
  const url = URLParse(sourceMappingURL);

  if (url != null) {
    switch (url.protocol) {
      case 'data:':
        return sourceMapFromDataUrl(sourceURL, url.pathname);
      default:
        debug(`unknown protocol ${url.protocol}`);
        return null;
    }
  }

  const mapURL = new URL(sourceMappingURL, sourceURL).href;
  return sourceMapFromFile(mapURL);
}

// Cache the length of each line in the file that a source map was extracted
// from. This allows translation from byte offset V8 coverage reports,
// to line/column offset Source Map V3.
function lineLengths(content) {
  const contentLength = content.length;
  const output = [];
  let lineLength = 0;
  for (let i = 0; i < contentLength; i++, lineLength++) {
    const codePoint = StringPrototypeCodePointAt(content, i);

    // We purposefully keep \r as part of the line-length calculation, in
    // cases where there is a \r\n separator, so that this can be taken into
    // account in coverage calculations.
    // codepoints for \n (new line), \u2028 (line separator) and \u2029 (paragraph separator)
    if (codePoint === 10 || codePoint === 0x2028 || codePoint === 0x2029) {
      ArrayPrototypePush(output, lineLength);
      lineLength = -1; // To not count the matched codePoint such as \n character
    }
  }
  ArrayPrototypePush(output, lineLength);
  return output;
}

/**
 * Read source map from file.
 * @param {string} mapURL - file url of the source map
 * @returns {object} deserialized source map JSON object
 */
function sourceMapFromFile(mapURL) {
  try {
    const fs = require('fs');
    const content = fs.readFileSync(fileURLToPath(mapURL), 'utf8');
    const data = JSONParse(content);
    return sourcesToAbsolute(mapURL, data);
  } catch (err) {
    debug(err);
    return null;
  }
}

// data:[<mediatype>][;base64],<data> see:
// https://tools.ietf.org/html/rfc2397#section-2
function sourceMapFromDataUrl(sourceURL, url) {
  const { 0: format, 1: data } = StringPrototypeSplit(url, ',');
  const splitFormat = StringPrototypeSplit(format, ';');
  const contentType = splitFormat[0];
  const base64 = splitFormat[splitFormat.length - 1] === 'base64';
  if (contentType === 'application/json') {
    const decodedData = base64 ?
      Buffer.from(data, 'base64').toString('utf8') : data;
    try {
      const parsedData = JSONParse(decodedData);
      return sourcesToAbsolute(sourceURL, parsedData);
    } catch (err) {
      debug(err);
      return null;
    }
  } else {
    debug(`unknown content-type ${contentType}`);
    return null;
  }
}

// If the sources are not absolute URLs after prepending of the "sourceRoot",
// the sources are resolved relative to the SourceMap (like resolving script
// src in a html document).
// If the sources are absolute paths, the sources are converted to absolute file URLs.
function sourcesToAbsolute(baseURL, data) {
  data.sources = data.sources.map((source) => {
    source = (data.sourceRoot || '') + source;
    if (isAbsolute(source)) {
      return pathToFileURL(source).href;
    }
    return new URL(source, baseURL).href;
  });
  // The sources array is now resolved to absolute URLs, sourceRoot should
  // be updated to noop.
  data.sourceRoot = '';
  return data;
}

// WARNING: The `sourceMapCacheToObject` runs during shutdown. In particular,
// it also runs when Workers are terminated, making it important that it does
// not call out to any user-provided code, including built-in prototypes that
// might have been tampered with.

// Get serialized representation of source-map cache, this is used
// to persist a cache of source-maps to disk when NODE_V8_COVERAGE is enabled.
function sourceMapCacheToObject() {
  const moduleSourceMapCache = getModuleSourceMapCache();
  if (moduleSourceMapCache.size === 0) {
    return undefined;
  }

  const obj = { __proto__: null };
  for (const { 0: k, 1: v } of moduleSourceMapCache) {
    obj[k] = {
      __proto__: null,
      lineLengths: v.lineLengths,
      data: v.data,
      url: v.sourceMapURL,
    };
  }
  return obj;
}

/**
 * Find a source map for a given actual source URL or path.
 * @param {string} sourceURL - actual source URL or path
 * @returns {import('internal/source_map/source_map').SourceMap | undefined} a source map or undefined if not found
 */
function findSourceMap(sourceURL) {
  if (RegExpPrototypeExec(kLeadingProtocol, sourceURL) === null) {
    sourceURL = pathToFileURL(sourceURL).href;
  }
  if (!SourceMap) {
    SourceMap = require('internal/source_map/source_map').SourceMap;
  }
  const entry = getModuleSourceMapCache().get(sourceURL) ?? generatedSourceMapCache.get(sourceURL);
  if (entry === undefined) {
    return undefined;
  }
  let sourceMap = entry.sourceMap;
  if (sourceMap === undefined) {
    sourceMap = new SourceMap(entry.data, { lineLengths: entry.lineLengths });
    entry.sourceMap = sourceMap;
  }
  return sourceMap;
}

module.exports = {
  findSourceMap,
  getSourceMapsEnabled,
  setSourceMapsEnabled,
  maybeCacheSourceMap,
  maybeCacheGeneratedSourceMap,
  sourceMapCacheToObject,
};
