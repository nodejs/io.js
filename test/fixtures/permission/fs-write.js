'use strict';

const common = require('../../common');
common.skipIfWorker();

const assert = require('assert');
const fs = require('fs');
const path = require('path');

const regularFolder = process.env.ALLOWEDFOLDER;
const regularFile = process.env.ALLOWEDFILE;
const blockedFolder = process.env.BLOCKEDFOLDER;
const blockedFile = process.env.BLOCKEDFILE;
const bufferBlockedFile = Buffer.from(process.env.BLOCKEDFILE);
const blockedFileURL = require('url').pathToFileURL(process.env.BLOCKEDFILE);
const relativeProtectedFile = process.env.RELATIVEBLOCKEDFILE;
const relativeProtectedFolder = process.env.RELATIVEBLOCKEDFOLDER;

{
  assert.ok(!process.permission.has('fs.write', blockedFolder));
  assert.ok(!process.permission.has('fs.write', blockedFile));
}

// fs.writeFile
{
  assert.throws(() => {
    fs.writeFileSync(blockedFile, 'example');
  }, {
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(blockedFile),
  });
  fs.writeFile(blockedFile, 'example', common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(blockedFile),
  }));
  fs.writeFile(bufferBlockedFile, 'example', common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(blockedFile),
  }));
  assert.throws(() => {
    fs.writeFileSync(blockedFileURL, 'example');
  }, {
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(blockedFile),
  });
  assert.throws(() => {
    fs.writeFileSync(relativeProtectedFile, 'example');
  }, {
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(relativeProtectedFile),
  });

  assert.throws(() => {
    fs.writeFileSync(path.join(blockedFolder, 'anyfile'), 'example');
  }, {
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(path.join(blockedFolder, 'anyfile')),
  });
}

// fs.createWriteStream
{
  assert.rejects(() => {
    return new Promise((_resolve, reject) => {
      const stream = fs.createWriteStream(blockedFile);
      stream.on('error', reject);
    });
  }, {
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(blockedFile),
  }).then(common.mustCall());
  assert.rejects(() => {
    return new Promise((_resolve, reject) => {
      const stream = fs.createWriteStream(relativeProtectedFile);
      stream.on('error', reject);
    });
  }, {
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(relativeProtectedFile),
  }).then(common.mustCall());

  assert.rejects(() => {
    return new Promise((_resolve, reject) => {
      const stream = fs.createWriteStream(path.join(blockedFolder, 'example'));
      stream.on('error', reject);
    });
  }, {
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(path.join(blockedFolder, 'example')),
  }).then(common.mustCall());
}

// fs.utimes
{
  assert.throws(() => {
    fs.utimes(blockedFile, new Date(), new Date(), () => {});
  }, {
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(blockedFile),
  });
  assert.throws(() => {
    fs.utimes(bufferBlockedFile, new Date(), new Date(), () => {});
  }, {
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(blockedFile),
  });
  assert.throws(() => {
    fs.utimes(blockedFileURL, new Date(), new Date(), () => {});
  }, {
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(blockedFile),
  });
  assert.throws(() => {
    fs.utimes(relativeProtectedFile, new Date(), new Date(), () => {});
  }, {
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(relativeProtectedFile),
  });

  assert.throws(() => {
    fs.utimes(path.join(blockedFolder, 'anyfile'), new Date(), new Date(), () => {});
  }, {
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(path.join(blockedFolder, 'anyfile')),
  });
}

// fs.lutimes
{
  assert.throws(() => {
    fs.lutimes(blockedFile, new Date(), new Date(), () => {});
  },{
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(blockedFile),
  });
  assert.throws(() => {
    fs.lutimes(bufferBlockedFile, new Date(), new Date(), () => {});
  },{
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(blockedFile),
  });
  assert.throws(() => {
    fs.lutimes(blockedFileURL, new Date(), new Date(), () => {});
  }, {
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(blockedFile),
  });
}

// fs.mkdir
{
  assert.throws(() => {
    fs.mkdir(path.join(blockedFolder, 'any-folder'), (err) => {
      assert.ifError(err);
    });
  },{
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(path.join(blockedFolder, 'any-folder')),
  });
  assert.throws(() => {
    fs.mkdir(path.join(relativeProtectedFolder, 'any-folder'), (err) => {
      assert.ifError(err);
    });
  },{
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(path.join(relativeProtectedFolder, 'any-folder')),
  });
}

{
  assert.throws(() => {
    fs.mkdtempSync(path.join(blockedFolder, 'any-folder'));
  },{
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
  });
  fs.mkdtemp(path.join(relativeProtectedFolder, 'any-folder'), common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
  }));
}

// fs.rename
{
  assert.throws(() => {
    fs.renameSync(blockedFile, path.join(blockedFile, 'renamed'));
  }, {
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(blockedFile),
  });
  fs.rename(blockedFile, path.join(blockedFile, 'renamed'), common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(blockedFile),
  }));
  fs.rename(bufferBlockedFile, path.join(blockedFile, 'renamed'), common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(blockedFile),
  }));
  assert.throws(() => {
    fs.renameSync(blockedFileURL, path.join(blockedFile, 'renamed'));
  }, {
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(blockedFile),
  });
  assert.throws(() => {
    fs.renameSync(relativeProtectedFile, path.join(relativeProtectedFile, 'renamed'));
  },{
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(relativeProtectedFile),
  });
  assert.throws(() => {
    fs.renameSync(blockedFile, path.join(regularFolder, 'renamed'));
  }, {
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(blockedFile),
  });

  assert.throws(() => {
    fs.renameSync(regularFile, path.join(blockedFolder, 'renamed'));
  },{
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(path.join(blockedFolder, 'renamed')),
  });
}

// fs.copyFile
{
  assert.throws(() => {
    fs.copyFileSync(regularFile, path.join(blockedFolder, 'any-file'));
  },{
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(path.join(blockedFolder, 'any-file')),
  });
  assert.throws(() => {
    fs.copyFileSync(regularFile, path.join(relativeProtectedFolder, 'any-file'));
  },{
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(path.join(relativeProtectedFolder, 'any-file')),
  });
  fs.copyFile(regularFile, path.join(relativeProtectedFolder, 'any-file'), common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(path.join(relativeProtectedFolder, 'any-file')),
  }));
  fs.copyFile(bufferBlockedFile, path.join(relativeProtectedFolder, 'any-file'), common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(path.join(relativeProtectedFolder, 'any-file')),
  }));
}

// fs.cp
{
  assert.throws(() => {
    fs.cpSync(regularFile, path.join(blockedFolder, 'any-file'));
  },{
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(path.join(blockedFolder, 'any-file')),
  });
  assert.throws(() => {
    fs.cpSync(regularFile, path.join(relativeProtectedFolder, 'any-file'));
  },{
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(path.join(relativeProtectedFolder, 'any-file')),
  });
}

// fs.rm
{
  assert.throws(() => {
    fs.rmSync(blockedFolder, { recursive: true });
  },{
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(blockedFolder),
  });
  assert.throws(() => {
    fs.rmSync(relativeProtectedFolder, { recursive: true });
  },{
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(relativeProtectedFolder),
  });
}

// fs.open
{
  // Extra flags should not enable trivially bypassing all restrictions.
  // See https://github.com/nodejs/node/issues/47090.
  fs.open(blockedFile, fs.constants.O_RDWR | 0x10000000, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
  }));
  fs.open(blockedFileURL, fs.constants.O_RDWR | 0x10000000, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
  }));
  fs.open(bufferBlockedFile, fs.constants.O_RDWR | 0x10000000, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
  }));
  assert.rejects(async () => {
    await fs.promises.open(blockedFile, fs.constants.O_RDWR | fs.constants.O_NOFOLLOW);
  }, {
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
  });
  if (common.isWindows) {
    // In particular, on Windows, the permission system should not blindly let
    // code delete write-protected files.
    const O_TEMPORARY = 0x40;
    assert.throws(() => {
      fs.openSync(blockedFile, fs.constants.O_RDONLY | O_TEMPORARY);
    }, {
      code: 'ERR_ACCESS_DENIED',
      permission: 'FileSystemWrite'
    });
  }
}

// fs.chmod
{
  assert.throws(() => {
    fs.chmod(blockedFile, 0o755, common.mustNotCall());
  }, {
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
  });
  assert.throws(() => {
    fs.chmod(bufferBlockedFile, 0o755, common.mustNotCall());
  }, {
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
  });
  assert.throws(() => {
    fs.chmod(blockedFileURL, 0o755, common.mustNotCall());
  }, {
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
  });
  assert.rejects(async () => {
    await fs.promises.chmod(blockedFile, 0o755);
  }, {
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
  });
}

// fs.lchmod
{
  if (common.isMacOS) {
    fs.lchmod(blockedFile, 0o755, common.expectsError({
      code: 'ERR_ACCESS_DENIED',
      permission: 'FileSystemWrite',
    }));
    assert.rejects(async () => {
      await fs.promises.lchmod(blockedFile, 0o755);
    }, {
      code: 'ERR_ACCESS_DENIED',
      permission: 'FileSystemWrite',
    });
  }
}

// fs.appendFile
{
  fs.appendFile(blockedFile, 'new data', common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
  }));
  fs.appendFile(bufferBlockedFile, 'new data', common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
  }));
  assert.throws(() => {
    fs.appendFileSync(blockedFileURL, 'new data');
  }, {
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
  });
  assert.rejects(async () => {
    await fs.promises.appendFile(blockedFile, 'new data');
  }, {
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
  });
}

// fs.chown
{
  fs.chown(blockedFile, 1541, 999, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
  }));
  fs.chown(bufferBlockedFile, 1541, 999, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
  }));
  assert.throws(() => {
    fs.chownSync(blockedFileURL, 1541, 999);
  }, {
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
  });
  // TODO(@RafaelGSS): Uncaught Exception somehow?
  // assert.rejects(async () => {
  //   return fs.promises.chown(blockedFile, 1541, 999);
  // }, {
  //   code: 'ERR_ACCESS_DENIED',
  //   permission: 'FileSystemWrite',
  // });
}

// fs.lchown
{
  fs.lchown(blockedFile, 1541, 999, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
  }));
  fs.lchown(bufferBlockedFile, 1541, 999, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
  }));
  assert.throws(() => {
    fs.lchownSync(blockedFileURL, 1541, 999);
  }, {
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
  });
  // TODO(@RafaelGSS): Uncaught Exception somehow?
  // assert.rejects(async () => {
  //   await fs.promises.lchown(blockedFile, 1541, 999);
  // }, {
  //   code: 'ERR_ACCESS_DENIED',
  //   permission: 'FileSystemWrite',
  // });
}

// fs.link
{
  assert.throws(() => {
    fs.linkSync(blockedFile, path.join(blockedFolder, '/linked'));
  }, {
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
  });
  fs.link(blockedFile, path.join(blockedFolder, '/linked'), common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
  }));
  fs.link(bufferBlockedFile, path.join(blockedFolder, '/linked'), common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
  }));
  assert.throws(() => {
    fs.linkSync(blockedFileURL, path.join(blockedFolder, '/linked'));
  }, {
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
  });
  // TODO(@RafaelGSS): Uncaught Exception somehow?
  // assert.rejects(async () => {
  //   await fs.promises.link(blockedFile, path.join(blockedFolder, '/linked'));
  // }, {
  //   code: 'ERR_ACCESS_DENIED',
  //   permission: 'FileSystemWrite',
  // });
}

// fs.unlink
{
  assert.throws(() => {
    fs.unlinkSync(blockedFile);
  }, {
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(blockedFile),
  });
  assert.throws(() => {
    fs.unlinkSync(bufferBlockedFile);
  }, {
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(blockedFile),
  });
  fs.unlink(blockedFile, common.expectsError({
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(blockedFile),
  }));
  assert.throws(() => {
    fs.unlinkSync(blockedFileURL);
  }, {
    code: 'ERR_ACCESS_DENIED',
    permission: 'FileSystemWrite',
    resource: path.toNamespacedPath(blockedFile),
  });
}

// fs.fchown with read-only fd
{
  assert.throws(() => {
    // blocked file is allowed to read
    const fd = fs.openSync(blockedFile, 'r');
    fs.fchmod(fd, 777, common.expectsError({
      code: 'ERR_ACCESS_DENIED',
    }));
    fs.fchmodSync(fd, 777);
  }, {
    code: 'ERR_ACCESS_DENIED',
  });
}

// fs.fchmod with read-only fd
{
  assert.throws(() => {
    // blocked file is allowed to read
    const fd = fs.openSync(blockedFile, 'r');
    fs.fchown(fd, 999, 999, common.expectsError({
      code: 'ERR_ACCESS_DENIED',
    }));
    fs.fchownSync(fd, 999, 999);
  }, {
    code: 'ERR_ACCESS_DENIED',
  });
}