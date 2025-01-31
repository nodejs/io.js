'use strict';

const {
  ObjectPrototypeHasOwnProperty,
  RegExpPrototypeExec,
  SafeSet,
  StringPrototypeCharCodeAt,
  StringPrototypeIncludes,
  StringPrototypeSlice,
} = primordials;
const { getOptionValue } = require('internal/options');
const {
  extensionFormatMap,
  getFormatOfExtensionlessFile,
  mimeToFormat,
} = require('internal/modules/esm/formats');

const detectModule = getOptionValue('--experimental-detect-module');
const { containsModuleSyntax } = internalBinding('contextify');
const { getPackageScopeConfig, getPackageType } = require('internal/modules/package_json_reader');
const { fileURLToPath } = require('internal/url');
const { ERR_UNKNOWN_FILE_EXTENSION } = require('internal/errors').codes;

const protocolHandlers = {
  '__proto__': null,
  'data:': getDataProtocolModuleFormat,
  'file:': getFileProtocolModuleFormat,
  'node:'() { return 'builtin'; },
};

/**
 * Determine whether the given ambiguous source contains CommonJS or ES module syntax.
 * @param {string | Buffer | undefined} source
 * @param {URL} url
 */
function detectModuleFormat(source, url) {
  if (!source) { return detectModule ? null : 'commonjs'; }
  if (!detectModule) { return 'commonjs'; }
  return containsModuleSyntax(`${source}`, fileURLToPath(url), url) ? 'module' : 'commonjs';
}

/**
 * @param {URL} parsed
 * @returns {string | null}
 */
function getDataProtocolModuleFormat(parsed) {
  const { 1: mime } = RegExpPrototypeExec(
    /^([^/]+\/[^;,]+)(?:[^,]*?)(;base64)?,/,
    parsed.pathname,
  ) || [ null, null, null ];

  return mimeToFormat(mime);
}

const DOT_CODE = 46;
const SLASH_CODE = 47;

/**
 * Returns the file extension from a URL. Should give similar result to
 * `require('node:path').extname(require('node:url').fileURLToPath(url))`
 * when used with a `file:` URL.
 * @param {URL} url
 * @returns {string}
 */
function extname(url) {
  const { pathname } = url;
  for (let i = pathname.length - 1; i > 0; i--) {
    switch (StringPrototypeCharCodeAt(pathname, i)) {
      case SLASH_CODE:
        return '';

      case DOT_CODE:
        return StringPrototypeCharCodeAt(pathname, i - 1) === SLASH_CODE ? '' : StringPrototypeSlice(pathname, i);
    }
  }
  return '';
}

/**
 * Determine whether the given file URL is under a `node_modules` folder.
 * This function assumes that the input has already been verified to be a `file:` URL,
 * and is a file rather than a folder.
 * @param {URL} url
 */
function underNodeModules(url) {
  if (url.protocol !== 'file:') { return false; } // We determine module types for other protocols based on MIME header

  return StringPrototypeIncludes(url.pathname, '/node_modules/');
}

let typelessPackageJsonFilesWarnedAbout;
function warnTypelessPackageJsonFile(pjsonPath, url) {
  typelessPackageJsonFilesWarnedAbout ??= new SafeSet();
  if (!underNodeModules(url) && !typelessPackageJsonFilesWarnedAbout.has(pjsonPath)) {
    const warning = `Module type of ${url} is not specified and it doesn't parse as CommonJS.\n` +
      'Reparsing as ES module because module syntax was detected. This incurs a performance overhead.\n' +
      `To eliminate this warning, add "type": "module" to ${pjsonPath}.`;
    process.emitWarning(warning, {
      code: 'MODULE_TYPELESS_PACKAGE_JSON',
    });
    typelessPackageJsonFilesWarnedAbout.add(pjsonPath);
  }
}

/**
 * @param {URL} url
 * @param {{parentURL: string; source?: Buffer}} context
 * @param {boolean} ignoreErrors
 * @returns {string}
 */
function getFileProtocolModuleFormat(url, context = { __proto__: null }, ignoreErrors) {
  const { source } = context;
  const ext = extname(url);

  if (ext === '.js') {
    const { type: packageType, pjsonPath, exists: foundPackageJson } = getPackageScopeConfig(url);
    if (packageType !== 'none') {
      return packageType;
    }

    // The controlling `package.json` file has no `type` field.
    // `source` is undefined when this is called from `defaultResolve`;
    // but this gets called again from `defaultLoad`/`defaultLoadSync`.
    // For ambiguous files (.js, no type field) we return undefined from `resolve` and re-run the check in `load`.
    const format = detectModuleFormat(source, url);
    if (format === 'module' && foundPackageJson) {
      // This module has a .js extension, a package.json with no `type` field, and ESM syntax.
      // Warn about the missing `type` field so that the user can avoid the performance penalty of detection.
      warnTypelessPackageJsonFile(pjsonPath, url);
    }
    return format;
  }
  if (ext === '.ts' && getOptionValue('--experimental-strip-types')) {
    const { type: packageType, pjsonPath, exists: foundPackageJson } = getPackageScopeConfig(url);
    if (packageType !== 'none') {
      return `${packageType}-typescript`;
    }
    // The controlling `package.json` file has no `type` field.
    // `source` is undefined when this is called from `defaultResolve`;
    // but this gets called again from `defaultLoad`/`defaultLoadSync`.
    // Since experimental-strip-types depends on detect-module, we always return null if source is undefined.
    if (!source) { return null; }
    const { stringify } = require('internal/modules/helpers');
    const { stripTypeScriptModuleTypes } = require('internal/modules/typescript');
    const stringifiedSource = stringify(source);
    const parsedSource = stripTypeScriptModuleTypes(stringifiedSource, fileURLToPath(url));
    const detectedFormat = detectModuleFormat(parsedSource, url);
    const format = `${detectedFormat}-typescript`;
    if (format === 'module-typescript' && foundPackageJson) {
      // This module has a .js extension, a package.json with no `type` field, and ESM syntax.
      // Warn about the missing `type` field so that the user can avoid the performance penalty of detection.
      warnTypelessPackageJsonFile(pjsonPath, url);
    }
    return format;
  }

  if (ext === '') {
    const packageType = getPackageType(url);
    if (packageType === 'module') {
      return getFormatOfExtensionlessFile(url);
    }
    if (packageType !== 'none') {
      return packageType; // 'commonjs' or future package types
    }

    // The controlling `package.json` file has no `type` field.
    if (!source) {
      return null;
    }
    const format = getFormatOfExtensionlessFile(url);
    if (format === 'wasm') {
      return format;
    }
    return detectModuleFormat(source, url);
  }

  const format = extensionFormatMap[ext];
  if (format) { return format; }

  // Explicit undefined return indicates load hook should rerun format check
  if (ignoreErrors) { return undefined; }
  const filepath = fileURLToPath(url);
  throw new ERR_UNKNOWN_FILE_EXTENSION(ext, filepath);
}

/**
 * @param {URL} url
 * @param {{parentURL: string}} context
 * @returns {Promise<string> | string | undefined} only works when enabled
 */
function defaultGetFormatWithoutErrors(url, context) {
  const protocol = url.protocol;
  if (!ObjectPrototypeHasOwnProperty(protocolHandlers, protocol)) {
    return null;
  }
  return protocolHandlers[protocol](url, context, true);
}

/**
 * @param {URL} url
 * @param {{parentURL: string}} context
 * @returns {Promise<string> | string | undefined} only works when enabled
 */
function defaultGetFormat(url, context) {
  const protocol = url.protocol;
  if (!ObjectPrototypeHasOwnProperty(protocolHandlers, protocol)) {
    return null;
  }
  return protocolHandlers[protocol](url, context, false);
}

module.exports = {
  defaultGetFormat,
  defaultGetFormatWithoutErrors,
  extensionFormatMap,
  extname,
};
