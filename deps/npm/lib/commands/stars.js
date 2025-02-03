const npmFetch = require('npm-registry-fetch')
const { log, output } = require('proc-log')
const getIdentity = require('../utils/get-identity.js')
const BaseCommand = require('../base-cmd.js')

class Stars extends BaseCommand {
  static description = 'View packages marked as favorites'
  static name = 'stars'
  static usage = ['[<user>]']
  static params = ['registry']
  static ignoreImplicitWorkspace = false

  async exec ([user]) {
    try {
      if (!user) {
        user = await getIdentity(this.npm, this.npm.flatOptions)
      }

      const { rows } = await npmFetch.json('/-/_view/starredByUser', {
        ...this.npm.flatOptions,
        query: { key: `"${user}"` },
      })
      if (rows.length === 0) {
        log.warn('stars', 'user has not starred any packages')
      }

      for (const row of rows) {
        output.standard(row.value)
      }
    } catch (err) {
      if (err.code === 'ENEEDAUTH') {
        log.warn('stars', 'auth is required to look up your username')
      }
      throw err
    }
  }
}

module.exports = Stars
