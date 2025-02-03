const Npm = require('../npm.js')
const BaseCommand = require('../base-cmd.js')

class Set extends BaseCommand {
  static description = 'Set a value in the npm configuration'
  static name = 'set'
  static usage = ['<key>=<value> [<key>=<value> ...] (See `npm config`)']
  static params = ['global', 'location']
  static ignoreImplicitWorkspace = false

  // TODO
  /* istanbul ignore next */
  static async completion (opts) {
    const Config = Npm.cmd('config')
    return Config.completion(opts)
  }

  async exec (args) {
    if (!args.length) {
      throw this.usageError()
    }
    return this.npm.exec('config', ['set'].concat(args))
  }
}

module.exports = Set
