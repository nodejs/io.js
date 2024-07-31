// look up the realpath, but cache stats to minimize overhead
// If the parent folder is in  the realpath cache, then we just
// lstat the child, since there's no need to do a full realpath
// This is not a separate module, and is much simpler than Node's
// built-in fs.realpath, because we only care about symbolic links,
// so we can handle many fewer edge cases.

const { lstat, readlink } = require('node:fs/promises')
const { resolve, basename, dirname } = require('node:path')

const realpathCached = (path, rpcache, stcache, depth) => {
  // just a safety against extremely deep eloops
  /* istanbul ignore next */
  if (depth > 2000) {
    throw eloop(path)
  }

  path = resolve(path)
  if (rpcache.has(path)) {
    return Promise.resolve(rpcache.get(path))
  }

  const dir = dirname(path)
  const base = basename(path)

  if (base && rpcache.has(dir)) {
    return realpathChild(dir, base, rpcache, stcache, depth)
  }

  // if it's the root, then we know it's real
  if (!base) {
    rpcache.set(dir, dir)
    return Promise.resolve(dir)
  }

  // the parent, what is that?
  // find out, and then come back.
  return realpathCached(dir, rpcache, stcache, depth + 1).then(() =>
    realpathCached(path, rpcache, stcache, depth + 1))
}

const lstatCached = (path, stcache) => {
  if (stcache.has(path)) {
    return Promise.resolve(stcache.get(path))
  }

  const p = lstat(path).then(st => {
    stcache.set(path, st)
    return st
  })
  stcache.set(path, p)
  return p
}

// This is a slight fib, as it doesn't actually occur during a stat syscall.
// But file systems are giant piles of lies, so whatever.
const eloop = path =>
  Object.assign(new Error(
    `ELOOP: too many symbolic links encountered, stat '${path}'`), {
    errno: -62,
    syscall: 'stat',
    code: 'ELOOP',
    path: path,
  })

const realpathChild = (dir, base, rpcache, stcache, depth) => {
  const realdir = rpcache.get(dir)
  // that unpossible
  /* istanbul ignore next */
  if (typeof realdir === 'undefined') {
    throw new Error('in realpathChild without parent being in realpath cache')
  }

  const realish = resolve(realdir, base)
  return lstatCached(realish, stcache).then(st => {
    if (!st.isSymbolicLink()) {
      rpcache.set(resolve(dir, base), realish)
      return realish
    }

    return readlink(realish).then(target => {
      const resolved = resolve(realdir, target)
      if (realish === resolved) {
        throw eloop(realish)
      }

      return realpathCached(resolved, rpcache, stcache, depth + 1)
    }).then(real => {
      rpcache.set(resolve(dir, base), real)
      return real
    })
  })
}

module.exports = realpathCached
