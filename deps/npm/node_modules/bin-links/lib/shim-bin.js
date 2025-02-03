const { resolve, dirname } = require('path')
const { lstat } = require('fs/promises')
const throwNonEnoent = er => {
  if (er.code !== 'ENOENT') {
    throw er
  }
}

const cmdShim = require('cmd-shim')
const readCmdShim = require('read-cmd-shim')

const fixBin = require('./fix-bin.js')

// even in --force mode, we never create a shim over a shim we've
// already created.  you can have multiple packages in a tree trying
// to contend for the same bin, which creates a race condition and
// nondeterminism.
const seen = new Set()

const failEEXIST = ({ to, from }) =>
  Promise.reject(Object.assign(new Error('EEXIST: file already exists'), {
    path: to,
    dest: from,
    code: 'EEXIST',
  }))

const handleReadCmdShimError = ({ er, from, to }) =>
  er.code === 'ENOENT' ? null
  : er.code === 'ENOTASHIM' ? failEEXIST({ from, to })
  : Promise.reject(er)

const SKIP = Symbol('skip - missing or already installed')
const shimBin = ({ path, to, from, absFrom, force }) => {
  const shims = [
    to,
    to + '.cmd',
    to + '.ps1',
  ]

  for (const shim of shims) {
    if (seen.has(shim)) {
      return true
    }
    seen.add(shim)
  }

  return Promise.all([
    ...shims,
    absFrom,
  ].map(f => lstat(f).catch(throwNonEnoent))).then((stats) => {
    const [, , , stFrom] = stats
    if (!stFrom) {
      return SKIP
    }

    if (force) {
      return false
    }

    return Promise.all(shims.map((s, i) => [s, stats[i]]).map(([s, st]) => {
      if (!st) {
        return false
      }
      return readCmdShim(s)
        .then(target => {
          target = resolve(dirname(to), target)
          if (target.indexOf(resolve(path)) !== 0) {
            return failEEXIST({ from, to, path })
          }
          return false
        }, er => handleReadCmdShimError({ er, from, to }))
    }))
  })
    .then(skip => skip !== SKIP && doShim(absFrom, to))
}

const doShim = (absFrom, to) =>
  cmdShim(absFrom, to).then(() => fixBin(absFrom))

const resetSeen = () => {
  for (const p of seen) {
    seen.delete(p)
  }
}

module.exports = Object.assign(shimBin, { resetSeen })
