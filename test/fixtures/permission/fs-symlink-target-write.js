'use strict'

const common = require('../../common');

const assert = require('assert');
const fs = require('fs');
const path = require('path');

const readOnlyFolder = process.env.READONLYFOLDER;
const readWriteFolder = process.env.READWRITEFOLDER;
const writeOnlyFolder = process.env.WRITEONLYFOLDER;

{
  assert.ok(!process.permission.has('fs.write', readOnlyFolder));
  assert.ok(!process.permission.has('fs.read', writeOnlyFolder));
  assert.ok(process.permission.has('fs.write', readWriteFolder));

  assert.ok(process.permission.has('fs.write', writeOnlyFolder));
  assert.ok(process.permission.has('fs.read', readOnlyFolder));
  assert.ok(process.permission.has('fs.read', readWriteFolder));
}

{
  // App won't be able to symlink from a readOnlyFolder
  assert.throws(() => {
    fs.symlinkSync(path.join(readOnlyFolder, 'file'), path.join(readWriteFolder, 'link-to-read-only'), 'file');
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(path.join(readOnlyFolder, 'file')),
  }));
  assert.throws(() => {
    fs.linkSync(path.join(readOnlyFolder, 'file'), path.join(readWriteFolder, 'link-to-read-only'));
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(path.join(readOnlyFolder, 'file')),
  }));

  // App will be able to symlink to a writeOnlyFolder
  fs.symlink(path.join(readWriteFolder, 'file'), path.join(writeOnlyFolder, 'link-to-read-write'), 'file', (err) => {
    assert.ifError(err);
    // App will won't be able to read the symlink
    fs.readFile(path.join(writeOnlyFolder, 'link-to-read-write'), common.expectsError({
      code: 'ERR_ACCESS_DENIED',
      permission: 'FileSystemRead',
    }));

    // App will be able to write to the symlink
    fs.writeFile(path.join(writeOnlyFolder, 'link-to-read-write'), 'some content', common.mustSucceed());
  });
  fs.link(path.join(readWriteFolder, 'file'), path.join(writeOnlyFolder, 'link-to-read-write2'), (err) => {
    assert.ifError(err);
    // App will won't be able to read the link
    fs.readFile(path.join(writeOnlyFolder, 'link-to-read-write2'), common.expectsError({
      code: 'ERR_ACCESS_DENIED',
      permission: 'FileSystemRead',
    }));

    // App will be able to write to the link
    fs.writeFile(path.join(writeOnlyFolder, 'link-to-read-write2'), 'some content', common.mustSucceed());
  });

  // App won't be able to symlink to a readOnlyFolder
  assert.throws(() => {
    fs.symlinkSync(path.join(readWriteFolder, 'file'), path.join(readOnlyFolder, 'link-to-read-only'), 'file');
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(path.join(readOnlyFolder, 'link-to-read-only')),
  }));
  assert.throws(() => {
    fs.linkSync(path.join(readWriteFolder, 'file'), path.join(readOnlyFolder, 'link-to-read-only'));
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(path.join(readOnlyFolder, 'link-to-read-only')),
  }));
}