'use strict';

const { isSea, getAsset, getRawAsset } = require('node:sea');
const { readFileSync } = require('fs');
const assert = require('assert');

assert(isSea());

{
  assert.throws(() => getRawAsset('nonexistent'), {
    code: 'ERR_SINGLE_EXECUTABLE_APPLICATION_ASSET_NOT_FOUND'
  });
  assert.throws(() => getRawAsset(null), {
    code: 'ERR_INVALID_ARG_TYPE'
  });
  assert.throws(() => getRawAsset(1), {
    code: 'ERR_INVALID_ARG_TYPE'
  });
}

{
  // Check that the asset embedded is the same as the original.
  const assetOnDisk = readFileSync(process.env.__TEST_PERSON_JPG);
  const assetCopy = getAsset('person.jpg')
  const assetCopyBuffer = Buffer.from(assetCopy);
  assert.deepStrictEqual(assetCopyBuffer, assetOnDisk);

  // Check that the copied asset is the same as the raw one.
  const rawAsset = getRawAsset('person.jpg');
  assert.deepStrictEqual(rawAsset, assetCopy);
}
