'use strict';
const {
  ArrayBufferPrototypeSlice,
} = primordials;

const { isSea, getAsset: getAssetInternal } = internalBinding('sea');
const { TextDecoder } = require('internal/encoding');
const { validateString } = require('internal/validators');
const {
  ERR_NOT_IN_SINGLE_EXECUTABLE_APPLICATION,
  ERR_SINGLE_EXECUTABLE_APPLICATION_ASSET_NOT_FOUND,
} = require('internal/errors').codes;
const { Blob } = require('internal/blob');

/**
 * Look for the asset in the injected SEA blob using the key. If
 * no matching asset is found an error is thrown. The returned
 * ArrayBuffer should not be mutated or otherwise the process
 * can crash due to access violation.
 * @param {string} key
 * @returns {ArrayBuffer}
 */
function getRawAsset(key) {
  validateString(key, 'key');

  if (!isSea()) {
    throw new ERR_NOT_IN_SINGLE_EXECUTABLE_APPLICATION();
  }

  const asset = getAssetInternal(key);
  if (asset === undefined) {
    throw new ERR_SINGLE_EXECUTABLE_APPLICATION_ASSET_NOT_FOUND(key);
  }
  return asset;
}

/**
 * Look for the asset in the injected SEA blob using the key. If the
 * encoding is specified, return a string decoded from it by TextDecoder,
 * otherwise return *a copy* of the original data in an ArrayBuffer. If
 * no matching asset is found an error is thrown.
 * @param {string} key
 * @param {string|undefined} encoding
 * @returns {string|ArrayBuffer}
 */
function getAsset(key, encoding) {
  if (encoding !== undefined) {
    validateString(encoding, 'encoding');
  }
  const asset = getRawAsset(key);
  if (encoding === undefined) {
    return ArrayBufferPrototypeSlice(asset);
  }
  const decoder = new TextDecoder(encoding);
  return decoder.decode(asset);
}

/**
 * Look for the asset in the injected SEA blob using the key. If
 * no matching asset is found an error is thrown. The data is returned
 * in a Blob. If no matching asset is found an error is thrown.
 * @param {string} key
 * @param {ConstructorParameters<Blob>[1]} [options]
 * @returns {Blob}
 */
function getAssetAsBlob(key, options) {
  const asset = getRawAsset(key);
  return new Blob([asset], options);
}

module.exports = {
  isSea,
  getAsset,
  getRawAsset,
  getAssetAsBlob,
};
