const PackageUrlCmd = require('../package-url-cmd.js')

class Docs extends PackageUrlCmd {
  static description = 'Open documentation for a package in a web browser'
  static name = 'docs'

  getUrl (spec, mani) {
    if (mani.homepage) {
      return mani.homepage
    }

    const info = this.hostedFromMani(mani)
    if (info) {
      return info.docs()
    }

    return `https://www.npmjs.com/package/${mani.name}`
  }
}

module.exports = Docs
