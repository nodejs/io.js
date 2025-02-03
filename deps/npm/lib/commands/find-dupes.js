const ArboristWorkspaceCmd = require('../arborist-cmd.js')

// dedupe duplicated packages, or find them in the tree
class FindDupes extends ArboristWorkspaceCmd {
  static description = 'Find duplication in the package tree'
  static name = 'find-dupes'
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
    ...super.params,
  ]

  async exec () {
    this.npm.config.set('dry-run', true)
    return this.npm.exec('dedupe', [])
  }
}

module.exports = FindDupes
