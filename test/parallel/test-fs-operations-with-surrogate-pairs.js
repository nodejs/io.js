'use strict';

require('../common');
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert');
const { describe, it } = require('node:test');
const tmpdir = require('../common/tmpdir');

tmpdir.refresh();

describe('File operations with filenames containing surrogate pairs', () => {
  it('should write, read, and delete a file with surrogate pairs in the filename', () => {
    // Create a temporary directory
    const tempdir = fs.mkdtempSync(tmpdir.resolve('emoji-fruit-🍇 🍈 🍉 🍊 🍋'));
    assert.strictEqual(fs.existsSync(tempdir), true);

    const filename = '🚀🔥🛸.txt';
    const content = 'Test content';

    // Write content to a file
    fs.writeFileSync(path.join(tempdir, filename), content);

    // Read content from the file
    const readContent = fs.readFileSync(path.join(tempdir, filename), 'utf8');

    // Check if the content matches
    assert.strictEqual(readContent, content);

  });
});
