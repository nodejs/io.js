const reifyFinish = require('../utils/reify-finish.js')
const ArboristWorkspaceCmd = require('../arborist-cmd.js')

// prune extraneous packages
class Prune extends ArboristWorkspaceCmd {
  static description = 'Remove extraneous packages'
  static name = 'prune'
  static params = [
    'omit',
    'include',
    'dry-run',
    'json',
    'foreground-scripts',
    'ignore-scripts',
    ...super.params,
  ]

  static usage = ['[[<@scope>/]<pkg>...]']

  async exec () {
    const where = this.npm.prefix
    const Arborist = require('@npmcli/arborist')
    const opts = {
      ...this.npm.flatOptions,
      path: where,
      workspaces: this.workspaceNames,
    }
    const arb = new Arborist(opts)
    await arb.prune(opts)
    await reifyFinish(this.npm, arb)
  }
}

module.exports = Prune
