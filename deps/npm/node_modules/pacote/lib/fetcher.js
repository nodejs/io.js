// This is the base class that the other fetcher types in lib
// all descend from.
// It handles the unpacking and retry logic that is shared among
// all of the other Fetcher types.

const { basename, dirname } = require('node:path')
const { rm, mkdir } = require('node:fs/promises')
const PackageJson = require('@npmcli/package-json')
const cacache = require('cacache')
const fsm = require('fs-minipass')
const getContents = require('@npmcli/installed-package-contents')
const npa = require('npm-package-arg')
const retry = require('promise-retry')
const ssri = require('ssri')
const tar = require('tar')
const { Minipass } = require('minipass')
const { log } = require('proc-log')
const _ = require('./util/protected.js')
const cacheDir = require('./util/cache-dir.js')
const isPackageBin = require('./util/is-package-bin.js')
const removeTrailingSlashes = require('./util/trailing-slashes.js')

// Pacote is only concerned with the package.json contents
const packageJsonPrepare = (p) => PackageJson.prepare(p).then(pkg => pkg.content)
const packageJsonNormalize = (p) => PackageJson.normalize(p).then(pkg => pkg.content)

class FetcherBase {
  constructor (spec, opts) {
    if (!opts || typeof opts !== 'object') {
      throw new TypeError('options object is required')
    }
    this.spec = npa(spec, opts.where)

    this.allowGitIgnore = !!opts.allowGitIgnore

    // a bit redundant because presumably the caller already knows this,
    // but it makes it easier to not have to keep track of the requested
    // spec when we're dispatching thousands of these at once, and normalizing
    // is nice.  saveSpec is preferred if set, because it turns stuff like
    // x/y#committish into github:x/y#committish.  use name@rawSpec for
    // registry deps so that we turn xyz and xyz@ -> xyz@
    this.from = this.spec.registry
      ? `${this.spec.name}@${this.spec.rawSpec}` : this.spec.saveSpec

    this.#assertType()
    // clone the opts object so that others aren't upset when we mutate it
    // by adding/modifying the integrity value.
    this.opts = { ...opts }

    this.cache = opts.cache || cacheDir().cacache
    this.tufCache = opts.tufCache || cacheDir().tufcache
    this.resolved = opts.resolved || null

    // default to caching/verifying with sha512, that's what we usually have
    // need to change this default, or start overriding it, when sha512
    // is no longer strong enough.
    this.defaultIntegrityAlgorithm = opts.defaultIntegrityAlgorithm || 'sha512'

    if (typeof opts.integrity === 'string') {
      this.opts.integrity = ssri.parse(opts.integrity)
    }

    this.package = null
    this.type = this.constructor.name
    this.fmode = opts.fmode || 0o666
    this.dmode = opts.dmode || 0o777
    // we don't need a default umask, because we don't chmod files coming
    // out of package tarballs.  they're forced to have a mode that is
    // valid, regardless of what's in the tarball entry, and then we let
    // the process's umask setting do its job.  but if configured, we do
    // respect it.
    this.umask = opts.umask || 0

    this.preferOnline = !!opts.preferOnline
    this.preferOffline = !!opts.preferOffline
    this.offline = !!opts.offline

    this.before = opts.before
    this.fullMetadata = this.before ? true : !!opts.fullMetadata
    this.fullReadJson = !!opts.fullReadJson
    this[_.readPackageJson] = this.fullReadJson
      ? packageJsonPrepare
      : packageJsonNormalize

    // rrh is a registry hostname or 'never' or 'always'
    // defaults to registry.npmjs.org
    this.replaceRegistryHost = (!opts.replaceRegistryHost || opts.replaceRegistryHost === 'npmjs') ?
      'registry.npmjs.org' : opts.replaceRegistryHost

    this.defaultTag = opts.defaultTag || 'latest'
    this.registry = removeTrailingSlashes(opts.registry || 'https://registry.npmjs.org')

    // command to run 'prepare' scripts on directories and git dirs
    // To use pacote with yarn, for example, set npmBin to 'yarn'
    // and npmCliConfig with yarn's equivalents.
    this.npmBin = opts.npmBin || 'npm'

    // command to install deps for preparing
    this.npmInstallCmd = opts.npmInstallCmd || ['install', '--force']

    // XXX fill more of this in based on what we know from this.opts
    // we explicitly DO NOT fill in --tag, though, since we are often
    // going to be packing in the context of a publish, which may set
    // a dist-tag, but certainly wants to keep defaulting to latest.
    this.npmCliConfig = opts.npmCliConfig || [
      `--cache=${dirname(this.cache)}`,
      `--prefer-offline=${!!this.preferOffline}`,
      `--prefer-online=${!!this.preferOnline}`,
      `--offline=${!!this.offline}`,
      ...(this.before ? [`--before=${this.before.toISOString()}`] : []),
      '--no-progress',
      '--no-save',
      '--no-audit',
      // override any omit settings from the environment
      '--include=dev',
      '--include=peer',
      '--include=optional',
      // we need the actual things, not just the lockfile
      '--no-package-lock-only',
      '--no-dry-run',
    ]
  }

  get integrity () {
    return this.opts.integrity || null
  }

  set integrity (i) {
    if (!i) {
      return
    }

    i = ssri.parse(i)
    const current = this.opts.integrity

    // do not ever update an existing hash value, but do
    // merge in NEW algos and hashes that we don't already have.
    if (current) {
      current.merge(i)
    } else {
      this.opts.integrity = i
    }
  }

  get notImplementedError () {
    return new Error('not implemented in this fetcher type: ' + this.type)
  }

  // override in child classes
  // Returns a Promise that resolves to this.resolved string value
  resolve () {
    return this.resolved ? Promise.resolve(this.resolved)
      : Promise.reject(this.notImplementedError)
  }

  packument () {
    return Promise.reject(this.notImplementedError)
  }

  // override in child class
  // returns a manifest containing:
  // - name
  // - version
  // - _resolved
  // - _integrity
  // - plus whatever else was in there (corgi, full metadata, or pj file)
  manifest () {
    return Promise.reject(this.notImplementedError)
  }

  // private, should be overridden.
  // Note that they should *not* calculate or check integrity or cache,
  // but *just*  return the raw tarball data stream.
  [_.tarballFromResolved] () {
    throw this.notImplementedError
  }

  // public, should not be overridden
  tarball () {
    return this.tarballStream(stream => stream.concat().then(data => {
      data.integrity = this.integrity && String(this.integrity)
      data.resolved = this.resolved
      data.from = this.from
      return data
    }))
  }

  // private
  // Note: cacache will raise a EINTEGRITY error if the integrity doesn't match
  #tarballFromCache () {
    return cacache.get.stream.byDigest(this.cache, this.integrity, this.opts)
  }

  get [_.cacheFetches] () {
    return true
  }

  #istream (stream) {
    // if not caching this, just return it
    if (!this.opts.cache || !this[_.cacheFetches]) {
      // instead of creating a new integrity stream, we only piggyback on the
      // provided stream's events
      if (stream.hasIntegrityEmitter) {
        stream.on('integrity', i => this.integrity = i)
        return stream
      }

      const istream = ssri.integrityStream(this.opts)
      istream.on('integrity', i => this.integrity = i)
      stream.on('error', err => istream.emit('error', err))
      return stream.pipe(istream)
    }

    // we have to return a stream that gets ALL the data, and proxies errors,
    // but then pipe from the original tarball stream into the cache as well.
    // To do this without losing any data, and since the cacache put stream
    // is not a passthrough, we have to pipe from the original stream into
    // the cache AFTER we pipe into the middleStream.  Since the cache stream
    // has an asynchronous flush to write its contents to disk, we need to
    // defer the middleStream end until the cache stream ends.
    const middleStream = new Minipass()
    stream.on('error', err => middleStream.emit('error', err))
    stream.pipe(middleStream, { end: false })
    const cstream = cacache.put.stream(
      this.opts.cache,
      `pacote:tarball:${this.from}`,
      this.opts
    )
    cstream.on('integrity', i => this.integrity = i)
    cstream.on('error', err => stream.emit('error', err))
    stream.pipe(cstream)

    // eslint-disable-next-line promise/catch-or-return
    cstream.promise().catch(() => {}).then(() => middleStream.end())
    return middleStream
  }

  pickIntegrityAlgorithm () {
    return this.integrity ? this.integrity.pickAlgorithm(this.opts)
      : this.defaultIntegrityAlgorithm
  }

  // TODO: check error class, once those are rolled out to our deps
  isDataCorruptionError (er) {
    return er.code === 'EINTEGRITY' || er.code === 'Z_DATA_ERROR'
  }

  // override the types getter
  get types () {
    return false
  }

  #assertType () {
    if (this.types && !this.types.includes(this.spec.type)) {
      throw new TypeError(`Wrong spec type (${
        this.spec.type
      }) for ${
        this.constructor.name
      }. Supported types: ${this.types.join(', ')}`)
    }
  }

  // We allow ENOENTs from cacache, but not anywhere else.
  // An ENOENT trying to read a tgz file, for example, is Right Out.
  isRetriableError (er) {
    // TODO: check error class, once those are rolled out to our deps
    return this.isDataCorruptionError(er) ||
      er.code === 'ENOENT' ||
      er.code === 'EISDIR'
  }

  // Mostly internal, but has some uses
  // Pass in a function which returns a promise
  // Function will be called 1 or more times with streams that may fail.
  // Retries:
  // Function MUST handle errors on the stream by rejecting the promise,
  // so that retry logic can pick it up and either retry or fail whatever
  // promise it was making (ie, failing extraction, etc.)
  //
  // The return value of this method is a Promise that resolves the same
  // as whatever the streamHandler resolves to.
  //
  // This should never be overridden by child classes, but it is public.
  tarballStream (streamHandler) {
    // Only short-circuit via cache if we have everything else we'll need,
    // and the user has not expressed a preference for checking online.

    const fromCache = (
      !this.preferOnline &&
      this.integrity &&
      this.resolved
    ) ? streamHandler(this.#tarballFromCache()).catch(er => {
        if (this.isDataCorruptionError(er)) {
          log.warn('tarball', `cached data for ${
          this.spec
        } (${this.integrity}) seems to be corrupted. Refreshing cache.`)
          return this.cleanupCached().then(() => {
            throw er
          })
        } else {
          throw er
        }
      }) : null

    const fromResolved = er => {
      if (er) {
        if (!this.isRetriableError(er)) {
          throw er
        }
        log.silly('tarball', `no local data for ${
          this.spec
        }. Extracting by manifest.`)
      }
      return this.resolve().then(() => retry(tryAgain =>
        streamHandler(this.#istream(this[_.tarballFromResolved]()))
          .catch(streamErr => {
          // Most likely data integrity.  A cache ENOENT error is unlikely
          // here, since we're definitely not reading from the cache, but it
          // IS possible that the fetch subsystem accessed the cache, and the
          // entry got blown away or something.  Try one more time to be sure.
            if (this.isRetriableError(streamErr)) {
              log.warn('tarball', `tarball data for ${
              this.spec
            } (${this.integrity}) seems to be corrupted. Trying again.`)
              return this.cleanupCached().then(() => tryAgain(streamErr))
            }
            throw streamErr
          }), { retries: 1, minTimeout: 0, maxTimeout: 0 }))
    }

    return fromCache ? fromCache.catch(fromResolved) : fromResolved()
  }

  cleanupCached () {
    return cacache.rm.content(this.cache, this.integrity, this.opts)
  }

  #empty (path) {
    return getContents({ path, depth: 1 }).then(contents => Promise.all(
      contents.map(entry => rm(entry, { recursive: true, force: true }))))
  }

  async #mkdir (dest) {
    await this.#empty(dest)
    return await mkdir(dest, { recursive: true })
  }

  // extraction is always the same.  the only difference is where
  // the tarball comes from.
  async extract (dest) {
    await this.#mkdir(dest)
    return this.tarballStream((tarball) => this.#extract(dest, tarball))
  }

  #toFile (dest) {
    return this.tarballStream(str => new Promise((res, rej) => {
      const writer = new fsm.WriteStream(dest)
      str.on('error', er => writer.emit('error', er))
      writer.on('error', er => rej(er))
      writer.on('close', () => res({
        integrity: this.integrity && String(this.integrity),
        resolved: this.resolved,
        from: this.from,
      }))
      str.pipe(writer)
    }))
  }

  // don't use this.#mkdir because we don't want to rimraf anything
  async tarballFile (dest) {
    const dir = dirname(dest)
    await mkdir(dir, { recursive: true })
    return this.#toFile(dest)
  }

  #extract (dest, tarball) {
    const extractor = tar.x(this.#tarxOptions({ cwd: dest }))
    const p = new Promise((resolve, reject) => {
      extractor.on('end', () => {
        resolve({
          resolved: this.resolved,
          integrity: this.integrity && String(this.integrity),
          from: this.from,
        })
      })

      extractor.on('error', er => {
        log.warn('tar', er.message)
        log.silly('tar', er)
        reject(er)
      })

      tarball.on('error', er => reject(er))
    })

    tarball.pipe(extractor)
    return p
  }

  // always ensure that entries are at least as permissive as our configured
  // dmode/fmode, but never more permissive than the umask allows.
  #entryMode (path, mode, type) {
    const m = /Directory|GNUDumpDir/.test(type) ? this.dmode
      : /File$/.test(type) ? this.fmode
      : /* istanbul ignore next - should never happen in a pkg */ 0

    // make sure package bins are executable
    const exe = isPackageBin(this.package, path) ? 0o111 : 0
    // always ensure that files are read/writable by the owner
    return ((mode | m) & ~this.umask) | exe | 0o600
  }

  #tarxOptions ({ cwd }) {
    const sawIgnores = new Set()
    return {
      cwd,
      noChmod: true,
      noMtime: true,
      filter: (name, entry) => {
        if (/Link$/.test(entry.type)) {
          return false
        }
        entry.mode = this.#entryMode(entry.path, entry.mode, entry.type)
        // this replicates the npm pack behavior where .gitignore files
        // are treated like .npmignore files, but only if a .npmignore
        // file is not present.
        if (/File$/.test(entry.type)) {
          const base = basename(entry.path)
          if (base === '.npmignore') {
            sawIgnores.add(entry.path)
          } else if (base === '.gitignore' && !this.allowGitIgnore) {
            // rename, but only if there's not already a .npmignore
            const ni = entry.path.replace(/\.gitignore$/, '.npmignore')
            if (sawIgnores.has(ni)) {
              return false
            }
            entry.path = ni
          }
          return true
        }
      },
      strip: 1,
      onwarn: /* istanbul ignore next - we can trust that tar logs */
      (code, msg, data) => {
        log.warn('tar', code, msg)
        log.silly('tar', code, msg, data)
      },
      umask: this.umask,
      // always ignore ownership info from tarball metadata
      preserveOwner: false,
    }
  }
}

module.exports = FetcherBase

// Child classes
const GitFetcher = require('./git.js')
const RegistryFetcher = require('./registry.js')
const FileFetcher = require('./file.js')
const DirFetcher = require('./dir.js')
const RemoteFetcher = require('./remote.js')

// Get an appropriate fetcher object from a spec and options
FetcherBase.get = (rawSpec, opts = {}) => {
  const spec = npa(rawSpec, opts.where)
  switch (spec.type) {
    case 'git':
      return new GitFetcher(spec, opts)

    case 'remote':
      return new RemoteFetcher(spec, opts)

    case 'version':
    case 'range':
    case 'tag':
    case 'alias':
      return new RegistryFetcher(spec.subSpec || spec, opts)

    case 'file':
      return new FileFetcher(spec, opts)

    case 'directory':
      return new DirFetcher(spec, opts)

    default:
      throw new TypeError('Unknown spec type: ' + spec.type)
  }
}
