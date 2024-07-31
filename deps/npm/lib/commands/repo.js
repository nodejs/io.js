const { URL } = require('node:url')
const PackageUrlCmd = require('../package-url-cmd.js')

class Repo extends PackageUrlCmd {
  static description = 'Open package repository page in the browser'
  static name = 'repo'

  getUrl (spec, mani) {
    const r = mani.repository
    const rurl = !r ? null
      : typeof r === 'string' ? r
      : typeof r === 'object' && typeof r.url === 'string' ? r.url
      : null

    if (!rurl) {
      throw Object.assign(new Error('no repository'), {
        pkgid: spec,
      })
    }

    const info = this.hostedFromMani(mani)
    const url = info ?
      info.browse(mani.repository.directory) : unknownHostedUrl(rurl)

    if (!url) {
      throw Object.assign(new Error('no repository: could not get url'), {
        pkgid: spec,
      })
    }
    return url
  }
}

module.exports = Repo

const unknownHostedUrl = url => {
  try {
    const {
      protocol,
      hostname,
      pathname,
    } = new URL(url)

    /* istanbul ignore next - URL ctor should prevent this */
    if (!protocol || !hostname) {
      return null
    }

    const proto = /(git\+)http:$/.test(protocol) ? 'http:' : 'https:'
    const path = pathname.replace(/\.git$/, '')
    return `${proto}//${hostname}${path}`
  } catch (e) {
    return null
  }
}
