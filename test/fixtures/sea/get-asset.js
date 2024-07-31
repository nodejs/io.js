'use strict';

const { isSea, getAsset, getAssetAsBlob } = require('node:sea');
const { readFileSync } = require('node:fs');
const assert = require('node:assert');

assert(isSea());

// Test invalid getAsset() calls.
{
  assert.throws(() => getAsset('utf8_test_text.txt', 'invalid'), {
    code: 'ERR_ENCODING_NOT_SUPPORTED'
  });

  [
    1,
    1n,
    Symbol(),
    false,
    () => {},
    {},
    [],
    null,
    undefined,
  ].forEach(arg => assert.throws(() => getAsset(arg), {
    code: 'ERR_INVALID_ARG_TYPE'
  }));

  assert.throws(() => getAsset('nonexistent'), {
    code: 'ERR_SINGLE_EXECUTABLE_APPLICATION_ASSET_NOT_FOUND'
  });
}

// Test invalid getAssetAsBlob() calls.
{
  // Invalid options argument.
  [
    123,
    123n,
    Symbol(),
    '',
    true,
  ].forEach(arg => assert.throws(() => {
    getAssetAsBlob('utf8_test_text.txt', arg)
  }, {
    code: 'ERR_INVALID_ARG_TYPE'
  }));

  assert.throws(() => getAssetAsBlob('nonexistent'), {
    code: 'ERR_SINGLE_EXECUTABLE_APPLICATION_ASSET_NOT_FOUND'
  });
}

const textAssetOnDisk = readFileSync(process.env.__TEST_UTF8_TEXT_PATH, 'utf8');
const binaryAssetOnDisk = readFileSync(process.env.__TEST_PERSON_JPG);

// Check getAsset() buffer copies.
{
  // Check that the asset embedded is the same as the original.
  const assetCopy1 = getAsset('person.jpg')
  const assetCopyBuffer1 = Buffer.from(assetCopy1);
  assert.deepStrictEqual(assetCopyBuffer1, binaryAssetOnDisk);

  const assetCopy2 = getAsset('person.jpg');
  const assetCopyBuffer2 = Buffer.from(assetCopy2);
  assert.deepStrictEqual(assetCopyBuffer2, binaryAssetOnDisk);

  // Zero-fill copy1.
  assetCopyBuffer1.fill(0);

  // Test that getAsset() returns an immutable copy.
  assert.deepStrictEqual(assetCopyBuffer2, binaryAssetOnDisk);
  assert.notDeepStrictEqual(assetCopyBuffer1, binaryAssetOnDisk);
}

// Check getAsset() with encoding.
{
  const actualAsset = getAsset('utf8_test_text.txt', 'utf8')
  assert.strictEqual(actualAsset, textAssetOnDisk);
  // Log it out so that the test could compare it and see if
  // it's encoded/decoded correctly in the SEA.
  console.log(actualAsset);
}

// Check getAssetAsBlob().
{
  let called = false;
  async function test() {
    const blob = getAssetAsBlob('person.jpg');
    const buffer = await blob.arrayBuffer();
    assert.deepStrictEqual(Buffer.from(buffer), binaryAssetOnDisk);
    const blob2 = getAssetAsBlob('utf8_test_text.txt');
    const text = await blob2.text();
    assert.strictEqual(text, textAssetOnDisk);
  }
  test().then(() => {
    called = true;
  });
  process.on('exit', () => {
    assert(called);
  });
}
