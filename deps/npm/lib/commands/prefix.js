const { output } = require('proc-log')
const BaseCommand = require('../base-cmd.js')

class Prefix extends BaseCommand {
  static description = 'Display prefix'
  static name = 'prefix'
  static params = ['global']
  static usage = ['[-g]']

  async exec () {
    return output.standard(this.npm.prefix)
  }
}

module.exports = Prefix
