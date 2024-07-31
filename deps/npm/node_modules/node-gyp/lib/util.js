'use strict'

const cp = require('child_process')
const path = require('path')
const { openSync, closeSync } = require('graceful-fs')
const log = require('./log')

const execFile = async (...args) => new Promise((resolve) => {
  const child = cp.execFile(...args, (...a) => resolve(a))
  child.stdin.end()
})

async function regGetValue (key, value, addOpts) {
  const outReValue = value.replace(/\W/g, '.')
  const outRe = new RegExp(`^\\s+${outReValue}\\s+REG_\\w+\\s+(\\S.*)$`, 'im')
  const reg = path.join(process.env.SystemRoot, 'System32', 'reg.exe')
  const regArgs = ['query', key, '/v', value].concat(addOpts)

  log.silly('reg', 'running', reg, regArgs)
  const [err, stdout, stderr] = await execFile(reg, regArgs, { encoding: 'utf8' })

  log.silly('reg', 'reg.exe stdout = %j', stdout)
  if (err || stderr.trim() !== '') {
    log.silly('reg', 'reg.exe err = %j', err && (err.stack || err))
    log.silly('reg', 'reg.exe stderr = %j', stderr)
    if (err) {
      throw err
    }
    throw new Error(stderr)
  }

  const result = outRe.exec(stdout)
  if (!result) {
    log.silly('reg', 'error parsing stdout')
    throw new Error('Could not parse output of reg.exe')
  }

  log.silly('reg', 'found: %j', result[1])
  return result[1]
}

async function regSearchKeys (keys, value, addOpts) {
  for (const key of keys) {
    try {
      return await regGetValue(key, value, addOpts)
    } catch {
      continue
    }
  }
}

/**
 * Returns the first file or directory from an array of candidates that is
 * readable by the current user, or undefined if none of the candidates are
 * readable.
 */
function findAccessibleSync (logprefix, dir, candidates) {
  for (let next = 0; next < candidates.length; next++) {
    const candidate = path.resolve(dir, candidates[next])
    let fd
    try {
      fd = openSync(candidate, 'r')
    } catch (e) {
      // this candidate was not found or not readable, do nothing
      log.silly(logprefix, 'Could not open %s: %s', candidate, e.message)
      continue
    }
    closeSync(fd)
    log.silly(logprefix, 'Found readable %s', candidate)
    return candidate
  }

  return undefined
}

module.exports = {
  execFile,
  regGetValue,
  regSearchKeys,
  findAccessibleSync
}
