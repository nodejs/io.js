const { resolve } = require('node:path')
const packlist = require('npm-packlist')
const runScript = require('@npmcli/run-script')
const tar = require('tar')
const { Minipass } = require('minipass')
const Fetcher = require('./fetcher.js')
const FileFetcher = require('./file.js')
const _ = require('./util/protected.js')
const tarCreateOptions = require('./util/tar-create-options.js')

class DirFetcher extends Fetcher {
  constructor (spec, opts) {
    super(spec, opts)
    // just the fully resolved filename
    this.resolved = this.spec.fetchSpec

    this.tree = opts.tree || null
    this.Arborist = opts.Arborist || null
  }

  // exposes tarCreateOptions as public API
  static tarCreateOptions (manifest) {
    return tarCreateOptions(manifest)
  }

  get types () {
    return ['directory']
  }

  #prepareDir () {
    return this.manifest().then(mani => {
      if (!mani.scripts || !mani.scripts.prepare) {
        return
      }

      // we *only* run prepare.
      // pre/post-pack is run by the npm CLI for publish and pack,
      // but this function is *also* run when installing git deps
      const stdio = this.opts.foregroundScripts ? 'inherit' : 'pipe'

      return runScript({
        pkg: mani,
        event: 'prepare',
        path: this.resolved,
        stdio,
        env: {
          npm_package_resolved: this.resolved,
          npm_package_integrity: this.integrity,
          npm_package_json: resolve(this.resolved, 'package.json'),
        },
      })
    })
  }

  [_.tarballFromResolved] () {
    if (!this.tree && !this.Arborist) {
      throw new Error('DirFetcher requires either a tree or an Arborist constructor to pack')
    }

    const stream = new Minipass()
    stream.resolved = this.resolved
    stream.integrity = this.integrity

    const { prefix, workspaces } = this.opts

    // run the prepare script, get the list of files, and tar it up
    // pipe to the stream, and proxy errors the chain.
    this.#prepareDir()
      .then(async () => {
        if (!this.tree) {
          const arb = new this.Arborist({ path: this.resolved })
          this.tree = await arb.loadActual()
        }
        return packlist(this.tree, { path: this.resolved, prefix, workspaces })
      })
      .then(files => tar.c(tarCreateOptions(this.package), files)
        .on('error', er => stream.emit('error', er)).pipe(stream))
      .catch(er => stream.emit('error', er))
    return stream
  }

  manifest () {
    if (this.package) {
      return Promise.resolve(this.package)
    }

    return this[_.readPackageJson](this.resolved)
      .then(mani => this.package = {
        ...mani,
        _integrity: this.integrity && String(this.integrity),
        _resolved: this.resolved,
        _from: this.from,
      })
  }

  packument () {
    return FileFetcher.prototype.packument.apply(this)
  }
}
module.exports = DirFetcher
