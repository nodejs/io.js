'use strict'

const common = require('../../common');

const assert = require('assert');
const fs = require('fs');
const path = require('path');

const { resolve } = path;
// This should not affect how the permission model resolves paths.
try {
  path.resolve = (s) => s;
  assert.fail('should not be called');
} catch {}

const blockedFolder = process.env.BLOCKEDFOLDER;
const allowedFolder = process.env.ALLOWEDFOLDER;
const traversalPath = allowedFolder + '/../file.md';
const traversalFolderPath = allowedFolder + '/../folder';
const bufferTraversalPath = Buffer.from(traversalPath);
const uint8ArrayTraversalPath = new TextEncoder().encode(traversalPath);

{
  assert.ok(process.permission.has('fs.read', allowedFolder));
  assert.ok(process.permission.has('fs.write', allowedFolder));
  assert.ok(!process.permission.has('fs.read', blockedFolder));
  assert.ok(!process.permission.has('fs.write', blockedFolder));
}

{
  fs.writeFile(traversalPath, 'test', common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(traversalPath),
  }));
}

{
  fs.readFile(traversalPath, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(traversalPath),
  }));
}

{
  assert.throws(() => {
    fs.mkdtempSync(traversalFolderPath);
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: traversalFolderPath + 'XXXXXX',
  }));
}

{
  fs.mkdtemp(traversalFolderPath, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: traversalFolderPath + 'XXXXXX',
  }));
}

{
  fs.readFile(bufferTraversalPath, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(traversalPath),
  }));
}

{
  fs.lstat(bufferTraversalPath, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    // lstat checks and throw on JS side.
    // resource is only resolved on C++ (is_granted)
    resource: bufferTraversalPath.toString(),
  }));
}

{
  fs.readFile(uint8ArrayTraversalPath, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(traversalPath),
  }));
}

// Monkey-patching Buffer internals should also not allow path traversal.
{
  const extraChars = '.'.repeat(40);
  const traversalPathWithExtraChars = traversalPath + extraChars;
  const traversalPathWithExtraBytes = Buffer.from(traversalPathWithExtraChars);

  Buffer.prototype.utf8Write = ((w) => function(str, ...args) {
    assert.strictEqual(str, resolve(traversalPath) + extraChars);
    return w.apply(this, [traversalPath, ...args]);
  })(Buffer.prototype.utf8Write);

  // Sanity check (remove if the internals of Buffer.from change):
  // The custom implementation of utf8Write should cause Buffer.from() to encode
  // traversalPath instead of the sanitized output of resolve().
  assert.strictEqual(Buffer.from(resolve(traversalPathWithExtraChars)).toString(), traversalPath);

  assert.throws(() => {
    fs.readFileSync(traversalPathWithExtraBytes);
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(traversalPathWithExtraChars),
  }));

  assert.throws(() => {
    fs.readFileSync(new TextEncoder().encode(traversalPathWithExtraBytes.toString()));
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(traversalPathWithExtraChars),
  }));
}

{
  assert.ok(!process.permission.has('fs.read', traversalPath));
  assert.ok(!process.permission.has('fs.write', traversalPath));
  assert.ok(!process.permission.has('fs.read', traversalFolderPath));
  assert.ok(!process.permission.has('fs.write', traversalFolderPath));
}