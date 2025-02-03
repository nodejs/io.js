const { resolve } = require('node:path')
const BaseCommand = require('../base-cmd.js')
const { log, output } = require('proc-log')

class QuerySelectorItem {
  constructor (node) {
    // all enumerable properties from the target
    Object.assign(this, node.target.package)

    // append extra info
    this.pkgid = node.target.pkgid
    this.location = node.target.location
    this.path = node.target.path
    this.realpath = node.target.realpath
    this.resolved = node.target.resolved
    this.from = []
    this.to = []
    this.dev = node.target.dev
    this.inBundle = node.target.inBundle
    this.deduped = this.from.length > 1
    this.overridden = node.overridden
    this.queryContext = node.queryContext
    for (const edge of node.target.edgesIn) {
      this.from.push(edge.from.location)
    }
    for (const [, edge] of node.target.edgesOut) {
      if (edge.to) {
        this.to.push(edge.to.location)
      }
    }
  }
}

class Query extends BaseCommand {
  #response = [] // response is the query response
  #seen = new Set() // paths we've seen so we can keep response deduped

  static description = 'Retrieve a filtered list of packages'
  static name = 'query'
  static usage = ['<selector>']

  static workspaces = true
  static ignoreImplicitWorkspace = false

  static params = [
    'global',
    'workspace',
    'workspaces',
    'include-workspace-root',
    'package-lock-only',
    'expect-results',
  ]

  constructor (...args) {
    super(...args)
    this.npm.config.set('json', true)
  }

  async exec (args) {
    const packageLock = this.npm.config.get('package-lock-only')
    const Arborist = require('@npmcli/arborist')
    const arb = new Arborist({
      ...this.npm.flatOptions,
      // one dir up from wherever node_modules lives
      path: resolve(this.npm.dir, '..'),
      forceActual: !packageLock,
    })
    let tree
    if (packageLock) {
      try {
        tree = await arb.loadVirtual()
      } catch (err) {
        log.verbose('loadVirtual', err.stack)
        throw this.usageError(
          'A package lock or shrinkwrap file is required in package-lock-only mode'
        )
      }
    } else {
      tree = await arb.loadActual()
    }
    await this.#queryTree(tree, args[0])
    this.#output()
  }

  async execWorkspaces (args) {
    await this.setWorkspaces()
    const Arborist = require('@npmcli/arborist')
    const arb = new Arborist({
      ...this.npm.flatOptions,
      path: this.npm.prefix,
    })
    // FIXME: Workspace support in query does not work as expected so this does not
    // do the same package-lock-only check as this.exec().
    // https://github.com/npm/cli/pull/6732#issuecomment-1708804921
    const tree = await arb.loadActual()
    for (const path of this.workspacePaths) {
      const wsTree = path === tree.root.path
        ? tree // --includes-workspace-root
        : await tree.querySelectorAll(`.workspace:path(${path})`).then(r => r[0].target)
      await this.#queryTree(wsTree, args[0])
    }
    this.#output()
  }

  #output () {
    this.checkExpected(this.#response.length)
    output.buffer(this.#response)
  }

  // builds a normalized inventory
  async #queryTree (tree, arg) {
    const items = await tree.querySelectorAll(arg, this.npm.flatOptions)
    for (const node of items) {
      const { location } = node.target
      if (!location || !this.#seen.has(location)) {
        const item = new QuerySelectorItem(node)
        this.#response.push(item)
        if (location) {
          this.#seen.add(item.location)
        }
      }
    }
  }
}

module.exports = Query
