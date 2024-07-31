const reifyFinish = require('../utils/reify-finish.js')
const ArboristWorkspaceCmd = require('../arborist-cmd.js')

// dedupe duplicated packages, or find them in the tree
class Dedupe extends ArboristWorkspaceCmd {
  static description = 'Reduce duplication in the package tree'
  static name = 'dedupe'
  static params = [
    'install-strategy',
    'legacy-bundling',
    'global-style',
    'strict-peer-deps',
    'package-lock',
    'omit',
    'include',
    'ignore-scripts',
    'audit',
    'bin-links',
    'fund',
    'dry-run',
    ...super.params,
  ]

  async exec () {
    if (this.npm.global) {
      const er = new Error('`npm dedupe` does not work in global mode.')
      er.code = 'EDEDUPEGLOBAL'
      throw er
    }

    const dryRun = this.npm.config.get('dry-run')
    const where = this.npm.prefix
    const Arborist = require('@npmcli/arborist')
    const opts = {
      ...this.npm.flatOptions,
      path: where,
      dryRun,
      // Saving during dedupe would only update if one of your direct
      // dependencies was also duplicated somewhere in your tree. It would be
      // confusing if running this were to also update your package.json.  In
      // order to reduce potential confusion we set this to false.
      save: false,
      workspaces: this.workspaceNames,
    }
    const arb = new Arborist(opts)
    await arb.dedupe(opts)
    await reifyFinish(this.npm, arb)
  }
}

module.exports = Dedupe
