'use strict';

const {
  RegExpPrototypeExec,
  decodeURIComponent,
} = primordials;
const { kEmptyObject } = require('internal/util');

const { defaultGetFormat } = require('internal/modules/esm/get_format');
const { validateAttributes, emitImportAssertionWarning } = require('internal/modules/esm/assert');
const { getOptionValue } = require('internal/options');
const { readFileSync } = require('fs');

const defaultType =
  getOptionValue('--experimental-default-type');

const { Buffer: { from: BufferFrom } } = require('buffer');
const {
  isUnderNodeModules,
} = require('internal/modules/helpers');

const { URL } = require('internal/url');
const {
  ERR_INVALID_URL,
  ERR_UNKNOWN_MODULE_FORMAT,
  ERR_UNSUPPORTED_ESM_URL_SCHEME,
  ERR_UNSUPPORTED_NODE_MODULES_TYPE_STRIPPING,
} = require('internal/errors').codes;

const DATA_URL_PATTERN = /^[^/]+\/[^,;]+(?:[^,]*?)(;base64)?,([\s\S]*)$/;

/**
 * @param {URL} url URL to the module
 * @param {ESModuleContext} context used to decorate error messages
 * @returns {Promise<{ responseURL: string, source: string | BufferView }>}
 */
async function getSource(url, context) {
  const { protocol, href } = url;
  const responseURL = href;
  let source;
  if (protocol === 'file:') {
    const { readFile: readFileAsync } = require('internal/fs/promises').exports;
    source = await readFileAsync(url);
  } else if (protocol === 'data:') {
    const match = RegExpPrototypeExec(DATA_URL_PATTERN, url.pathname);
    if (!match) {
      throw new ERR_INVALID_URL(responseURL);
    }
    const { 1: base64, 2: body } = match;
    source = BufferFrom(decodeURIComponent(body), base64 ? 'base64' : 'utf8');
  } else {
    const supportedSchemes = ['file', 'data'];
    throw new ERR_UNSUPPORTED_ESM_URL_SCHEME(url, supportedSchemes);
  }
  return { __proto__: null, responseURL, source };
}

/**
 * @param {URL} url URL to the module
 * @param {ESModuleContext} context used to decorate error messages
 * @returns {{ responseURL: string, source: string | BufferView }}
 */
function getSourceSync(url, context) {
  const { protocol, href } = url;
  const responseURL = href;
  let source;
  if (protocol === 'file:') {
    source = readFileSync(url);
  } else if (protocol === 'data:') {
    const match = RegExpPrototypeExec(DATA_URL_PATTERN, url.pathname);
    if (!match) {
      throw new ERR_INVALID_URL(responseURL);
    }
    const { 1: base64, 2: body } = match;
    source = BufferFrom(decodeURIComponent(body), base64 ? 'base64' : 'utf8');
  } else {
    const supportedSchemes = ['file', 'data'];
    throw new ERR_UNSUPPORTED_ESM_URL_SCHEME(url, supportedSchemes);
  }
  return { __proto__: null, responseURL, source };
}


/**
 * Node.js default load hook.
 * @param {string} url
 * @param {LoadContext} context
 * @returns {LoadReturn}
 */
async function defaultLoad(url, context = kEmptyObject) {
  let responseURL = url;
  let {
    importAttributes,
    format,
    source,
  } = context;

  if (importAttributes == null && !('importAttributes' in context) && 'importAssertions' in context) {
    emitImportAssertionWarning();
    importAttributes = context.importAssertions;
    // Alias `importAssertions` to `importAttributes`
    context = {
      ...context,
      importAttributes,
    };
  }

  const urlInstance = new URL(url);

  throwIfUnsupportedURLScheme(urlInstance);

  if (urlInstance.protocol === 'node:') {
    source = null;
    format ??= 'builtin';
  } else if (format !== 'commonjs' || defaultType === 'module') {
    if (source == null) {
      ({ responseURL, source } = await getSource(urlInstance, context));
      context = { __proto__: context, source };
    }

    if (format == null) {
      // Now that we have the source for the module, run `defaultGetFormat` to detect its format.
      format = await defaultGetFormat(urlInstance, context);

      if (format === 'commonjs') {
        // For backward compatibility reasons, we need to discard the source in
        // order for the CJS loader to re-fetch it.
        source = null;
      }
    }
  }

  validateAttributes(url, format, importAttributes);

  // Use the synchronous commonjs translator which can deal with cycles.
  if (format === 'commonjs' && getOptionValue('--experimental-require-module')) {
    format = 'commonjs-sync';
  }

  if (getOptionValue('--experimental-strip-types') &&
    (format === 'module-typescript' || format === 'commonjs-typescript') &&
    isUnderNodeModules(url)) {
    throw new ERR_UNSUPPORTED_NODE_MODULES_TYPE_STRIPPING(url);
  }

  return {
    __proto__: null,
    format,
    responseURL,
    source,
  };
}
/**
 * @typedef LoadContext
 * @property {string} [format] A hint (possibly returned from `resolve`)
 * @property {string | Buffer | ArrayBuffer} [source] source
 * @property {Record<string, string>} [importAttributes] import attributes
 */

/**
 * @typedef LoadReturn
 * @property {string} format format
 * @property {URL['href']} responseURL The module's fully resolved URL
 * @property {Buffer} source source
 */

/**
 * @param {URL['href']} url
 * @param {LoadContext} [context]
 * @returns {LoadReturn}
 */
function defaultLoadSync(url, context = kEmptyObject) {
  let responseURL = url;
  const { importAttributes } = context;
  let {
    format,
    source,
  } = context;

  const urlInstance = new URL(url);

  throwIfUnsupportedURLScheme(urlInstance, false);

  if (urlInstance.protocol === 'node:') {
    source = null;
  } else if (source == null) {
    ({ responseURL, source } = getSourceSync(urlInstance, context));
    context.source = source;
  }

  format ??= defaultGetFormat(urlInstance, context);

  validateAttributes(url, format, importAttributes);

  // Use the synchronous commonjs translator which can deal with cycles.
  if (format === 'commonjs' && getOptionValue('--experimental-require-module')) {
    format = 'commonjs-sync';
  }

  return {
    __proto__: null,
    format,
    responseURL,
    source,
  };
}


/**
 * throws an error if the protocol is not one of the protocols
 * that can be loaded in the default loader
 * @param {URL} parsed
 */
function throwIfUnsupportedURLScheme(parsed) {
  // Avoid accessing the `protocol` property due to the lazy getters.
  const protocol = parsed?.protocol;
  if (
    protocol &&
    protocol !== 'file:' &&
    protocol !== 'data:' &&
    protocol !== 'node:' &&
    (
      protocol !== 'https:' &&
      protocol !== 'http:'
    )
  ) {
    const schemes = ['file', 'data', 'node'];
    throw new ERR_UNSUPPORTED_ESM_URL_SCHEME(parsed, schemes);
  }
}

/**
 * For a falsy `format` returned from `load`, throw an error.
 * This could happen from either a custom user loader _or_ from the default loader, because the default loader tries to
 * determine formats for data URLs.
 * @param {string} url The resolved URL of the module
 * @param {null | undefined | false | 0 | -0 | 0n | ''} format Falsy format returned from `load`
 */
function throwUnknownModuleFormat(url, format) {
  const dataUrl = RegExpPrototypeExec(
    /^data:([^/]+\/[^;,]+)(?:[^,]*?)(;base64)?,/,
    url,
  );

  throw new ERR_UNKNOWN_MODULE_FORMAT(
    dataUrl ? dataUrl[1] : format,
    url);
}


module.exports = {
  defaultLoad,
  defaultLoadSync,
  getSourceSync,
  throwUnknownModuleFormat,
};
