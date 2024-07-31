'use strict';

const {
  ArrayPrototypeFilter,
  ArrayPrototypeIncludes,
  ObjectKeys,
  ObjectPrototypeHasOwnProperty,
  ObjectValues,
} = primordials;
const { validateString } = require('internal/validators');

const {
  ERR_IMPORT_ATTRIBUTE_TYPE_INCOMPATIBLE,
  ERR_IMPORT_ATTRIBUTE_MISSING,
  ERR_IMPORT_ATTRIBUTE_UNSUPPORTED,
} = require('internal/errors').codes;

// The HTML spec has an implied default type of `'javascript'`.
const kImplicitTypeAttribute = 'javascript';

/**
 * Define a map of module formats to import attributes types (the value of
 * `type` in `with { type: 'json' }`).
 * @type {Map<string, string>}
 */
const formatTypeMap = {
  '__proto__': null,
  'builtin': kImplicitTypeAttribute,
  'commonjs': kImplicitTypeAttribute,
  'json': 'json',
  'module': kImplicitTypeAttribute,
  'wasm': kImplicitTypeAttribute, // It's unclear whether the HTML spec will require an type attribute or not for Wasm; see https://github.com/WebAssembly/esm-integration/issues/42
};

/**
 * The HTML spec disallows the default type to be explicitly specified
 * (for now); so `import './file.js'` is okay but
 * `import './file.js' with { type: 'javascript' }` throws.
 * @type {Array<string, string>}
 */
const supportedTypeAttributes = ArrayPrototypeFilter(
  ObjectValues(formatTypeMap),
  (type) => type !== kImplicitTypeAttribute);


/**
 * Test a module's import attributes.
 * @param {string} url The URL of the imported module, for error reporting.
 * @param {string} format One of Node's supported translators
 * @param {Record<string, string>} importAttributes Validations for the
 *                                                  module import.
 * @returns {true}
 * @throws {TypeError} If the format and type attribute are incompatible.
 */
function validateAttributes(url, format,
                            importAttributes = { __proto__: null }) {
  const keys = ObjectKeys(importAttributes);
  for (let i = 0; i < keys.length; i++) {
    if (keys[i] !== 'type') {
      throw new ERR_IMPORT_ATTRIBUTE_UNSUPPORTED(keys[i], importAttributes[keys[i]]);
    }
  }
  const validType = formatTypeMap[format];

  switch (validType) {
    case undefined:
      // Ignore attributes for module formats we don't recognize, to allow new
      // formats in the future.
      return true;

    case kImplicitTypeAttribute:
      // This format doesn't allow an import type attribute, so the property
      // must not be set on the import attributes object.
      if (!ObjectPrototypeHasOwnProperty(importAttributes, 'type')) {
        return true;
      }
      return handleInvalidType(url, importAttributes.type);

    case importAttributes.type:
      // The type attribute is the valid type for this format.
      return true;

    default:
      // There is an expected type for this format, but the value of
      // `importAttributes.type` might not have been it.
      if (!ObjectPrototypeHasOwnProperty(importAttributes, 'type')) {
        // `type` wasn't specified at all.
        throw new ERR_IMPORT_ATTRIBUTE_MISSING(url, 'type', validType);
      }
      return handleInvalidType(url, importAttributes.type);
  }
}

/**
 * Throw the correct error depending on what's wrong with the type attribute.
 * @param {string} url The resolved URL for the module to be imported
 * @param {string} type The value of the import attributes' `type` property
 */
function handleInvalidType(url, type) {
  // `type` might have not been a string.
  validateString(type, 'type');

  // `type` might not have been one of the types we understand.
  if (!ArrayPrototypeIncludes(supportedTypeAttributes, type)) {
    throw new ERR_IMPORT_ATTRIBUTE_UNSUPPORTED('type', type);
  }

  // `type` was the wrong value for this format.
  throw new ERR_IMPORT_ATTRIBUTE_TYPE_INCOMPATIBLE(url, type);
}


module.exports = {
  kImplicitTypeAttribute,
  validateAttributes,
};
