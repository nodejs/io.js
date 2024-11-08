// if the thing isn't there, skip it
// if there's a non-symlink there already, eexist
// if there's a symlink already, pointing somewhere else, eexist
// if there's a symlink already, pointing into our pkg, remove it first
// then create the symlink

const { resolve, dirname } = require('path')
const { lstat, mkdir, readlink, rm, symlink } = require('fs/promises')
const { log } = require('proc-log')
const throwSignificant = er => {
  if (er.code === 'ENOENT') {
    return
  }
  if (er.code === 'EACCES') {
    log.warn('error adding file', er.message)
    return
  }
  throw er
}

const rmOpts = {
  recursive: true,
  force: true,
}

// even in --force mode, we never create a link over a link we've
// already created.  you can have multiple packages in a tree trying
// to contend for the same bin, or the same manpage listed multiple times,
// which creates a race condition and nondeterminism.
const seen = new Set()

const SKIP = Symbol('skip - missing or already installed')
const CLOBBER = Symbol('clobber - ours or in forceful mode')

const linkGently = async ({ path, to, from, absFrom, force }) => {
  if (seen.has(to)) {
    return false
  }
  seen.add(to)

  // if the script or manpage isn't there, just ignore it.
  // this arguably *should* be an install error of some sort,
  // or at least a warning, but npm has always behaved this
  // way in the past, so it'd be a breaking change
  return Promise.all([
    lstat(absFrom).catch(throwSignificant),
    lstat(to).catch(throwSignificant),
  ]).then(([stFrom, stTo]) => {
    // not present in package, skip it
    if (!stFrom) {
      return SKIP
    }

    // exists! maybe clobber if we can
    if (stTo) {
      if (!stTo.isSymbolicLink()) {
        return force && rm(to, rmOpts).then(() => CLOBBER)
      }

      return readlink(to).then(target => {
        if (target === from) {
          return SKIP
        } // skip it, already set up like we want it.

        target = resolve(dirname(to), target)
        if (target.indexOf(path) === 0 || force) {
          return rm(to, rmOpts).then(() => CLOBBER)
        }
        // neither skip nor clobber
        return false
      })
    } else {
      // doesn't exist, dir might not either
      return mkdir(dirname(to), { recursive: true })
    }
  })
    .then(skipOrClobber => {
      if (skipOrClobber === SKIP) {
        return false
      }
      return symlink(from, to, 'file').catch(er => {
        if (skipOrClobber === CLOBBER || force) {
          return rm(to, rmOpts).then(() => symlink(from, to, 'file'))
        }
        throw er
      }).then(() => true)
    })
}

const resetSeen = () => {
  for (const p of seen) {
    seen.delete(p)
  }
}

module.exports = Object.assign(linkGently, { resetSeen })
