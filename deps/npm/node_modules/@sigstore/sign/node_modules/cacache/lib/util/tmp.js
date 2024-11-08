'use strict'

const { withTempDir } = require('@npmcli/fs')
const fs = require('fs/promises')
const path = require('path')

module.exports.mkdir = mktmpdir

async function mktmpdir (cache, opts = {}) {
  const { tmpPrefix } = opts
  const tmpDir = path.join(cache, 'tmp')
  await fs.mkdir(tmpDir, { recursive: true, owner: 'inherit' })
  // do not use path.join(), it drops the trailing / if tmpPrefix is unset
  const target = `${tmpDir}${path.sep}${tmpPrefix || ''}`
  return fs.mkdtemp(target, { owner: 'inherit' })
}

module.exports.withTmp = withTmp

function withTmp (cache, opts, cb) {
  if (!cb) {
    cb = opts
    opts = {}
  }
  return withTempDir(path.join(cache, 'tmp'), cb, opts)
}
