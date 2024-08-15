'use strict';

const {
  ObjectFreeze,
} = primordials;

const permission = internalBinding('permission');
const { validateString, validateBuffer } = require('internal/validators');
const { Buffer } = require('buffer');
const { isBuffer } = Buffer;

let experimentalPermission;

module.exports = ObjectFreeze({
  __proto__: null,
  isEnabled() {
    if (experimentalPermission === undefined) {
      const { getOptionValue } = require('internal/options');
      experimentalPermission = getOptionValue('--experimental-permission');
    }
    return experimentalPermission;
  },
  has(scope, reference) {
    validateString(scope, 'scope');
    if (reference != null) {
      // TODO: add support for WHATWG URLs and Uint8Arrays.
      if (isBuffer(reference)) {
        validateBuffer(reference, 'reference');
      } else {
        validateString(reference, 'reference');
      }
    }

    return permission.has(scope, reference);
  },
});
