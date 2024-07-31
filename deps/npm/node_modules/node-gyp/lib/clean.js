'use strict'

const fs = require('graceful-fs').promises
const log = require('./log')

async function clean (gyp, argv) {
  // Remove the 'build' dir
  const buildDir = 'build'

  log.verbose('clean', 'removing "%s" directory', buildDir)
  await fs.rm(buildDir, { recursive: true, force: true })
}

module.exports = clean
module.exports.usage = 'Removes any generated build files and the "out" dir'
