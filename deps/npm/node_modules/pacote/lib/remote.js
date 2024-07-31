const fetch = require('npm-registry-fetch')
const { Minipass } = require('minipass')
const Fetcher = require('./fetcher.js')
const FileFetcher = require('./file.js')
const _ = require('./util/protected.js')
const pacoteVersion = require('../package.json').version

class RemoteFetcher extends Fetcher {
  constructor (spec, opts) {
    super(spec, opts)
    this.resolved = this.spec.fetchSpec
    const resolvedURL = new URL(this.resolved)
    if (this.replaceRegistryHost !== 'never'
      && (this.replaceRegistryHost === 'always'
      || this.replaceRegistryHost === resolvedURL.host)) {
      this.resolved = new URL(resolvedURL.pathname, this.registry).href
    }

    // nam is a fermented pork sausage that is good to eat
    const nameat = this.spec.name ? `${this.spec.name}@` : ''
    this.pkgid = opts.pkgid ? opts.pkgid : `remote:${nameat}${this.resolved}`
  }

  // Don't need to cache tarball fetches in pacote, because make-fetch-happen
  // will write into cacache anyway.
  get [_.cacheFetches] () {
    return false
  }

  [_.tarballFromResolved] () {
    const stream = new Minipass()
    stream.hasIntegrityEmitter = true

    const fetchOpts = {
      ...this.opts,
      headers: this.#headers(),
      spec: this.spec,
      integrity: this.integrity,
      algorithms: [this.pickIntegrityAlgorithm()],
    }

    // eslint-disable-next-line promise/always-return
    fetch(this.resolved, fetchOpts).then(res => {
      res.body.on('error',
        /* istanbul ignore next - exceedingly rare and hard to simulate */
        er => stream.emit('error', er)
      )

      res.body.on('integrity', i => {
        this.integrity = i
        stream.emit('integrity', i)
      })

      res.body.pipe(stream)
    }).catch(er => stream.emit('error', er))

    return stream
  }

  #headers () {
    return {
      // npm will override this, but ensure that we always send *something*
      'user-agent': this.opts.userAgent ||
        `pacote/${pacoteVersion} node/${process.version}`,
      ...(this.opts.headers || {}),
      'pacote-version': pacoteVersion,
      'pacote-req-type': 'tarball',
      'pacote-pkg-id': this.pkgid,
      ...(this.integrity ? { 'pacote-integrity': String(this.integrity) }
      : {}),
      ...(this.opts.headers || {}),
    }
  }

  get types () {
    return ['remote']
  }

  // getting a packument and/or manifest is the same as with a file: spec.
  // unpack the tarball stream, and then read from the package.json file.
  packument () {
    return FileFetcher.prototype.packument.apply(this)
  }

  manifest () {
    return FileFetcher.prototype.manifest.apply(this)
  }
}
module.exports = RemoteFetcher
