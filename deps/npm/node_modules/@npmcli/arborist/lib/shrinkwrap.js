// a module that manages a shrinkwrap file (npm-shrinkwrap.json or
// package-lock.json).

// Increment whenever the lockfile version updates
// v1 - npm <=6
// v2 - arborist v1, npm v7, backwards compatible with v1, add 'packages'
// v3 will drop the 'dependencies' field, backwards comp with v2, not v1
//
// We cannot bump to v3 until npm v6 is out of common usage, and
// definitely not before npm v8.

const localeCompare = require('@isaacs/string-locale-compare')('en')
const defaultLockfileVersion = 3

// for comparing nodes to yarn.lock entries
const mismatch = (a, b) => a && b && a !== b

// this.tree => the root node for the tree (ie, same path as this)
// - Set the first time we do `this.add(node)` for a path matching this.path
//
// this.add(node) =>
// - decorate the node with the metadata we have, if we have it, and it matches
// - add to the map of nodes needing to be committed, so that subsequent
// changes are captured when we commit that location's metadata.
//
// this.commit() =>
// - commit all nodes awaiting update to their metadata entries
// - re-generate this.data and this.yarnLock based on this.tree
//
// Note that between this.add() and this.commit(), `this.data` will be out of
// date!  Always call `commit()` before relying on it.
//
// After calling this.commit(), any nodes not present in the tree will have
// been removed from the shrinkwrap data as well.

const { log } = require('proc-log')
const YarnLock = require('./yarn-lock.js')
const {
  readFile,
  readdir,
  readlink,
  rm,
  stat,
  writeFile,
} = require('node:fs/promises')

const { resolve, basename, relative } = require('node:path')
const specFromLock = require('./spec-from-lock.js')
const versionFromTgz = require('./version-from-tgz.js')
const npa = require('npm-package-arg')
const pkgJson = require('@npmcli/package-json')
const parseJSON = require('parse-conflict-json')
const nameFromFolder = require('@npmcli/name-from-folder')

const stringify = require('json-stringify-nice')
const swKeyOrder = [
  'name',
  'version',
  'lockfileVersion',
  'resolved',
  'integrity',
  'requires',
  'packages',
  'dependencies',
]

// used to rewrite from yarn registry to npm registry
const yarnRegRe = /^https?:\/\/registry\.yarnpkg\.com\//
const npmRegRe = /^https?:\/\/registry\.npmjs\.org\//

// sometimes resolved: is weird or broken, or something npa can't handle
const specFromResolved = resolved => {
  try {
    return npa(resolved)
  } catch (er) {
    return {}
  }
}

const relpath = require('./relpath.js')

const consistentResolve = require('./consistent-resolve.js')
const { overrideResolves } = require('./override-resolves.js')

const pkgMetaKeys = [
  // note: name is included if necessary, for alias packages
  'version',
  'dependencies',
  'peerDependencies',
  'peerDependenciesMeta',
  'optionalDependencies',
  'bundleDependencies',
  'acceptDependencies',
  'funding',
  'engines',
  'os',
  'cpu',
  '_integrity',
  'license',
  '_hasShrinkwrap',
  'hasInstallScript',
  'bin',
  'deprecated',
  'workspaces',
]

const nodeMetaKeys = [
  'integrity',
  'inBundle',
  'hasShrinkwrap',
  'hasInstallScript',
]

const metaFieldFromPkg = (pkg, key) => {
  const val = pkg[key]
  if (val) {
    // get only the license type, not the full object
    if (key === 'license' && typeof val === 'object' && val.type) {
      return val.type
    }
    // skip empty objects and falsey values
    if (typeof val !== 'object' || Object.keys(val).length) {
      return val
    }
  }
  return null
}

// check to make sure that there are no packages newer than or missing from the hidden lockfile
const assertNoNewer = async (path, data, lockTime, dir, seen) => {
  const base = basename(dir)
  const isNM = dir !== path && base === 'node_modules'
  const isScope = dir !== path && base.startsWith('@')
  const isParent = (dir === path) || isNM || isScope

  const parent = isParent ? dir : resolve(dir, 'node_modules')
  const rel = relpath(path, dir)
  seen.add(rel)
  let entries
  if (dir === path) {
    entries = [{ name: 'node_modules', isDirectory: () => true }]
  } else {
    const { mtime: dirTime } = await stat(dir)
    if (dirTime > lockTime) {
      throw new Error(`out of date, updated: ${rel}`)
    }
    if (!isScope && !isNM && !data.packages[rel]) {
      throw new Error(`missing from lockfile: ${rel}`)
    }
    entries = await readdir(parent, { withFileTypes: true }).catch(() => [])
  }

  // TODO limit concurrency here, this is recursive
  await Promise.all(entries.map(async dirent => {
    const child = resolve(parent, dirent.name)
    if (dirent.isDirectory() && !dirent.name.startsWith('.')) {
      await assertNoNewer(path, data, lockTime, child, seen)
    } else if (dirent.isSymbolicLink()) {
      const target = resolve(parent, await readlink(child))
      const tstat = await stat(target).catch(
        /* istanbul ignore next - windows */ () => null)
      seen.add(relpath(path, child))
      /* istanbul ignore next - windows cannot do this */
      if (tstat?.isDirectory() && !seen.has(relpath(path, target))) {
        await assertNoNewer(path, data, lockTime, target, seen)
      }
    }
  }))

  if (dir !== path) {
    return
  }

  // assert that all the entries in the lockfile were seen
  for (const loc in data.packages) {
    if (!seen.has(loc)) {
      throw new Error(`missing from node_modules: ${loc}`)
    }
  }
}

class Shrinkwrap {
  static get defaultLockfileVersion () {
    return defaultLockfileVersion
  }

  static load (options) {
    return new Shrinkwrap(options).load()
  }

  static get keyOrder () {
    return swKeyOrder
  }

  static async reset (options) {
    // still need to know if it was loaded from the disk, but don't
    // bother reading it if we're gonna just throw it away.
    const s = new Shrinkwrap(options)
    s.reset()

    const [sw, lock] = await s.resetFiles

    // XXX this is duplicated in this.load(), but using loadFiles instead of resetFiles
    if (s.hiddenLockfile) {
      s.filename = resolve(s.path, 'node_modules/.package-lock.json')
    } else if (s.shrinkwrapOnly || sw) {
      s.filename = resolve(s.path, 'npm-shrinkwrap.json')
    } else {
      s.filename = resolve(s.path, 'package-lock.json')
    }
    s.loadedFromDisk = !!(sw || lock)
    // TODO what uses this?
    s.type = basename(s.filename)

    return s
  }

  static metaFromNode (node, path, options = {}) {
    if (node.isLink) {
      return {
        resolved: relpath(path, node.realpath),
        link: true,
      }
    }

    const meta = {}
    for (const key of pkgMetaKeys) {
      const val = metaFieldFromPkg(node.package, key)
      if (val) {
        meta[key.replace(/^_/, '')] = val
      }
    }
    // we only include name if different from the node path name, and for the
    // root to help prevent churn based on the name of the directory the
    // project is in
    const pname = node.packageName
    // when Target package name and Target node share the same name, we include the name, target node should have name as per realpath.
    if (pname && (node === node.root || pname !== node.name || nameFromFolder(node.realpath) !== pname)) {
      meta.name = pname
    }

    if (node.isTop && node.package.devDependencies) {
      meta.devDependencies = node.package.devDependencies
    }

    for (const key of nodeMetaKeys) {
      if (node[key]) {
        meta[key] = node[key]
      }
    }

    const resolved = consistentResolve(node.resolved, node.path, path, true)
    // hide resolved from registry dependencies.
    if (!resolved) {
      // no-op
    } else if (node.isRegistryDependency) {
      meta.resolved = overrideResolves(resolved, options)
    } else {
      meta.resolved = resolved
    }

    if (node.extraneous) {
      meta.extraneous = true
    } else {
      if (node.peer) {
        meta.peer = true
      }
      if (node.dev) {
        meta.dev = true
      }
      if (node.optional) {
        meta.optional = true
      }
      if (node.devOptional && !node.dev && !node.optional) {
        meta.devOptional = true
      }
    }
    return meta
  }

  #awaitingUpdate = new Map()

  constructor (options = {}) {
    const {
      path,
      indent = 2,
      newline = '\n',
      shrinkwrapOnly = false,
      hiddenLockfile = false,
      lockfileVersion,
      resolveOptions = {},
    } = options

    if (hiddenLockfile) {
      this.lockfileVersion = 3
    } else if (lockfileVersion) {
      this.lockfileVersion = parseInt(lockfileVersion, 10)
    } else {
      this.lockfileVersion = null
    }

    this.tree = null
    this.path = resolve(path || '.')
    this.filename = null
    this.data = null
    this.indent = indent
    this.newline = newline
    this.loadedFromDisk = false
    this.type = null
    this.yarnLock = null
    this.hiddenLockfile = hiddenLockfile
    this.loadingError = null
    this.resolveOptions = resolveOptions
    // only load npm-shrinkwrap.json in dep trees, not package-lock
    this.shrinkwrapOnly = shrinkwrapOnly
  }

  // check to see if a spec is present in the yarn.lock file, and if so,
  // if we should use it, and what it should resolve to.  This is only
  // done when we did not load a shrinkwrap from disk.  Also, decorate
  // the options object if provided with the resolved and integrity that
  // we expect.
  checkYarnLock (spec, options = {}) {
    spec = npa(spec)
    const { yarnLock, loadedFromDisk } = this
    const useYarnLock = yarnLock && !loadedFromDisk
    const fromYarn = useYarnLock && yarnLock.entries.get(spec.raw)
    if (fromYarn && fromYarn.version) {
      // if it's the yarn or npm default registry, use the version as
      // our effective spec.  if it's any other kind of thing, use that.
      const { resolved, version, integrity } = fromYarn
      const isYarnReg = spec.registry && yarnRegRe.test(resolved)
      const isnpmReg = spec.registry && !isYarnReg && npmRegRe.test(resolved)
      const isReg = isnpmReg || isYarnReg
      // don't use the simple version if the "registry" url is
      // something else entirely!
      const tgz = isReg && versionFromTgz(spec.name, resolved) || {}
      let yspec = resolved
      if (tgz.name === spec.name && tgz.version === version) {
        yspec = version
      } else if (isReg && tgz.name && tgz.version) {
        yspec = `npm:${tgz.name}@${tgz.version}`
      }
      if (yspec) {
        options.resolved = resolved.replace(yarnRegRe, 'https://registry.npmjs.org/')
        options.integrity = integrity
        return npa(`${spec.name}@${yspec}`)
      }
    }
    return spec
  }

  // throw away the shrinkwrap data so we can start fresh
  // still worth doing a load() first so we know which files to write.
  reset () {
    this.tree = null
    this.#awaitingUpdate = new Map()
    const lockfileVersion = this.lockfileVersion || defaultLockfileVersion
    this.originalLockfileVersion = lockfileVersion

    this.data = {
      lockfileVersion,
      requires: true,
      packages: {},
      dependencies: {},
    }
  }

  // files to potentially read from and write to, in order of priority
  get #filenameSet () {
    if (this.shrinkwrapOnly) {
      return [`${this.path}/npm-shrinkwrap.json`]
    }
    if (this.hiddenLockfile) {
      return [`${this.path}/node_modules/.package-lock.json`]
    }
    return [
      `${this.path}/npm-shrinkwrap.json`,
      `${this.path}/package-lock.json`,
      `${this.path}/yarn.lock`,
    ]
  }

  get loadFiles () {
    return Promise.all(
      this.#filenameSet.map(file => file && readFile(file, 'utf8').then(d => d, er => {
        /* istanbul ignore else - can't test without breaking module itself */
        if (er.code === 'ENOENT') {
          return ''
        } else {
          throw er
        }
      }))
    )
  }

  get resetFiles () {
    // slice out yarn, we only care about lock or shrinkwrap when checking
    // this way, since we're not actually loading the full lock metadata
    return Promise.all(this.#filenameSet.slice(0, 2)
      .map(file => file && stat(file).then(st => st.isFile(), er => {
        /* istanbul ignore else - can't test without breaking module itself */
        if (er.code === 'ENOENT') {
          return null
        } else {
          throw er
        }
      })
      )
    )
  }

  inferFormattingOptions (packageJSONData) {
    const {
      [Symbol.for('indent')]: indent,
      [Symbol.for('newline')]: newline,
    } = packageJSONData
    if (indent !== undefined) {
      this.indent = indent
    }
    if (newline !== undefined) {
      this.newline = newline
    }
  }

  async load () {
    // we don't need to load package-lock.json except for top of tree nodes,
    // only npm-shrinkwrap.json.
    let data
    try {
      const [sw, lock, yarn] = await this.loadFiles
      data = sw || lock || '{}'

      // use shrinkwrap only for deps, otherwise prefer package-lock
      // and ignore npm-shrinkwrap if both are present.
      // TODO: emit a warning here or something if both are present.
      if (this.hiddenLockfile) {
        this.filename = resolve(this.path, 'node_modules/.package-lock.json')
      } else if (this.shrinkwrapOnly || sw) {
        this.filename = resolve(this.path, 'npm-shrinkwrap.json')
      } else {
        this.filename = resolve(this.path, 'package-lock.json')
      }
      this.type = basename(this.filename)
      this.loadedFromDisk = Boolean(sw || lock)

      if (yarn) {
        this.yarnLock = new YarnLock()
        // ignore invalid yarn data.  we'll likely clobber it later anyway.
        try {
          this.yarnLock.parse(yarn)
        } catch {
          // ignore errors
        }
      }

      data = parseJSON(data)
      this.inferFormattingOptions(data)

      if (this.hiddenLockfile && data.packages) {
        // add a few ms just to account for jitter
        const lockTime = +(await stat(this.filename)).mtime + 10
        await assertNoNewer(this.path, data, lockTime, this.path, new Set())
      }

      // all good!  hidden lockfile is the newest thing in here.
    } catch (er) {
      /* istanbul ignore else */
      if (typeof this.filename === 'string') {
        const rel = relpath(this.path, this.filename)
        log.verbose('shrinkwrap', `failed to load ${rel}`, er.message)
      } else {
        log.verbose('shrinkwrap', `failed to load ${this.path}`, er.message)
      }
      this.loadingError = er
      this.loadedFromDisk = false
      this.ancientLockfile = false
      data = {}
    }
    // auto convert v1 lockfiles to v3
    // leave v2 in place unless configured
    // v3 by default
    let lockfileVersion = defaultLockfileVersion
    if (this.lockfileVersion) {
      lockfileVersion = this.lockfileVersion
    } else if (data.lockfileVersion && data.lockfileVersion !== 1) {
      lockfileVersion = data.lockfileVersion
    }

    this.data = {
      ...data,
      lockfileVersion,
      requires: true,
      packages: data.packages || {},
      dependencies: data.dependencies || {},
    }

    this.originalLockfileVersion = data.lockfileVersion

    // use default if it wasn't explicitly set, and the current file is
    // less than our default.  otherwise, keep whatever is in the file,
    // unless we had an explicit setting already.
    if (!this.lockfileVersion) {
      this.lockfileVersion = this.data.lockfileVersion = lockfileVersion
    }
    this.ancientLockfile = this.loadedFromDisk &&
      !(data.lockfileVersion >= 2) && !data.requires

    // load old lockfile deps into the packages listing
    if (data.dependencies && !data.packages) {
      let pkg
      try {
        pkg = await pkgJson.normalize(this.path)
        pkg = pkg.content
      } catch {
        pkg = {}
      }
      this.#loadAll('', null, this.data)
      this.#fixDependencies(pkg)
    }
    return this
  }

  #loadAll (location, name, lock) {
    // migrate a v1 package lock to the new format.
    const meta = this.#metaFromLock(location, name, lock)
    // dependencies nested under a link are actually under the link target
    if (meta.link) {
      location = meta.resolved
    }
    if (lock.dependencies) {
      for (const name in lock.dependencies) {
        const loc = location + (location ? '/' : '') + 'node_modules/' + name
        this.#loadAll(loc, name, lock.dependencies[name])
      }
    }
  }

  // v1 lockfiles track the optional/dev flags, but they don't tell us
  // which thing had what kind of dep on what other thing, so we need
  // to correct that now, or every link will be considered prod
  #fixDependencies (pkg) {
    // we need the root package.json because legacy shrinkwraps just
    // have requires:true at the root level, which is even less useful
    // than merging all dep types into one object.
    const root = this.data.packages['']
    for (const key of pkgMetaKeys) {
      const val = metaFieldFromPkg(pkg, key)
      if (val) {
        root[key.replace(/^_/, '')] = val
      }
    }

    for (const loc in this.data.packages) {
      const meta = this.data.packages[loc]
      if (!meta.requires || !loc) {
        continue
      }

      // resolve each require to a meta entry
      // if this node isn't optional, but the dep is, then it's an optionalDep
      // likewise for dev deps.
      // This isn't perfect, but it's a pretty good approximation, and at
      // least gets us out of having all 'prod' edges, which throws off the
      // buildIdealTree process
      for (const name in meta.requires) {
        const dep = this.#resolveMetaNode(loc, name)
        // this overwrites the false value set above
        // default to dependencies if the dep just isn't in the tree, which
        // maybe should be an error, since it means that the shrinkwrap is
        // invalid, but we can't do much better without any info.
        let depType = 'dependencies'
        /* istanbul ignore else - dev deps are only for the root level */
        if (dep?.optional && !meta.optional) {
          depType = 'optionalDependencies'
        } else if (dep?.dev && !meta.dev) {
          // XXX is this even reachable?
          depType = 'devDependencies'
        }
        if (!meta[depType]) {
          meta[depType] = {}
        }
        meta[depType][name] = meta.requires[name]
      }
      delete meta.requires
    }
  }

  #resolveMetaNode (loc, name) {
    for (let path = loc; true; path = path.replace(/(^|\/)[^/]*$/, '')) {
      const check = `${path}${path ? '/' : ''}node_modules/${name}`
      if (this.data.packages[check]) {
        return this.data.packages[check]
      }

      if (!path) {
        break
      }
    }
    return null
  }

  #lockFromLoc (lock, path, i = 0) {
    if (!lock) {
      return null
    }

    if (path[i] === '') {
      i++
    }

    if (i >= path.length) {
      return lock
    }

    if (!lock.dependencies) {
      return null
    }

    return this.#lockFromLoc(lock.dependencies[path[i]], path, i + 1)
  }

  // pass in a path relative to the root path, or an absolute path,
  // get back a /-normalized location based on root path.
  #pathToLoc (path) {
    return relpath(this.path, resolve(this.path, path))
  }

  delete (nodePath) {
    if (!this.data) {
      throw new Error('run load() before getting or setting data')
    }
    const location = this.#pathToLoc(nodePath)
    this.#awaitingUpdate.delete(location)

    delete this.data.packages[location]
    const path = location.split(/(?:^|\/)node_modules\//)
    const name = path.pop()
    const pLock = this.#lockFromLoc(this.data, path)
    if (pLock && pLock.dependencies) {
      delete pLock.dependencies[name]
    }
  }

  get (nodePath) {
    if (!this.data) {
      throw new Error('run load() before getting or setting data')
    }

    const location = this.#pathToLoc(nodePath)
    if (this.#awaitingUpdate.has(location)) {
      this.#updateWaitingNode(location)
    }

    // first try to get from the newer spot, which we know has
    // all the things we need.
    if (this.data.packages[location]) {
      return this.data.packages[location]
    }

    // otherwise, fall back to the legacy metadata, and hope for the best
    // get the node in the shrinkwrap corresponding to this spot
    const path = location.split(/(?:^|\/)node_modules\//)
    const name = path[path.length - 1]
    const lock = this.#lockFromLoc(this.data, path)

    return this.#metaFromLock(location, name, lock)
  }

  #metaFromLock (location, name, lock) {
    // This function tries as hard as it can to figure out the metadata
    // from a lockfile which may be outdated or incomplete.  Since v1
    // lockfiles used the "version" field to contain a variety of
    // different possible types of data, this gets a little complicated.
    if (!lock) {
      return {}
    }

    // try to figure out a npm-package-arg spec from the lockfile entry
    // This will return null if we could not get anything valid out of it.
    const spec = specFromLock(name, lock, this.path)

    if (spec.type === 'directory') {
      // the "version" was a file: url to a non-tarball path
      // this is a symlink dep.  We don't store much metadata
      // about symlinks, just the target.
      const target = relpath(this.path, spec.fetchSpec)
      this.data.packages[location] = {
        link: true,
        resolved: target,
      }
      // also save the link target, omitting version since we don't know
      // what it is, but we know it isn't a link to itself!
      if (!this.data.packages[target]) {
        this.#metaFromLock(target, name, { ...lock, version: null })
      }
      return this.data.packages[location]
    }

    const meta = {}
    // when calling loadAll we'll change these into proper dep objects
    if (lock.requires && typeof lock.requires === 'object') {
      meta.requires = lock.requires
    }

    if (lock.optional) {
      meta.optional = true
    }
    if (lock.dev) {
      meta.dev = true
    }

    // the root will typically have a name from the root project's
    // package.json file.
    if (location === '') {
      meta.name = lock.name
    }

    // if we have integrity, save it now.
    if (lock.integrity) {
      meta.integrity = lock.integrity
    }

    if (lock.version && !lock.integrity) {
      // this is usually going to be a git url or symlink, but it could
      // also be a registry dependency that did not have integrity at
      // the time it was saved.
      // Symlinks were already handled above, so that leaves git.
      //
      // For git, always save the full SSH url.  we'll actually fetch the
      // tgz most of the time, since it's faster, but it won't work for
      // private repos, and we can't get back to the ssh from the tgz,
      // so we store the ssh instead.
      // For unknown git hosts, just resolve to the raw spec in lock.version
      if (spec.type === 'git') {
        meta.resolved = consistentResolve(spec, this.path, this.path)

        // return early because there is nothing else we can do with this
        return this.data.packages[location] = meta
      } else if (spec.registry) {
        // registry dep that didn't save integrity.  grab the version, and
        // fall through to pick up the resolved and potentially name.
        meta.version = lock.version
      }
      // only other possible case is a tarball without integrity.
      // fall through to do what we can with the filename later.
    }

    // at this point, we know that the spec is either a registry dep
    // (ie, version, because locking, which means a resolved url),
    // or a remote dep, or file: url.  Remote deps and file urls
    // have a fetchSpec equal to the fully resolved thing.
    // Registry deps, we take what's in the lockfile.
    if (lock.resolved || (spec.type && !spec.registry)) {
      if (spec.registry) {
        meta.resolved = lock.resolved
      } else if (spec.type === 'file') {
        meta.resolved = consistentResolve(spec, this.path, this.path, true)
      } else if (spec.fetchSpec) {
        meta.resolved = spec.fetchSpec
      }
    }

    // at this point, if still we don't have a version, do our best to
    // infer it from the tarball url/file.  This works a surprising
    // amount of the time, even though it's not guaranteed.
    if (!meta.version) {
      if (spec.type === 'file' || spec.type === 'remote') {
        const fromTgz = versionFromTgz(spec.name, spec.fetchSpec) ||
          versionFromTgz(spec.name, meta.resolved)
        if (fromTgz) {
          meta.version = fromTgz.version
          if (fromTgz.name !== name) {
            meta.name = fromTgz.name
          }
        }
      } else if (spec.type === 'alias') {
        meta.name = spec.subSpec.name
        meta.version = spec.subSpec.fetchSpec
      } else if (spec.type === 'version') {
        meta.version = spec.fetchSpec
      }
      // ok, I did my best!  good luck!
    }

    if (lock.bundled) {
      meta.inBundle = true
    }

    // save it for next time
    return this.data.packages[location] = meta
  }

  add (node) {
    if (!this.data) {
      throw new Error('run load() before getting or setting data')
    }

    // will be actually updated on read
    const loc = relpath(this.path, node.path)
    if (node.path === this.path) {
      this.tree = node
    }

    // if we have metadata about this node, and it's a match, then
    // try to decorate it.
    if (node.resolved === null || node.integrity === null) {
      const {
        resolved,
        integrity,
        hasShrinkwrap,
        version,
      } = this.get(node.path)

      let pathFixed = null
      if (resolved) {
        if (!/^file:/.test(resolved)) {
          pathFixed = resolved
        } else {
          pathFixed = `file:${resolve(this.path, resolved.slice(5)).replace(/#/g, '%23')}`
        }
      }

      // if we have one, only set the other if it matches
      // otherwise it could be for a completely different thing.
      const resolvedOk = !resolved || !node.resolved ||
        node.resolved === pathFixed
      const integrityOk = !integrity || !node.integrity ||
        node.integrity === integrity
      const versionOk = !version || !node.version || version === node.version

      const allOk = (resolved || integrity || version) &&
        resolvedOk && integrityOk && versionOk

      if (allOk) {
        node.resolved = node.resolved || pathFixed || null
        node.integrity = node.integrity || integrity || null
        node.hasShrinkwrap = node.hasShrinkwrap || hasShrinkwrap || false
      } else {
        // try to read off the package or node itself
        const {
          resolved,
          integrity,
          hasShrinkwrap,
        } = Shrinkwrap.metaFromNode(node, this.path, this.resolveOptions)
        node.resolved = node.resolved || resolved || null
        node.integrity = node.integrity || integrity || null
        node.hasShrinkwrap = node.hasShrinkwrap || hasShrinkwrap || false
      }
    }
    this.#awaitingUpdate.set(loc, node)
  }

  addEdge (edge) {
    if (!this.yarnLock || !edge.valid) {
      return
    }

    const { to: node } = edge

    // if it's already set up, nothing to do
    if (node.resolved !== null && node.integrity !== null) {
      return
    }

    // if the yarn lock is empty, nothing to do
    if (!this.yarnLock.entries || !this.yarnLock.entries.size) {
      return
    }

    // we relativize the path here because that's how it shows up in the lock
    // XXX why is this different from pathFixed in this.add??
    let pathFixed = null
    if (node.resolved) {
      if (!/file:/.test(node.resolved)) {
        pathFixed = node.resolved
      } else {
        pathFixed = consistentResolve(node.resolved, node.path, this.path, true)
      }
    }

    const spec = npa(`${node.name}@${edge.spec}`)
    const entry = this.yarnLock.entries.get(`${node.name}@${edge.spec}`)

    if (!entry ||
        mismatch(node.version, entry.version) ||
        mismatch(node.integrity, entry.integrity) ||
        mismatch(pathFixed, entry.resolved)) {
      return
    }

    if (entry.resolved && yarnRegRe.test(entry.resolved) && spec.registry) {
      entry.resolved = entry.resolved.replace(yarnRegRe, 'https://registry.npmjs.org/')
    }

    node.integrity = node.integrity || entry.integrity || null
    node.resolved = node.resolved ||
      consistentResolve(entry.resolved, this.path, node.path) || null

    this.#awaitingUpdate.set(relpath(this.path, node.path), node)
  }

  #updateWaitingNode (loc) {
    const node = this.#awaitingUpdate.get(loc)
    this.#awaitingUpdate.delete(loc)
    this.data.packages[loc] = Shrinkwrap.metaFromNode(
      node,
      this.path,
      this.resolveOptions)
  }

  commit () {
    if (this.tree) {
      if (this.yarnLock) {
        this.yarnLock.fromTree(this.tree)
      }
      const root = Shrinkwrap.metaFromNode(
        this.tree.target,
        this.path,
        this.resolveOptions)
      this.data.packages = {}
      if (Object.keys(root).length) {
        this.data.packages[''] = root
      }
      for (const node of this.tree.root.inventory.values()) {
        // only way this.tree is not root is if the root is a link to it
        if (node === this.tree || node.isRoot || node.location === '') {
          continue
        }
        const loc = relpath(this.path, node.path)
        this.data.packages[loc] = Shrinkwrap.metaFromNode(
          node,
          this.path,
          this.resolveOptions)
      }
    } else if (this.#awaitingUpdate.size > 0) {
      for (const loc of this.#awaitingUpdate.keys()) {
        this.#updateWaitingNode(loc)
      }
    }

    // if we haven't set it by now, use the default
    if (!this.lockfileVersion) {
      this.lockfileVersion = defaultLockfileVersion
    }
    this.data.lockfileVersion = this.lockfileVersion

    // hidden lockfiles don't include legacy metadata or a root entry
    if (this.hiddenLockfile) {
      delete this.data.packages['']
      delete this.data.dependencies
    } else if (this.tree && this.lockfileVersion <= 3) {
      this.#buildLegacyLockfile(this.tree, this.data)
    }

    // lf version 1 = dependencies only
    // lf version 2 = dependencies and packages
    // lf version 3 = packages only
    if (this.lockfileVersion >= 3) {
      const { dependencies, ...data } = this.data
      return data
    } else if (this.lockfileVersion < 2) {
      const { packages, ...data } = this.data
      return data
    } else {
      return { ...this.data }
    }
  }

  #buildLegacyLockfile (node, lock, path = []) {
    if (node === this.tree) {
      // the root node
      lock.name = node.packageName || node.name
      if (node.version) {
        lock.version = node.version
      }
    }

    // npm v6 and before tracked 'from', meaning "the request that led
    // to this package being installed".  However, that's inherently
    // racey and non-deterministic in a world where deps are deduped
    // ahead of fetch time.  In order to maintain backwards compatibility
    // with v6 in the lockfile, we do this trick where we pick a valid
    // dep link out of the edgesIn set.  Choose the edge with the fewest
    // number of `node_modules` sections in the requestor path, and then
    // lexically sort afterwards.
    const edge = [...node.edgesIn].filter(e => e.valid).sort((a, b) => {
      const aloc = a.from.location.split('node_modules')
      const bloc = b.from.location.split('node_modules')
      /* istanbul ignore next - sort calling order is indeterminate */
      if (aloc.length > bloc.length) {
        return 1
      }
      if (bloc.length > aloc.length) {
        return -1
      }
      return localeCompare(aloc[aloc.length - 1], bloc[bloc.length - 1])
    })[0]

    const res = consistentResolve(node.resolved, this.path, this.path, true)
    const rSpec = specFromResolved(res)

    // if we don't have anything (ie, it's extraneous) then use the resolved
    // value as if that was where we got it from, since at least it's true.
    // if we don't have either, just an empty object so nothing matches below.
    // This will effectively just save the version and resolved, as if it's
    // a standard version/range dep, which is a reasonable default.
    let spec = rSpec
    if (edge) {
      spec = npa.resolve(node.name, edge.spec, edge.from.realpath)
    }

    if (node.isLink) {
      lock.version = `file:${relpath(this.path, node.realpath).replace(/#/g, '%23')}`
    } else if (spec && (spec.type === 'file' || spec.type === 'remote')) {
      lock.version = spec.saveSpec
    } else if (spec && spec.type === 'git' || rSpec.type === 'git') {
      lock.version = node.resolved
      /* istanbul ignore else - don't think there are any cases where a git
       * spec (or indeed, ANY npa spec) doesn't have a .raw member */
      if (spec.raw) {
        lock.from = spec.raw
      }
    } else if (!node.isRoot &&
        node.package &&
        node.packageName &&
        node.packageName !== node.name) {
      lock.version = `npm:${node.packageName}@${node.version}`
    } else if (node.package && node.version) {
      lock.version = node.version
    }

    if (node.inDepBundle) {
      lock.bundled = true
    }

    // when we didn't resolve to git, file, or dir, and didn't request
    // git, file, dir, or remote, then the resolved value is necessary.
    if (node.resolved &&
        !node.isLink &&
        rSpec.type !== 'git' &&
        rSpec.type !== 'file' &&
        rSpec.type !== 'directory' &&
        spec.type !== 'directory' &&
        spec.type !== 'git' &&
        spec.type !== 'file' &&
        spec.type !== 'remote') {
      lock.resolved = overrideResolves(node.resolved, this.resolveOptions)
    }

    if (node.integrity) {
      lock.integrity = node.integrity
    }

    if (node.extraneous) {
      lock.extraneous = true
    } else if (!node.isLink) {
      if (node.peer) {
        lock.peer = true
      }

      if (node.devOptional && !node.dev && !node.optional) {
        lock.devOptional = true
      }

      if (node.dev) {
        lock.dev = true
      }

      if (node.optional) {
        lock.optional = true
      }
    }

    const depender = node.target
    if (depender.edgesOut.size > 0) {
      if (node !== this.tree) {
        const entries = [...depender.edgesOut.entries()]
        lock.requires = entries.reduce((set, [k, v]) => {
          // omit peer deps from legacy lockfile requires field, because
          // npm v6 doesn't handle peer deps, and this triggers some bad
          // behavior if the dep can't be found in the dependencies list.
          const { spec, peer } = v
          if (peer) {
            return set
          }
          if (spec.startsWith('file:')) {
            // turn absolute file: paths into relative paths from the node
            // this especially shows up with workspace edges when the root
            // node is also a workspace in the set.
            const p = resolve(node.realpath, spec.slice('file:'.length))
            set[k] = `file:${relpath(node.realpath, p).replace(/#/g, '%23')}`
          } else {
            set[k] = spec
          }
          return set
        }, {})
      } else {
        lock.requires = true
      }
    }

    // now we walk the children, putting them in the 'dependencies' object
    const { children } = node.target
    if (!children.size) {
      delete lock.dependencies
    } else {
      const kidPath = [...path, node.realpath]
      const dependencies = {}
      // skip any that are already in the descent path, so cyclical link
      // dependencies don't blow up with ELOOP.
      let found = false
      for (const [name, kid] of children.entries()) {
        if (path.includes(kid.realpath)) {
          continue
        }
        dependencies[name] = this.#buildLegacyLockfile(kid, {}, kidPath)
        found = true
      }
      if (found) {
        lock.dependencies = dependencies
      }
    }
    return lock
  }

  toJSON () {
    if (!this.data) {
      throw new Error('run load() before getting or setting data')
    }

    return this.commit()
  }

  toString (options = {}) {
    const data = this.toJSON()
    const { format = true } = options
    const defaultIndent = this.indent || 2
    const indent = format === true ? defaultIndent
      : format || 0
    const eol = format ? this.newline || '\n' : ''
    return stringify(data, swKeyOrder, indent).replace(/\n/g, eol)
  }

  save (options = {}) {
    if (!this.data) {
      throw new Error('run load() before saving data')
    }

    // This must be called before the lockfile conversion check below since it sets properties as part of `commit()`
    const json = this.toString(options)
    if (
      !this.hiddenLockfile
      && this.originalLockfileVersion !== undefined
      && this.originalLockfileVersion !== this.lockfileVersion
    ) {
      log.warn(
        'shrinkwrap',
        `Converting lock file (${relative(process.cwd(), this.filename)}) from v${this.originalLockfileVersion} -> v${this.lockfileVersion}`
      )
    }

    return Promise.all([
      writeFile(this.filename, json).catch(er => {
        if (this.hiddenLockfile) {
          // well, we did our best.
          // if we reify, and there's nothing there, then it might be lacking
          // a node_modules folder, but then the lockfile is not important.
          // Remove the file, so that in case there WERE deps, but we just
          // failed to update the file for some reason, it's not out of sync.
          return rm(this.filename, { recursive: true, force: true })
        }
        throw er
      }),
      this.yarnLock && this.yarnLock.entries.size &&
        writeFile(this.path + '/yarn.lock', this.yarnLock.toString()),
    ])
  }
}

module.exports = Shrinkwrap
