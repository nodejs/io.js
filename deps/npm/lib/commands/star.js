const npmFetch = require('npm-registry-fetch')
const npa = require('npm-package-arg')
const { log, output } = require('proc-log')
const getIdentity = require('../utils/get-identity')
const BaseCommand = require('../base-cmd.js')

class Star extends BaseCommand {
  static description = 'Mark your favorite packages'
  static name = 'star'
  static usage = ['[<package-spec>...]']
  static params = [
    'registry',
    'unicode',
    'otp',
  ]

  static ignoreImplicitWorkspace = false

  async exec (args) {
    if (!args.length) {
      throw this.usageError()
    }

    // if we're unstarring, then show an empty star image
    // otherwise, show the full star image
    const unicode = this.npm.config.get('unicode')
    const full = unicode ? '\u2605 ' : '(*)'
    const empty = unicode ? '\u2606 ' : '( )'
    const show = this.name === 'star' ? full : empty

    const pkgs = args.map(npa)
    const username = await getIdentity(this.npm, this.npm.flatOptions)

    for (const pkg of pkgs) {
      const fullData = await npmFetch.json(pkg.escapedName, {
        ...this.npm.flatOptions,
        spec: pkg,
        query: { write: true },
        preferOnline: true,
      })

      const body = {
        _id: fullData._id,
        _rev: fullData._rev,
        users: fullData.users || {},
      }

      if (this.name === 'star') {
        log.info('star', 'starring', body._id)
        body.users[username] = true
        log.verbose('star', 'starring', body)
      } else {
        delete body.users[username]
        log.info('unstar', 'unstarring', body._id)
        log.verbose('unstar', 'unstarring', body)
      }

      const data = await npmFetch.json(pkg.escapedName, {
        ...this.npm.flatOptions,
        spec: pkg,
        method: 'PUT',
        body,
      })

      output.standard(show + ' ' + pkg.name)
      log.verbose('star', data)
      return data
    }
  }
}

module.exports = Star
