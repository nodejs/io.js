const { output } = require('proc-log')
const getIdentity = require('../utils/get-identity.js')
const BaseCommand = require('../base-cmd.js')

class Whoami extends BaseCommand {
  static description = 'Display npm username'
  static name = 'whoami'
  static params = ['registry']

  async exec () {
    const username = await getIdentity(this.npm, { ...this.npm.flatOptions })
    if (this.npm.config.get('json')) {
      output.buffer(username)
    } else {
      output.standard(username)
    }
  }
}

module.exports = Whoami
