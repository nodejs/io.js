'use strict';

const common = require('../../common');

const assert = require('assert');
const fs = require('fs');
const path = require('path');

const blockedFile = process.env.BLOCKEDFILE;
const blockedFileURL = new URL('file://' + process.env.BLOCKEDFILE);
const blockedFolder = process.env.BLOCKEDFOLDER;
const allowedFolder = process.env.ALLOWEDFOLDER;
const regularFile = __filename;

// fs.readFile
{
  fs.readFile(blockedFile, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  }));
  assert.throws(() => {
    fs.readFileSync(blockedFile);
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  }));
  assert.throws(() => {
    fs.readFileSync(blockedFileURL);
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  }));
}

// fs.createReadStream
{
  assert.rejects(() => {
    return new Promise((_resolve, reject) => {
      const stream = fs.createReadStream(blockedFile);
      stream.on('error', reject);
    });
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  })).then(common.mustCall());
  assert.rejects(() => {
    return new Promise((_resolve, reject) => {
      const stream = fs.createReadStream(blockedFileURL);
      stream.on('error', reject);
    });
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  })).then(common.mustCall());

  assert.rejects(() => {
    return new Promise((_resolve, reject) => {
      const stream = fs.createReadStream(blockedFile);
      stream.on('error', reject);
    });
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  })).then(common.mustCall());
}

// fs.stat
{
  fs.stat(blockedFile, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  }));
  assert.throws(() => {
    fs.statSync(blockedFile);
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  }));
  assert.throws(() => {
    fs.statSync(blockedFileURL);
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  }));
  fs.stat(path.join(blockedFolder, 'anyfile'), common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(path.join(blockedFolder, 'anyfile')),
  }));

  // doesNotThrow
  fs.stat(regularFile, (err) => {
    assert.ifError(err);
  });
}

// fs.access
{
  fs.access(blockedFile, fs.constants.R_OK, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  }));
  assert.throws(() => {
    fs.accessSync(blockedFileURL, fs.constants.R_OK);
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  }));
  assert.throws(() => {
    fs.accessSync(path.join(blockedFolder, 'anyfile'), fs.constants.R_OK);
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(path.join(blockedFolder, 'anyfile')),
  }));

  // doesNotThrow
  fs.access(regularFile, fs.constants.R_OK, (err) => {
    assert.ifError(err);
  });
}

// fs.copyFile
{
  fs.copyFile(blockedFile, path.join(blockedFolder, 'any-other-file'), common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  }));
  assert.throws(() => {
    fs.copyFileSync(blockedFileURL, path.join(blockedFolder, 'any-other-file'));
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  }));
  assert.throws(() => {
    fs.copyFileSync(blockedFile, path.join(__dirname, 'any-other-file'));
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  }));
}

// fs.cp
{
  assert.throws(() => {
    fs.cpSync(blockedFile, path.join(blockedFolder, 'any-other-file'));
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  }));
  assert.throws(() => {
    fs.cpSync(blockedFileURL, path.join(blockedFolder, 'any-other-file'));
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  }));
  assert.throws(() => {
    fs.cpSync(blockedFile, path.join(__dirname, 'any-other-file'));
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  }));
}

// fs.open
{
  fs.open(blockedFile, 'r', common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  }));
  assert.throws(() => {
    fs.openSync(blockedFileURL, 'r');
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  }));
  assert.throws(() => {
    fs.openSync(path.join(blockedFolder, 'anyfile'), 'r');
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(path.join(blockedFolder, 'anyfile')),
  }));

  // doesNotThrow
  fs.open(regularFile, 'r', (err) => {
    assert.ifError(err);
  });

  // Extra flags should not enable trivially bypassing all restrictions.
  // See https://github.com/nodejs/node/issues/47090.
  assert.throws(() => {
    fs.openSync(blockedFile, fs.constants.O_RDONLY | fs.constants.O_NOCTTY);
  }, {
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
  });
  fs.open(blockedFile, fs.constants.O_RDWR | 0x10000000, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
  }));
}

// fs.opendir
{
  fs.opendir(blockedFolder, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFolder),
  }));
  assert.throws(() => {
    fs.opendirSync(blockedFolder);
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFolder),
  }));
  // doesNotThrow
  fs.opendir(allowedFolder, (err, dir) => {
    assert.ifError(err);
    dir.closeSync();
  });
}

// fs.readdir
{
  fs.readdir(blockedFolder, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFolder),
  }));
  assert.throws(() => {
    fs.readdirSync(blockedFolder);
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFolder),
  }));

  // doesNotThrow
  fs.readdir(allowedFolder, (err) => {
    assert.ifError(err);
  });
}

// fs.watch
{
  assert.throws(() => {
    fs.watch(blockedFile, () => {});
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  }));
  assert.throws(() => {
    fs.watch(blockedFileURL, () => {});
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  }));

  // doesNotThrow
  fs.readdir(allowedFolder, (err) => {
    assert.ifError(err);
  });
}

// fs.watchFile
{
  assert.throws(() => {
    fs.watchFile(blockedFile, common.mustNotCall());
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  }));
  assert.throws(() => {
    fs.watchFile(blockedFileURL, common.mustNotCall());
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  }));
}

// fs.rename
{
  fs.rename(blockedFile, 'newfile', common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  }));
  assert.throws(() => {
    fs.renameSync(blockedFile, 'newfile');
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  }));
  assert.throws(() => {
    fs.renameSync(blockedFileURL, 'newfile');
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  }));
}

// fs.openAsBlob
{
  assert.throws(() => {
    fs.openAsBlob(blockedFile);
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  }));
  assert.throws(() => {
    fs.openAsBlob(blockedFileURL);
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  }));
}

// fs.exists
{
  // It will return false (without performing IO) when permissions is not met
  fs.exists(blockedFile, (exists) => {
    assert.equal(exists, false);
  });
  fs.exists(blockedFileURL, (exists) => {
    assert.equal(exists, false);
  });
  assert.throws(() => {
    fs.existsSync(blockedFile);
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  }));
}

// fs.statfs
{
  fs.statfs(blockedFile, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  }));
  assert.throws(() => {
    fs.statfsSync(blockedFile);
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  }));
  assert.throws(() => {
    fs.statfsSync(blockedFileURL);
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: path.toNamespacedPath(blockedFile),
  }));
}

// process.chdir
{
  assert.throws(() => {
    process.chdir(blockedFolder);
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemRead',
    resource: blockedFolder,
  }));
}

// fs.lstat
{
  assert.throws(() => {
    fs.lstatSync(blockedFile);
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
  }));
  assert.throws(() => {
    fs.lstatSync(path.join(blockedFolder, 'anyfile'));
  }, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
  }));

  // doesNotThrow
  fs.lstat(regularFile, (err) => {
    assert.ifError(err);
  });
}