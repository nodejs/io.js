'use strict';

const {
  RegExpPrototypeExec,
} = primordials;
const {
  kEmptyObject,
} = require('internal/util');

const { defaultGetFormat } = require('internal/modules/esm/get_format');
const { validateAttributes, emitImportAssertionWarning } = require('internal/modules/esm/assert');
const { readFileSync } = require('fs');

const { Buffer: { from: BufferFrom } } = require('buffer');

const { URL } = require('internal/url');
const {
  ERR_INVALID_URL,
  ERR_UNKNOWN_MODULE_FORMAT,
  ERR_UNSUPPORTED_ESM_URL_SCHEME,
} = require('internal/errors').codes;

const {
  dataURLProcessor,
} = require('internal/data_url');

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
    const result = dataURLProcessor(url);
    if (result === 'failure') {
      throw new ERR_INVALID_URL(responseURL, null);
    }
    source = BufferFrom(result.body);
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
    const result = dataURLProcessor(url);
    if (result === 'failure') {
      throw new ERR_INVALID_URL(responseURL);
    }
    source = BufferFrom(result.body);
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
  } else if (format === 'addon') {
    // Skip loading addon file content. It must be loaded with dlopen from file system.
    source = null;
  } else if (format !== 'commonjs') {
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
