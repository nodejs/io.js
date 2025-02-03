const { resolve } = require('node:path')
const pkgJson = require('@npmcli/package-json')
const reifyFinish = require('../utils/reify-finish.js')
const completion = require('../utils/installed-shallow.js')
const ArboristWorkspaceCmd = require('../arborist-cmd.js')

class Uninstall extends ArboristWorkspaceCmd {
  static description = 'Remove a package'
  static name = 'uninstall'
  static params = ['save', 'global', ...super.params]
  static usage = ['[<@scope>/]<pkg>...']
  static ignoreImplicitWorkspace = false

  // TODO
  /* istanbul ignore next */
  static async completion (opts, npm) {
    return completion(npm, opts)
  }

  async exec (args) {
    if (!args.length) {
      if (!this.npm.global) {
        throw new Error('Must provide a package name to remove')
      } else {
        try {
          const { content: pkg } = await pkgJson.normalize(this.npm.localPrefix)
          args.push(pkg.name)
        } catch (er) {
          if (er.code !== 'ENOENT' && er.code !== 'ENOTDIR') {
            throw er
          } else {
            throw this.usageError()
          }
        }
      }
    }

    // the /path/to/node_modules/..
    const path = this.npm.global
      ? resolve(this.npm.globalDir, '..')
      : this.npm.localPrefix

    const Arborist = require('@npmcli/arborist')
    const opts = {
      ...this.npm.flatOptions,
      path,
      rm: args,
      workspaces: this.workspaceNames,
    }
    const arb = new Arborist(opts)
    await arb.reify(opts)
    await reifyFinish(this.npm, arb)
  }
}

module.exports = Uninstall
