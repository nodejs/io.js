'use strict'

const fs = require('graceful-fs').promises
const log = require('./log')

async function list (gyp, args) {
  const devDir = gyp.devDir
  log.verbose('list', 'using node-gyp dir:', devDir)

  let versions = []
  try {
    const dir = await fs.readdir(devDir)
    if (Array.isArray(dir)) {
      versions = dir.filter((v) => v !== 'current')
    }
  } catch (err) {
    if (err && err.code !== 'ENOENT') {
      throw err
    }
  }

  return versions
}

module.exports = list
module.exports.usage = 'Prints a listing of the currently installed node development files'
