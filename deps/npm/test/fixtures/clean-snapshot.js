const { relative, dirname } = require('node:path')

// normalize line endings (for ini)
const cleanNewlines = (s) => s.replace(/\r\n/g, '\n')

// XXX: this also cleans quoted " in json snapshots
// ideally this could be avoided but its easier to just
// run this command inside cleanSnapshot
const normalizePath = (str) => cleanNewlines(str)
  .replace(/[A-z]:\\/g, '\\') // turn windows roots to posix ones
  .replace(/\\+/g, '/') // replace \ with /

const pathRegex = (p) => new RegExp(normalizePath(p), 'gi')

// create a cwd replacer in the module scope, since some tests
// overwrite process.cwd()
const CWD = pathRegex(process.cwd())
const TESTDIR = pathRegex(relative(process.cwd(), dirname(require.main.filename)))

const cleanCwd = (path) => normalizePath(path)
  // repalce CWD, TESTDIR, and TAPDIR separately
  .replace(CWD, '{CWD}')
  .replace(TESTDIR, '{TESTDIR}')
  .replace(/tap-testdir-[\w-.]+/gi, '{TAPDIR}')
  // if everything ended up in line, reduce it all to CWD
  .replace(/\{CWD\}\/\{TESTDIR\}\/\{TAPDIR\}/g, '{CWD}')
  // replace for platform differences in global nodemodules
  .replace(/lib\/node_modules/g, 'node_modules')
  .replace(/global\/lib/g, 'global')

const cleanDate = (str) =>
  str.replace(/\d{4}-\d{2}-\d{2}T\d{2}[_:]\d{2}[_:]\d{2}[_:.]\d{3}Z/g, '{DATE}')

const cleanTime = str => str.replace(/in [0-9]+m?s\s*$/gm, 'in {TIME}')

const cleanZlib = str => str
  .replace(/shasum:( *)[0-9a-f]{40}/g, 'shasum:$1{sha}')
  .replace(/integrity:( *).*/g, 'integrity:$1{integrity}')
  .replace(/package size:( *)[0-9 A-Z]*/g, 'package size:$1{size}')

  .replace(/"shasum": "[0-9a-f]{40}",/g, '"shasum": "{sha}",')
  .replace(/"integrity": ".*",/g, '"integrity": "{integrity}",')
  .replace(/"size": [0-9]*,/g, '"size": "{size}",')

const cleanPackumentCache = str => str
  .replace(/heap:[0-9]*/g, 'heap:{heap}')
  .replace(/maxSize:[0-9]*/g, 'maxSize:{maxSize}')
  .replace(/maxEntrySize:[0-9]*/g, 'maxEntrySize:{maxEntrySize}')

module.exports = {
  cleanCwd,
  cleanDate,
  cleanNewlines,
  cleanTime,
  cleanZlib,
  cleanPackumentCache,
  normalizePath,
  pathRegex,
}
