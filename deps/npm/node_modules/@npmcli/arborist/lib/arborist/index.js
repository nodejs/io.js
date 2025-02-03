// The arborist manages three trees:
// - actual
// - virtual
// - ideal
//
// The actual tree is what's present on disk in the node_modules tree
// and elsewhere that links may extend.
//
// The virtual tree is loaded from metadata (package.json and lock files).
//
// The ideal tree is what we WANT that actual tree to become.  This starts
// with the virtual tree, and then applies the options requesting
// add/remove/update actions.
//
// To reify a tree, we calculate a diff between the ideal and actual trees,
// and then turn the actual tree into the ideal tree by taking the actions
// required.  At the end of the reification process, the actualTree is
// updated to reflect the changes.
//
// Each tree has an Inventory at the root.  Shrinkwrap is tracked by Arborist
// instance.  It always refers to the actual tree, but is updated (and written
// to disk) on reification.

// Each of the mixin "classes" adds functionality, but are not dependent on
// constructor call order.  So, we just load them in an array, and build up
// the base class, so that the overall voltron class is easier to test and
// cover, and separation of concerns can be maintained.

const { resolve } = require('node:path')
const { homedir } = require('node:os')
const { depth } = require('treeverse')
const mapWorkspaces = require('@npmcli/map-workspaces')
const { log, time } = require('proc-log')
const { saveTypeMap } = require('../add-rm-pkg-deps.js')
const AuditReport = require('../audit-report.js')
const relpath = require('../relpath.js')
const PackumentCache = require('../packument-cache.js')

const mixins = [
  require('../tracker.js'),
  require('./build-ideal-tree.js'),
  require('./load-actual.js'),
  require('./load-virtual.js'),
  require('./rebuild.js'),
  require('./reify.js'),
  require('./isolated-reifier.js'),
]

const _setWorkspaces = Symbol.for('setWorkspaces')
const Base = mixins.reduce((a, b) => b(a), require('node:events'))

// if it's 1, 2, or 3, set it explicitly that.
// if undefined or null, set it null
// otherwise, throw.
const lockfileVersion = lfv => {
  if (lfv === 1 || lfv === 2 || lfv === 3) {
    return lfv
  }

  if (lfv === undefined || lfv === null) {
    return null
  }

  throw new TypeError('Invalid lockfileVersion config: ' + lfv)
}

class Arborist extends Base {
  constructor (options = {}) {
    const timeEnd = time.start('arborist:ctor')
    super(options)
    this.options = {
      nodeVersion: process.version,
      ...options,
      Arborist: this.constructor,
      binLinks: 'binLinks' in options ? !!options.binLinks : true,
      cache: options.cache || `${homedir()}/.npm/_cacache`,
      dryRun: !!options.dryRun,
      formatPackageLock: 'formatPackageLock' in options ? !!options.formatPackageLock : true,
      force: !!options.force,
      global: !!options.global,
      ignoreScripts: !!options.ignoreScripts,
      installStrategy: options.global ? 'shallow' : (options.installStrategy ? options.installStrategy : 'hoisted'),
      lockfileVersion: lockfileVersion(options.lockfileVersion),
      packageLockOnly: !!options.packageLockOnly,
      packumentCache: options.packumentCache || new PackumentCache(),
      path: options.path || '.',
      rebuildBundle: 'rebuildBundle' in options ? !!options.rebuildBundle : true,
      replaceRegistryHost: options.replaceRegistryHost,
      savePrefix: 'savePrefix' in options ? options.savePrefix : '^',
      scriptShell: options.scriptShell,
      workspaces: options.workspaces || [],
      workspacesEnabled: options.workspacesEnabled !== false,
    }
    // TODO we only ever look at this.options.replaceRegistryHost, not
    // this.replaceRegistryHost.  Defaulting needs to be written back to
    // this.options to work properly
    this.replaceRegistryHost = this.options.replaceRegistryHost =
      (!this.options.replaceRegistryHost || this.options.replaceRegistryHost === 'npmjs') ?
        'registry.npmjs.org' : this.options.replaceRegistryHost

    if (options.saveType && !saveTypeMap.get(options.saveType)) {
      throw new Error(`Invalid saveType ${options.saveType}`)
    }
    this.cache = resolve(this.options.cache)
    this.diff = null
    this.path = resolve(this.options.path)
    timeEnd()
  }

  // TODO: We should change these to static functions instead
  //   of methods for the next major version

  // Get the actual nodes corresponding to a root node's child workspaces,
  // given a list of workspace names.
  workspaceNodes (tree, workspaces) {
    const wsMap = tree.workspaces
    if (!wsMap) {
      log.warn('workspaces', 'filter set, but no workspaces present')
      return []
    }

    const nodes = []
    for (const name of workspaces) {
      const path = wsMap.get(name)
      if (!path) {
        log.warn('workspaces', `${name} in filter set, but not in workspaces`)
        continue
      }

      const loc = relpath(tree.realpath, path)
      const node = tree.inventory.get(loc)

      if (!node) {
        log.warn('workspaces', `${name} in filter set, but no workspace folder present`)
        continue
      }

      nodes.push(node)
    }

    return nodes
  }

  // returns a set of workspace nodes and all their deps
  // TODO why is includeWorkspaceRoot a param?
  // TODO why is workspaces a param?
  workspaceDependencySet (tree, workspaces, includeWorkspaceRoot) {
    const wsNodes = this.workspaceNodes(tree, workspaces)
    if (includeWorkspaceRoot) {
      for (const edge of tree.edgesOut.values()) {
        if (edge.type !== 'workspace' && edge.to) {
          wsNodes.push(edge.to)
        }
      }
    }
    const wsDepSet = new Set(wsNodes)
    const extraneous = new Set()
    for (const node of wsDepSet) {
      for (const edge of node.edgesOut.values()) {
        const dep = edge.to
        if (dep) {
          wsDepSet.add(dep)
          if (dep.isLink) {
            wsDepSet.add(dep.target)
          }
        }
      }
      for (const child of node.children.values()) {
        if (child.extraneous) {
          extraneous.add(child)
        }
      }
    }
    for (const extra of extraneous) {
      wsDepSet.add(extra)
    }

    return wsDepSet
  }

  // returns a set of root dependencies, excluding dependencies that are
  // exclusively workspace dependencies
  excludeWorkspacesDependencySet (tree) {
    const rootDepSet = new Set()
    depth({
      tree,
      visit: node => {
        for (const { to } of node.edgesOut.values()) {
          if (!to || to.isWorkspace) {
            continue
          }
          for (const edgeIn of to.edgesIn.values()) {
            if (edgeIn.from.isRoot || rootDepSet.has(edgeIn.from)) {
              rootDepSet.add(to)
            }
          }
        }
        return node
      },
      filter: node => node,
      getChildren: (node, tree) =>
        [...tree.edgesOut.values()].map(edge => edge.to),
    })
    return rootDepSet
  }

  async [_setWorkspaces] (node) {
    const workspaces = await mapWorkspaces({
      cwd: node.path,
      pkg: node.package,
    })

    if (node && workspaces.size) {
      node.workspaces = workspaces
    }

    return node
  }

  async audit (options = {}) {
    this.addTracker('audit')
    if (this.options.global) {
      throw Object.assign(
        new Error('`npm audit` does not support testing globals'),
        { code: 'EAUDITGLOBAL' }
      )
    }

    // allow the user to set options on the ctor as well.
    // XXX: deprecate separate method options objects.
    options = { ...this.options, ...options }

    const timeEnd = time.start('audit')
    let tree
    if (options.packageLock === false) {
      // build ideal tree
      await this.loadActual(options)
      await this.buildIdealTree()
      tree = this.idealTree
    } else {
      tree = await this.loadVirtual()
    }
    if (this.options.workspaces.length) {
      options.filterSet = this.workspaceDependencySet(
        tree,
        this.options.workspaces,
        this.options.includeWorkspaceRoot
      )
    }
    if (!options.workspacesEnabled) {
      options.filterSet =
        this.excludeWorkspacesDependencySet(tree)
    }
    this.auditReport = await AuditReport.load(tree, options)
    const ret = options.fix ? this.reify(options) : this.auditReport
    timeEnd()
    this.finishTracker('audit')
    return ret
  }

  async dedupe (options = {}) {
    // allow the user to set options on the ctor as well.
    // XXX: deprecate separate method options objects.
    options = { ...this.options, ...options }
    const tree = await this.loadVirtual().catch(() => this.loadActual())
    const names = []
    for (const name of tree.inventory.query('name')) {
      if (tree.inventory.query('name', name).size > 1) {
        names.push(name)
      }
    }
    return this.reify({
      ...options,
      preferDedupe: true,
      update: { names },
    })
  }
}

module.exports = Arborist
