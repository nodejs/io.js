const { explainNode } = require('../utils/explain-dep.js')
const npa = require('npm-package-arg')
const semver = require('semver')
const { relative, resolve } = require('node:path')
const validName = require('validate-npm-package-name')
const { output } = require('proc-log')
const ArboristWorkspaceCmd = require('../arborist-cmd.js')

class Explain extends ArboristWorkspaceCmd {
  static description = 'Explain installed packages'
  static name = 'explain'
  static usage = ['<package-spec>']
  static params = [
    'json',
    'workspace',
  ]

  static ignoreImplicitWorkspace = false

  // TODO
  /* istanbul ignore next */
  static async completion (opts, npm) {
    const completion = require('../utils/installed-deep.js')
    return completion(npm, opts)
  }

  async exec (args) {
    if (!args.length) {
      throw this.usageError()
    }

    const Arborist = require('@npmcli/arborist')
    const arb = new Arborist({ path: this.npm.prefix, ...this.npm.flatOptions })
    const tree = await arb.loadActual()

    if (this.npm.flatOptions.workspacesEnabled
      && this.workspaceNames
      && this.workspaceNames.length
    ) {
      this.filterSet = arb.workspaceDependencySet(tree, this.workspaceNames)
    } else if (!this.npm.flatOptions.workspacesEnabled) {
      this.filterSet =
        arb.excludeWorkspacesDependencySet(tree)
    }

    const nodes = new Set()
    for (const arg of args) {
      for (const node of this.getNodes(tree, arg)) {
        const filteredOut = this.filterSet
          && this.filterSet.size > 0
          && !this.filterSet.has(node)
        if (!filteredOut) {
          nodes.add(node)
        }
      }
    }
    if (nodes.size === 0) {
      throw new Error(`No dependencies found matching ${args.join(', ')}`)
    }

    const expls = []
    for (const node of nodes) {
      const { extraneous, dev, optional, devOptional, peer, inBundle, overridden } = node
      const expl = node.explain()
      if (extraneous) {
        expl.extraneous = true
      } else {
        expl.dev = dev
        expl.optional = optional
        expl.devOptional = devOptional
        expl.peer = peer
        expl.bundled = inBundle
        expl.overridden = overridden
      }
      expls.push(expl)
    }

    if (this.npm.flatOptions.json) {
      output.buffer(expls)
    } else {
      output.standard(expls.map(expl => {
        return explainNode(expl, Infinity, this.npm.chalk)
      }).join('\n\n'))
    }
  }

  getNodes (tree, arg) {
    // if it's just a name, return packages by that name
    const { validForOldPackages: valid } = validName(arg)
    if (valid) {
      return tree.inventory.query('packageName', arg)
    }

    // if it's a location, get that node
    const maybeLoc = arg.replace(/\\/g, '/').replace(/\/+$/, '')
    const nodeByLoc = tree.inventory.get(maybeLoc)
    if (nodeByLoc) {
      return [nodeByLoc]
    }

    // maybe a path to a node_modules folder
    const maybePath = relative(this.npm.prefix, resolve(maybeLoc))
      .replace(/\\/g, '/').replace(/\/+$/, '')
    const nodeByPath = tree.inventory.get(maybePath)
    if (nodeByPath) {
      return [nodeByPath]
    }

    // otherwise, try to select all matching nodes
    try {
      return this.getNodesByVersion(tree, arg)
    } catch (er) {
      return []
    }
  }

  getNodesByVersion (tree, arg) {
    const spec = npa(arg, this.npm.prefix)
    if (spec.type !== 'version' && spec.type !== 'range') {
      return []
    }

    return tree.inventory.filter(node => {
      return node.package.name === spec.name &&
        semver.satisfies(node.package.version, spec.rawSpec)
    })
  }
}

module.exports = Explain
