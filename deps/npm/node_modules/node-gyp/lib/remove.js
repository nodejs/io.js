'use strict'

const fs = require('graceful-fs').promises
const path = require('path')
const log = require('./log')
const semver = require('semver')

async function remove (gyp, argv) {
  const devDir = gyp.devDir
  log.verbose('remove', 'using node-gyp dir:', devDir)

  // get the user-specified version to remove
  let version = argv[0] || gyp.opts.target
  log.verbose('remove', 'removing target version:', version)

  if (!version) {
    throw new Error('You must specify a version number to remove. Ex: "' + process.version + '"')
  }

  const versionSemver = semver.parse(version)
  if (versionSemver) {
    // flatten the version Array into a String
    version = versionSemver.version
  }

  const versionPath = path.resolve(gyp.devDir, version)
  log.verbose('remove', 'removing development files for version:', version)

  // first check if its even installed
  try {
    await fs.stat(versionPath)
  } catch (err) {
    if (err.code === 'ENOENT') {
      return 'version was already uninstalled: ' + version
    }
    throw err
  }

  await fs.rm(versionPath, { recursive: true, force: true })
}

module.exports = remove
module.exports.usage = 'Removes the node development files for the specified version'
