'use strict';
const common = require('../common');
const assert = require('assert');
const fs = require('fs');

const tmpdir = require('../common/tmpdir');
tmpdir.refresh();

const filename = tmpdir.resolve('test.txt');
const fixtures = require('../common/fixtures');
const s = fixtures.utf8TestText;

// The length of the buffer should be a multiple of 8
// as required by common.getArrayBufferViews()
const inputBuffer = Buffer.from(s.repeat(8), 'utf8');

for (const expectView of common.getArrayBufferViews(inputBuffer)) {
  console.log('Sync test for ', expectView[Symbol.toStringTag]);
  fs.writeFileSync(filename, expectView);
  assert.strictEqual(
    fs.readFileSync(filename, 'utf8'),
    inputBuffer.toString('utf8')
  );
}

for (const expectView of common.getArrayBufferViews(inputBuffer)) {
  console.log('Async test for ', expectView[Symbol.toStringTag]);
  const file = `${filename}-${expectView[Symbol.toStringTag]}`;
  fs.writeFile(file, expectView, common.mustSucceed(() => {
    fs.readFile(file, 'utf8', common.mustSucceed((data) => {
      assert.strictEqual(data, inputBuffer.toString('utf8'));
    }));
  }));
}
