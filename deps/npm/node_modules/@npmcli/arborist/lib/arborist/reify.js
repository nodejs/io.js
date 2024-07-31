// mixin implementing the reify method
const onExit = require('../signal-handling.js')
const pacote = require('pacote')
const AuditReport = require('../audit-report.js')
const { subset, intersects } = require('semver')
const npa = require('npm-package-arg')
const semver = require('semver')
const debug = require('../debug.js')
const { walkUp } = require('walk-up-path')
const { log, time } = require('proc-log')
const hgi = require('hosted-git-info')
const rpj = require('read-package-json-fast')

const { dirname, resolve, relative, join } = require('node:path')
const { depth: dfwalk } = require('treeverse')
const {
  lstat,
  mkdir,
  rm,
  symlink,
} = require('node:fs/promises')
const { moveFile } = require('@npmcli/fs')
const PackageJson = require('@npmcli/package-json')
const packageContents = require('@npmcli/installed-package-contents')
const runScript = require('@npmcli/run-script')
const { checkEngine, checkPlatform } = require('npm-install-checks')

const treeCheck = require('../tree-check.js')
const relpath = require('../relpath.js')
const Diff = require('../diff.js')
const retirePath = require('../retire-path.js')
const promiseAllRejectLate = require('promise-all-reject-late')
const { callLimit: promiseCallLimit } = require('promise-call-limit')
const optionalSet = require('../optional-set.js')
const calcDepFlags = require('../calc-dep-flags.js')
const { saveTypeMap, hasSubKey } = require('../add-rm-pkg-deps.js')

const Shrinkwrap = require('../shrinkwrap.js')
const { defaultLockfileVersion } = Shrinkwrap

// Part of steps (steps need refactoring before we can do anything about these)
const _retireShallowNodes = Symbol.for('retireShallowNodes')
const _loadBundlesAndUpdateTrees = Symbol.for('loadBundlesAndUpdateTrees')
const _submitQuickAudit = Symbol('submitQuickAudit')
const _addOmitsToTrashList = Symbol('addOmitsToTrashList')
const _unpackNewModules = Symbol.for('unpackNewModules')
const _build = Symbol.for('build')

// shared by rebuild mixin
const _trashList = Symbol.for('trashList')
const _handleOptionalFailure = Symbol.for('handleOptionalFailure')
const _loadTrees = Symbol.for('loadTrees')
// defined by rebuild mixin
const _checkBins = Symbol.for('checkBins')

// shared symbols for swapping out when testing
// TODO tests should not be this deep into internals
const _diffTrees = Symbol.for('diffTrees')
const _createSparseTree = Symbol.for('createSparseTree')
const _loadShrinkwrapsAndUpdateTrees = Symbol.for('loadShrinkwrapsAndUpdateTrees')
const _reifyNode = Symbol.for('reifyNode')
const _updateAll = Symbol.for('updateAll')
const _updateNames = Symbol.for('updateNames')
const _moveContents = Symbol.for('moveContents')
const _moveBackRetiredUnchanged = Symbol.for('moveBackRetiredUnchanged')
const _removeTrash = Symbol.for('removeTrash')
const _renamePath = Symbol.for('renamePath')
const _rollbackRetireShallowNodes = Symbol.for('rollbackRetireShallowNodes')
const _rollbackCreateSparseTree = Symbol.for('rollbackCreateSparseTree')
const _rollbackMoveBackRetiredUnchanged = Symbol.for('rollbackMoveBackRetiredUnchanged')
const _saveIdealTree = Symbol.for('saveIdealTree')
const _reifyPackages = Symbol.for('reifyPackages')

// defined by build-ideal-tree mixin
const _resolvedAdd = Symbol.for('resolvedAdd')
const _usePackageLock = Symbol.for('usePackageLock')
// used by build-ideal-tree mixin
const _addNodeToTrashList = Symbol.for('addNodeToTrashList')

const _createIsolatedTree = Symbol.for('createIsolatedTree')

module.exports = cls => class Reifier extends cls {
  #bundleMissing = new Set() // child nodes we'd EXPECT to be included in a bundle, but aren't
  #bundleUnpacked = new Set() // the nodes we unpack to read their bundles
  #dryRun
  #nmValidated = new Set()
  #omitDev
  #omitPeer
  #omitOptional
  #retiredPaths = {}
  #retiredUnchanged = {}
  #savePrefix
  #shrinkwrapInflated = new Set()
  #sparseTreeDirs = new Set()
  #sparseTreeRoots = new Set()

  constructor (options) {
    super(options)

    this[_trashList] = new Set()
  }

  // public method
  async reify (options = {}) {
    const linked = (options.installStrategy || this.options.installStrategy) === 'linked'

    if (this.options.packageLockOnly && this.options.global) {
      const er = new Error('cannot generate lockfile for global packages')
      er.code = 'ESHRINKWRAPGLOBAL'
      throw er
    }

    const omit = new Set(options.omit || [])
    this.#omitDev = omit.has('dev')
    this.#omitOptional = omit.has('optional')
    this.#omitPeer = omit.has('peer')

    // start tracker block
    this.addTracker('reify')
    const timeEnd = time.start('reify')
    // don't create missing dirs on dry runs
    if (!this.options.packageLockOnly && !this.options.dryRun) {
      // we do NOT want to set ownership on this folder, especially
      // recursively, because it can have other side effects to do that
      // in a project directory.  We just want to make it if it's missing.
      await mkdir(resolve(this.path), { recursive: true })

      // do not allow the top-level node_modules to be a symlink
      await this.#validateNodeModules(resolve(this.path, 'node_modules'))
    }
    await this[_loadTrees](options)

    const oldTree = this.idealTree
    if (linked) {
      // swap out the tree with the isolated tree
      // this is currently technical debt which will be resolved in a refactor
      // of Node/Link trees
      log.warn('reify', 'The "linked" install strategy is EXPERIMENTAL and may contain bugs.')
      this.idealTree = await this[_createIsolatedTree]()
    }
    await this[_diffTrees]()
    await this[_reifyPackages]()
    if (linked) {
      // swap back in the idealTree
      // so that the lockfile is preserved
      this.idealTree = oldTree
    }
    await this[_saveIdealTree](options)
    // clean up any trash that is still in the tree
    for (const path of this[_trashList]) {
      const loc = relpath(this.idealTree.realpath, path)
      const node = this.idealTree.inventory.get(loc)
      if (node && node.root === this.idealTree) {
        node.parent = null
      }
    }

    // if we filtered to only certain nodes, then anything ELSE needs
    // to be untouched in the resulting actual tree, even if it differs
    // in the idealTree.  Copy over anything that was in the actual and
    // was not changed, delete anything in the ideal and not actual.
    // Then we move the entire idealTree over to this.actualTree, and
    // save the hidden lockfile.
    if (this.diff && this.diff.filterSet.size) {
      const reroot = new Set()

      const { filterSet } = this.diff
      const seen = new Set()
      for (const [loc, ideal] of this.idealTree.inventory.entries()) {
        seen.add(loc)

        // if it's an ideal node from the filter set, then skip it
        // because we already made whatever changes were necessary
        if (filterSet.has(ideal)) {
          continue
        }

        // otherwise, if it's not in the actualTree, then it's not a thing
        // that we actually added.  And if it IS in the actualTree, then
        // it's something that we left untouched, so we need to record
        // that.
        const actual = this.actualTree.inventory.get(loc)
        if (!actual) {
          ideal.root = null
        } else {
          if ([...actual.linksIn].some(link => filterSet.has(link))) {
            seen.add(actual.location)
            continue
          }
          const { realpath, isLink } = actual
          if (isLink && ideal.isLink && ideal.realpath === realpath) {
            continue
          } else {
            reroot.add(actual)
          }
        }
      }

      // now find any actual nodes that may not be present in the ideal
      // tree, but were left behind by virtue of not being in the filter
      for (const [loc, actual] of this.actualTree.inventory.entries()) {
        if (seen.has(loc)) {
          continue
        }
        seen.add(loc)

        // we know that this is something that ISN'T in the idealTree,
        // or else we will have addressed it in the previous loop.
        // If it's in the filterSet, that means we intentionally removed
        // it, so nothing to do here.
        if (filterSet.has(actual)) {
          continue
        }

        reroot.add(actual)
      }

      // go through the rerooted actual nodes, and move them over.
      for (const actual of reroot) {
        actual.root = this.idealTree
      }

      // prune out any tops that lack a linkIn, they are no longer relevant.
      for (const top of this.idealTree.tops) {
        if (top.linksIn.size === 0) {
          top.root = null
        }
      }

      // need to calculate dep flags, since nodes may have been marked
      // as extraneous or otherwise incorrect during transit.
      calcDepFlags(this.idealTree)
    }

    // save the ideal's meta as a hidden lockfile after we actualize it
    this.idealTree.meta.filename =
      this.idealTree.realpath + '/node_modules/.package-lock.json'
    this.idealTree.meta.hiddenLockfile = true
    this.idealTree.meta.lockfileVersion = defaultLockfileVersion

    this.actualTree = this.idealTree
    this.idealTree = null

    if (!this.options.global) {
      await this.actualTree.meta.save()
      const ignoreScripts = !!this.options.ignoreScripts
      // if we aren't doing a dry run or ignoring scripts and we actually made changes to the dep
      // tree, then run the dependencies scripts
      if (!this.options.dryRun && !ignoreScripts && this.diff && this.diff.children.length) {
        const { path, package: pkg } = this.actualTree.target
        const stdio = this.options.foregroundScripts ? 'inherit' : 'pipe'
        const { scripts = {} } = pkg
        for (const event of ['predependencies', 'dependencies', 'postdependencies']) {
          if (Object.prototype.hasOwnProperty.call(scripts, event)) {
            log.info('run', pkg._id, event, scripts[event])
            await time.start(`reify:run:${event}`, () => runScript({
              event,
              path,
              pkg,
              stdio,
              scriptShell: this.options.scriptShell,
            }))
          }
        }
      }
    }
    // This is a very bad pattern and I can't wait to stop doing it
    this.auditReport = await this.auditReport

    this.finishTracker('reify')
    timeEnd()
    return treeCheck(this.actualTree)
  }

  async [_reifyPackages] () {
    // we don't submit the audit report or write to disk on dry runs
    if (this.options.dryRun) {
      return
    }

    if (this.options.packageLockOnly) {
      // we already have the complete tree, so just audit it now,
      // and that's all we have to do here.
      return this[_submitQuickAudit]()
    }

    // ok, we're about to start touching the fs.  need to roll back
    // if we get an early termination.
    let reifyTerminated = null
    const removeHandler = onExit(({ signal }) => {
      // only call once.  if signal hits twice, we just terminate
      removeHandler()
      reifyTerminated = Object.assign(new Error('process terminated'), {
        signal,
      })
      return false
    })

    // [rollbackfn, [...actions]]
    // after each step, if the process was terminated, execute the rollback
    // note that each rollback *also* calls the previous one when it's
    // finished, and then the first one throws the error, so we only need
    // a new rollback step when we have a new thing that must be done to
    // revert the install.
    const steps = [
      [_rollbackRetireShallowNodes, [
        _retireShallowNodes,
      ]],
      [_rollbackCreateSparseTree, [
        _createSparseTree,
        _addOmitsToTrashList,
        _loadShrinkwrapsAndUpdateTrees,
        _loadBundlesAndUpdateTrees,
        _submitQuickAudit,
        _unpackNewModules,
      ]],
      [_rollbackMoveBackRetiredUnchanged, [
        _moveBackRetiredUnchanged,
        _build,
      ]],
    ]
    for (const [rollback, actions] of steps) {
      for (const action of actions) {
        try {
          await this[action]()
          if (reifyTerminated) {
            throw reifyTerminated
          }
        } catch (er) {
          // TODO rollbacks shouldn't be relied on to throw err
          await this[rollback](er)
          /* istanbul ignore next - rollback throws, should never hit this */
          throw er
        }
      }
    }

    // no rollback for this one, just exit with the error, since the
    // install completed and can't be safely recovered at this point.
    await this[_removeTrash]()
    if (reifyTerminated) {
      throw reifyTerminated
    }

    // done modifying the file system, no need to keep listening for sigs
    removeHandler()
  }

  // when doing a local install, we load everything and figure it all out.
  // when doing a global install, we *only* care about the explicit requests.
  [_loadTrees] (options) {
    const timeEnd = time.start('reify:loadTrees')
    const bitOpt = {
      ...options,
      complete: this.options.packageLockOnly || this.options.dryRun,
    }

    // if we're only writing a package lock, then it doesn't matter what's here
    if (this.options.packageLockOnly) {
      return this.buildIdealTree(bitOpt).then(timeEnd)
    }

    const actualOpt = this.options.global ? {
      ignoreMissing: true,
      global: true,
      filter: (node, kid) => {
        // if it's not the project root, and we have no explicit requests,
        // then we're already into a nested dep, so we keep it
        if (this.explicitRequests.size === 0 || !node.isProjectRoot) {
          return true
        }

        // if we added it as an edgeOut, then we want it
        if (this.idealTree.edgesOut.has(kid)) {
          return true
        }

        // if it's an explicit request, then we want it
        const hasExplicit = [...this.explicitRequests]
          .some(edge => edge.name === kid)
        if (hasExplicit) {
          return true
        }

        // ignore the rest of the global install folder
        return false
      },
    } : { ignoreMissing: true }

    if (!this.options.global) {
      return Promise.all([
        this.loadActual(actualOpt),
        this.buildIdealTree(bitOpt),
      ]).then(timeEnd)
    }

    // the global install space tends to have a lot of stuff in it.  don't
    // load all of it, just what we care about.  we won't be saving a
    // hidden lockfile in there anyway.  Note that we have to load ideal
    // BEFORE loading actual, so that the actualOpt can use the
    // explicitRequests which is set during buildIdealTree
    return this.buildIdealTree(bitOpt)
      .then(() => this.loadActual(actualOpt))
      .then(timeEnd)
  }

  [_diffTrees] () {
    if (this.options.packageLockOnly) {
      return
    }

    const timeEnd = time.start('reify:diffTrees')
    // XXX if we have an existing diff already, there should be a way
    // to just invalidate the parts that changed, but avoid walking the
    // whole tree again.

    const includeWorkspaces = this.options.workspacesEnabled
    const includeRootDeps = !includeWorkspaces
      || this.options.includeWorkspaceRoot && this.options.workspaces.length > 0

    const filterNodes = []
    if (this.options.global && this.explicitRequests.size) {
      const idealTree = this.idealTree.target
      const actualTree = this.actualTree.target
      // we ONLY are allowed to make changes in the global top-level
      // children where there's an explicit request.
      for (const { name } of this.explicitRequests) {
        const ideal = idealTree.children.get(name)
        if (ideal) {
          filterNodes.push(ideal)
        }
        const actual = actualTree.children.get(name)
        if (actual) {
          filterNodes.push(actual)
        }
      }
    } else {
      if (includeWorkspaces) {
        // add all ws nodes to filterNodes
        for (const ws of this.options.workspaces) {
          const ideal = this.idealTree.children.get(ws)
          if (ideal) {
            filterNodes.push(ideal)
          }
          const actual = this.actualTree.children.get(ws)
          if (actual) {
            filterNodes.push(actual)
          }
        }
      }
      if (includeRootDeps) {
        // add all non-workspace nodes to filterNodes
        for (const tree of [this.idealTree, this.actualTree]) {
          for (const { type, to } of tree.edgesOut.values()) {
            if (type !== 'workspace' && to) {
              filterNodes.push(to)
            }
          }
        }
      }
    }

    // find all the nodes that need to change between the actual
    // and ideal trees.
    this.diff = Diff.calculate({
      shrinkwrapInflated: this.#shrinkwrapInflated,
      filterNodes,
      actual: this.actualTree,
      ideal: this.idealTree,
    })

    // we don't have to add 'removed' folders to the trashlist, because
    // they'll be moved aside to a retirement folder, and then the retired
    // folder will be deleted at the end.  This is important when we have
    // a folder like FOO being "removed" in favor of a folder like "foo",
    // because if we remove node_modules/FOO on case-insensitive systems,
    // it will remove the dep that we *want* at node_modules/foo.

    timeEnd()
  }

  // add the node and all its bins to the list of things to be
  // removed later on in the process.  optionally, also mark them
  // as a retired paths, so that we move them out of the way and
  // replace them when rolling back on failure.
  [_addNodeToTrashList] (node, retire = false) {
    const paths = [node.path, ...node.binPaths]
    const moves = this.#retiredPaths
    log.silly('reify', 'mark', retire ? 'retired' : 'deleted', paths)
    for (const path of paths) {
      if (retire) {
        const retired = retirePath(path)
        moves[path] = retired
        this[_trashList].add(retired)
      } else {
        this[_trashList].add(path)
      }
    }
  }

  // move aside the shallowest nodes in the tree that have to be
  // changed or removed, so that we can rollback if necessary.
  [_retireShallowNodes] () {
    const timeEnd = time.start('reify:retireShallow')
    const moves = this.#retiredPaths = {}
    for (const diff of this.diff.children) {
      if (diff.action === 'CHANGE' || diff.action === 'REMOVE') {
        // we'll have to clean these up at the end, so add them to the list
        this[_addNodeToTrashList](diff.actual, true)
      }
    }
    log.silly('reify', 'moves', moves)
    const movePromises = Object.entries(moves)
      .map(([from, to]) => this[_renamePath](from, to))
    return promiseAllRejectLate(movePromises).then(timeEnd)
  }

  [_renamePath] (from, to, didMkdirp = false) {
    return moveFile(from, to)
      .catch(er => {
        // Occasionally an expected bin file might not exist in the package,
        // or a shim/symlink might have been moved aside.  If we've already
        // handled the most common cause of ENOENT (dir doesn't exist yet),
        // then just ignore any ENOENT.
        if (er.code === 'ENOENT') {
          return didMkdirp ? null : mkdir(dirname(to), { recursive: true }).then(() =>
            this[_renamePath](from, to, true))
        } else if (er.code === 'EEXIST') {
          return rm(to, { recursive: true, force: true }).then(() => moveFile(from, to))
        } else {
          throw er
        }
      })
  }

  [_rollbackRetireShallowNodes] (er) {
    const timeEnd = time.start('reify:rollback:retireShallow')
    const moves = this.#retiredPaths
    const movePromises = Object.entries(moves)
      .map(([from, to]) => this[_renamePath](to, from))
    return promiseAllRejectLate(movePromises)
      // ignore subsequent rollback errors
      .catch(() => {})
      .then(timeEnd)
      .then(() => {
        throw er
      })
  }

  // adding to the trash list will skip reifying, and delete them
  // if they are currently in the tree and otherwise untouched.
  [_addOmitsToTrashList] () {
    if (!this.#omitDev && !this.#omitOptional && !this.#omitPeer) {
      return
    }

    const timeEnd = time.start('reify:trashOmits')

    for (const node of this.idealTree.inventory.values()) {
      const { top } = node

      // if the top is not the root or workspace then we do not want to omit it
      if (!top.isProjectRoot && !top.isWorkspace) {
        continue
      }

      // if a diff filter has been created, then we do not omit the node if the
      // top node is not in that set
      if (this.diff?.filterSet?.size && !this.diff.filterSet.has(top)) {
        continue
      }

      // omit node if the dep type matches any omit flags that were set
      if (
        node.peer && this.#omitPeer ||
        node.dev && this.#omitDev ||
        node.optional && this.#omitOptional ||
        node.devOptional && this.#omitOptional && this.#omitDev
      ) {
        this[_addNodeToTrashList](node)
      }
    }

    timeEnd()
  }

  [_createSparseTree] () {
    const timeEnd = time.start('reify:createSparse')
    // if we call this fn again, we look for the previous list
    // so that we can avoid making the same directory multiple times
    const leaves = this.diff.leaves
      .filter(diff => {
        return (diff.action === 'ADD' || diff.action === 'CHANGE') &&
          !this.#sparseTreeDirs.has(diff.ideal.path) &&
          !diff.ideal.isLink
      })
      .map(diff => diff.ideal)

    // we check this in parallel, so guard against multiple attempts to
    // retire the same path at the same time.
    const dirsChecked = new Set()
    return promiseAllRejectLate(leaves.map(async node => {
      for (const d of walkUp(node.path)) {
        if (d === node.top.path) {
          break
        }
        if (dirsChecked.has(d)) {
          continue
        }
        dirsChecked.add(d)
        const st = await lstat(d).catch(() => null)
        // this can happen if we have a link to a package with a name
        // that the filesystem treats as if it is the same thing.
        // would be nice to have conditional istanbul ignores here...
        /* istanbul ignore next - defense in depth */
        if (st && !st.isDirectory()) {
          const retired = retirePath(d)
          this.#retiredPaths[d] = retired
          this[_trashList].add(retired)
          await this[_renamePath](d, retired)
        }
      }
      this.#sparseTreeDirs.add(node.path)
      const made = await mkdir(node.path, { recursive: true })
      // if the directory already exists, made will be undefined. if that's the case
      // we don't want to remove it because we aren't the ones who created it so we
      // omit it from the #sparseTreeRoots
      if (made) {
        this.#sparseTreeRoots.add(made)
      }
    })).then(timeEnd)
  }

  [_rollbackCreateSparseTree] (er) {
    const timeEnd = time.start('reify:rollback:createSparse')
    // cut the roots of the sparse tree that were created, not the leaves
    const roots = this.#sparseTreeRoots
    // also delete the moves that we retired, so that we can move them back
    const failures = []
    const targets = [...roots, ...Object.keys(this.#retiredPaths)]
    const unlinks = targets
      .map(path => rm(path, { recursive: true, force: true }).catch(er => failures.push([path, er])))
    return promiseAllRejectLate(unlinks).then(() => {
      // eslint-disable-next-line promise/always-return
      if (failures.length) {
        log.warn('cleanup', 'Failed to remove some directories', failures)
      }
    })
      .then(timeEnd)
      .then(() => this[_rollbackRetireShallowNodes](er))
  }

  // shrinkwrap nodes define their dependency branches with a file, so
  // we need to unpack them, read that shrinkwrap file, and then update
  // the tree by calling loadVirtual with the node as the root.
  [_loadShrinkwrapsAndUpdateTrees] () {
    const seen = this.#shrinkwrapInflated
    const shrinkwraps = this.diff.leaves
      .filter(d => (d.action === 'CHANGE' || d.action === 'ADD' || !d.action) &&
        d.ideal.hasShrinkwrap && !seen.has(d.ideal) &&
        !this[_trashList].has(d.ideal.path))

    if (!shrinkwraps.length) {
      return
    }

    const timeEnd = time.start('reify:loadShrinkwraps')

    const Arborist = this.constructor
    return promiseAllRejectLate(shrinkwraps.map(diff => {
      const node = diff.ideal
      seen.add(node)
      return diff.action ? this[_reifyNode](node) : node
    }))
      .then(nodes => promiseAllRejectLate(nodes.map(node => new Arborist({
        ...this.options,
        path: node.path,
      }).loadVirtual({ root: node }))))
      // reload the diff and sparse tree because the ideal tree changed
      .then(() => this[_diffTrees]())
      .then(() => this[_createSparseTree]())
      .then(() => this[_addOmitsToTrashList]())
      .then(() => this[_loadShrinkwrapsAndUpdateTrees]())
      .then(timeEnd)
  }

  // create a symlink for Links, extract for Nodes
  // return the node object, since we usually want that
  // handle optional dep failures here
  // If node is in trash list, skip it
  // If reifying fails, and the node is optional, add it and its optionalSet
  // to the trash list
  // Always return the node.
  [_reifyNode] (node) {
    if (this[_trashList].has(node.path)) {
      return node
    }

    const timeEnd = time.start(`reifyNode:${node.location}`)
    this.addTracker('reify', node.name, node.location)

    const { npmVersion, nodeVersion, cpu, os, libc } = this.options
    const p = Promise.resolve().then(async () => {
      // when we reify an optional node, check the engine and platform
      // first. be sure to ignore the --force and --engine-strict flags,
      // since we always want to skip any optional packages we can't install.
      // these checks throwing will result in a rollback and removal
      // of the mismatches
      // eslint-disable-next-line promise/always-return
      if (node.optional) {
        checkEngine(node.package, npmVersion, nodeVersion, false)
        checkPlatform(node.package, false, { cpu, os, libc })
      }
      await this[_checkBins](node)
      await this.#extractOrLink(node)
      const { _id, deprecated } = node.package
      // The .catch is in _handleOptionalFailure.  Not ideal, this should be cleaned up.
      // eslint-disable-next-line promise/always-return
      if (deprecated) {
        log.warn('deprecated', `${_id}: ${deprecated}`)
      }
    })

    return this[_handleOptionalFailure](node, p)
      .then(() => {
        this.finishTracker('reify', node.name, node.location)
        timeEnd()
        return node
      })
  }

  // do not allow node_modules to be a symlink
  async #validateNodeModules (nm) {
    if (this.options.force || this.#nmValidated.has(nm)) {
      return
    }
    const st = await lstat(nm).catch(() => null)
    if (!st || st.isDirectory()) {
      this.#nmValidated.add(nm)
      return
    }
    log.warn('reify', 'Removing non-directory', nm)
    await rm(nm, { recursive: true, force: true })
  }

  async #extractOrLink (node) {
    const nm = resolve(node.parent.path, 'node_modules')
    await this.#validateNodeModules(nm)

    if (!node.isLink) {
      // in normal cases, node.resolved should *always* be set by now.
      // however, it is possible when a lockfile is damaged, or very old,
      // or in some other race condition bugs in npm v6, that a previously
      // bundled dependency will have just a version, but no resolved value,
      // and no 'bundled: true' setting.
      // Do the best with what we have, or else remove it from the tree
      // entirely, since we can't possibly reify it.
      let res = null
      if (node.resolved) {
        const registryResolved = this.#registryResolved(node.resolved)
        if (registryResolved) {
          res = `${node.name}@${registryResolved}`
        }
      } else if (node.package.name && node.version) {
        res = `${node.package.name}@${node.version}`
      }

      // no idea what this thing is.  remove it from the tree.
      if (!res) {
        const warning = 'invalid or damaged lockfile detected\n' +
          'please re-try this operation once it completes\n' +
          'so that the damage can be corrected, or perform\n' +
          'a fresh install with no lockfile if the problem persists.'
        log.warn('reify', warning)
        log.verbose('reify', 'unrecognized node in tree', node.path)
        node.parent = null
        node.fsParent = null
        this[_addNodeToTrashList](node)
        return
      }
      await debug(async () => {
        const st = await lstat(node.path).catch(() => null)
        if (st && !st.isDirectory()) {
          debug.log('unpacking into a non-directory', node)
          throw Object.assign(new Error('ENOTDIR: not a directory'), {
            code: 'ENOTDIR',
            path: node.path,
          })
        }
      })
      await pacote.extract(res, node.path, {
        ...this.options,
        resolved: node.resolved,
        integrity: node.integrity,
      })
      // store nodes don't use Node class so node.package doesn't get updated
      if (node.isInStore) {
        const pkg = await rpj(join(node.path, 'package.json'))
        node.package.scripts = pkg.scripts
      }
      return
    }

    // node.isLink
    await rm(node.path, { recursive: true, force: true })

    // symlink
    const dir = dirname(node.path)
    const target = node.realpath
    const rel = relative(dir, target)
    await mkdir(dir, { recursive: true })
    return symlink(rel, node.path, 'junction')
  }

  // if the node is optional, then the failure of the promise is nonfatal
  // just add it and its optional set to the trash list.
  [_handleOptionalFailure] (node, p) {
    return (node.optional ? p.catch(() => {
      const set = optionalSet(node)
      for (node of set) {
        log.verbose('reify', 'failed optional dependency', node.path)
        this[_addNodeToTrashList](node)
      }
    }) : p).then(() => node)
  }

  #registryResolved (resolved) {
    // the default registry url is a magic value meaning "the currently
    // configured registry".
    // `resolved` must never be falsey.
    //
    // XXX: use a magic string that isn't also a valid value, like
    // ${REGISTRY} or something.  This has to be threaded through the
    // Shrinkwrap and Node classes carefully, so for now, just treat
    // the default reg as the magical animal that it has been.
    const resolvedURL = hgi.parseUrl(resolved)

    if (!resolvedURL) {
      // if we could not parse the url at all then returning nothing
      // here means it will get removed from the tree in the next step
      return
    }

    if ((this.options.replaceRegistryHost === resolvedURL.hostname)
      || this.options.replaceRegistryHost === 'always') {
      // this.registry always has a trailing slash
      return `${this.registry.slice(0, -1)}${resolvedURL.pathname}${resolvedURL.searchParams}`
    }

    return resolved
  }

  // bundles are *sort of* like shrinkwraps, in that the branch is defined
  // by the contents of the package.  however, in their case, rather than
  // shipping a virtual tree that must be reified, they ship an entire
  // reified actual tree that must be unpacked and not modified.
  [_loadBundlesAndUpdateTrees] (depth = 0, bundlesByDepth) {
    let maxBundleDepth
    if (!bundlesByDepth) {
      bundlesByDepth = new Map()
      maxBundleDepth = -1
      dfwalk({
        tree: this.diff,
        visit: diff => {
          const node = diff.ideal
          if (!node) {
            return
          }
          if (node.isProjectRoot) {
            return
          }

          const { bundleDependencies } = node.package
          if (bundleDependencies && bundleDependencies.length) {
            maxBundleDepth = Math.max(maxBundleDepth, node.depth)
            if (!bundlesByDepth.has(node.depth)) {
              bundlesByDepth.set(node.depth, [node])
            } else {
              bundlesByDepth.get(node.depth).push(node)
            }
          }
        },
        getChildren: diff => diff.children,
      })

      bundlesByDepth.set('maxBundleDepth', maxBundleDepth)
    } else {
      maxBundleDepth = bundlesByDepth.get('maxBundleDepth')
    }

    if (depth === 0) {
      time.start('reify:loadBundles')
    }

    if (depth > maxBundleDepth) {
      // if we did something, then prune the tree and update the diffs
      if (maxBundleDepth !== -1) {
        this.#pruneBundledMetadeps(bundlesByDepth)
        this[_diffTrees]()
      }
      time.end('reify:loadBundles')
      return
    }

    // skip any that have since been removed from the tree, eg by a
    // shallower bundle overwriting them with a bundled meta-dep.
    const set = (bundlesByDepth.get(depth) || [])
      .filter(node => node.root === this.idealTree &&
        node.target !== node.root &&
        !this[_trashList].has(node.path))

    if (!set.length) {
      return this[_loadBundlesAndUpdateTrees](depth + 1, bundlesByDepth)
    }

    // extract all the nodes with bundles
    return promiseCallLimit(set.map(node => {
      return () => {
        this.#bundleUnpacked.add(node)
        return this[_reifyNode](node)
      }
    }), { rejectLate: true })
    // then load their unpacked children and move into the ideal tree
      .then(nodes =>
        promiseAllRejectLate(nodes.map(async node => {
          const arb = new this.constructor({
            ...this.options,
            path: node.path,
          })
          const notTransplanted = new Set(node.children.keys())
          await arb.loadActual({
            root: node,
            // don't transplant any sparse folders we created
            // loadActual will set node.package to {} for empty directories
            // if by chance there are some empty folders in the node_modules
            // tree for some other reason, then ok, ignore those too.
            transplantFilter: node => {
              if (node.package._id) {
                // it's actually in the bundle if it gets transplanted
                notTransplanted.delete(node.name)
                return true
              } else {
                return false
              }
            },
          })
          for (const name of notTransplanted) {
            this.#bundleMissing.add(node.children.get(name))
          }
        })))
    // move onto the next level of bundled items
      .then(() => this[_loadBundlesAndUpdateTrees](depth + 1, bundlesByDepth))
  }

  // https://github.com/npm/cli/issues/1597#issuecomment-667639545
  #pruneBundledMetadeps (bundlesByDepth) {
    const bundleShadowed = new Set()

    // Example dep graph:
    // root -> (a, c)
    // a -> BUNDLE(b)
    // b -> c
    // c -> b
    //
    // package tree:
    // root
    // +-- a
    // |   +-- b(1)
    // |   +-- c(1)
    // +-- b(2)
    // +-- c(2)
    // 1. mark everything that's shadowed by anything in the bundle.  This
    //    marks b(2) and c(2).
    // 2. anything with edgesIn from outside the set, mark not-extraneous,
    //    remove from set.  This unmarks c(2).
    // 3. continue until no change
    // 4. remove everything in the set from the tree.  b(2) is pruned

    // create the list of nodes shadowed by children of bundlers
    for (const bundles of bundlesByDepth.values()) {
      // skip the 'maxBundleDepth' item
      if (!Array.isArray(bundles)) {
        continue
      }
      for (const node of bundles) {
        for (const name of node.children.keys()) {
          const shadow = node.parent.resolve(name)
          if (!shadow) {
            continue
          }
          bundleShadowed.add(shadow)
          shadow.extraneous = true
        }
      }
    }

    // lib -> (a@1.x) BUNDLE(a@1.2.3 (b@1.2.3))
    // a@1.2.3 -> (b@1.2.3)
    // a@1.3.0 -> (b@2)
    // b@1.2.3 -> ()
    // b@2 -> (c@2)
    //
    // root
    // +-- lib
    // |   +-- a@1.2.3
    // |   +-- b@1.2.3
    // +-- b@2 <-- shadowed, now extraneous
    // +-- c@2 <-- also shadowed, because only dependent is shadowed
    for (const shadow of bundleShadowed) {
      for (const shadDep of shadow.edgesOut.values()) {
        /* istanbul ignore else - pretty unusual situation, just being
         * defensive here. Would mean that a bundled dep has a dependency
         * that is unmet. which, weird, but if you bundle it, we take
         * whatever you put there and assume the publisher knows best. */
        if (shadDep.to) {
          bundleShadowed.add(shadDep.to)
          shadDep.to.extraneous = true
        }
      }
    }

    let changed
    do {
      changed = false
      for (const shadow of bundleShadowed) {
        for (const edge of shadow.edgesIn) {
          if (!bundleShadowed.has(edge.from)) {
            shadow.extraneous = false
            bundleShadowed.delete(shadow)
            changed = true
            break
          }
        }
      }
    } while (changed)

    for (const shadow of bundleShadowed) {
      this[_addNodeToTrashList](shadow)
      shadow.root = null
    }
  }

  async [_submitQuickAudit] () {
    if (this.options.audit === false) {
      this.auditReport = null
      return
    }

    // we submit the quick audit at this point in the process, as soon as
    // we have all the deps resolved, so that it can overlap with the other
    // actions as much as possible.  Stash the promise, which we resolve
    // before finishing the reify() and returning the tree.  Thus, we do
    // NOT return the promise, as the intent is for this to run in parallel
    // with the reification, and be resolved at a later time.
    const timeEnd = time.start('reify:audit')
    const options = { ...this.options }
    const tree = this.idealTree

    // if we're operating on a workspace, only audit the workspace deps
    if (this.options.workspaces.length) {
      options.filterSet = this.workspaceDependencySet(
        tree,
        this.options.workspaces,
        this.options.includeWorkspaceRoot
      )
    }

    this.auditReport = AuditReport.load(tree, options).then(res => {
      timeEnd()
      return res
    })
  }

  // ok!  actually unpack stuff into their target locations!
  // The sparse tree has already been created, so we walk the diff
  // kicking off each unpack job.  If any fail, we rm the sparse
  // tree entirely and try to put everything back where it was.
  [_unpackNewModules] () {
    const timeEnd = time.start('reify:unpack')
    const unpacks = []
    dfwalk({
      tree: this.diff,
      visit: diff => {
        // no unpacking if we don't want to change this thing
        if (diff.action !== 'CHANGE' && diff.action !== 'ADD') {
          return
        }

        const node = diff.ideal
        const bd = this.#bundleUnpacked.has(node)
        const sw = this.#shrinkwrapInflated.has(node)
        const bundleMissing = this.#bundleMissing.has(node)

        // check whether we still need to unpack this one.
        // test the inDepBundle last, since that's potentially a tree walk.
        const doUnpack = node && // can't unpack if removed!
          // root node already exists
          !node.isRoot &&
          // already unpacked to read bundle
          !bd &&
          // already unpacked to read sw
          !sw &&
          // already unpacked by another dep's bundle
          (bundleMissing || !node.inDepBundle)

        if (doUnpack) {
          unpacks.push(this[_reifyNode](node))
        }
      },
      getChildren: diff => diff.children,
    })
    return promiseAllRejectLate(unpacks).then(timeEnd)
  }

  // This is the part where we move back the unchanging nodes that were
  // the children of a node that did change.  If this fails, the rollback
  // is a three-step process.  First, we try to move the retired unchanged
  // nodes BACK to their retirement folders, then delete the sparse tree,
  // then move everything out of retirement.
  [_moveBackRetiredUnchanged] () {
    // get a list of all unchanging children of any shallow retired nodes
    // if they are not the ancestor of any node in the diff set, then the
    // directory won't already exist, so just rename it over.
    // This is sort of an inverse diff tree, of all the nodes where
    // the actualTree and idealTree _don't_ differ, starting from the
    // shallowest nodes that we moved aside in the first place.
    const timeEnd = time.start('reify:unretire')
    const moves = this.#retiredPaths
    this.#retiredUnchanged = {}
    return promiseAllRejectLate(this.diff.children.map(diff => {
      // skip if nothing was retired
      if (diff.action !== 'CHANGE' && diff.action !== 'REMOVE') {
        return
      }

      const { path: realFolder } = diff.actual
      const retireFolder = moves[realFolder]
      /* istanbul ignore next - should be impossible */
      debug(() => {
        if (!retireFolder) {
          const er = new Error('trying to un-retire but not retired')
          throw Object.assign(er, {
            realFolder,
            retireFolder,
            actual: diff.actual,
            ideal: diff.ideal,
            action: diff.action,
          })
        }
      })

      this.#retiredUnchanged[retireFolder] = []
      return promiseAllRejectLate(diff.unchanged.map(node => {
        // no need to roll back links, since we'll just delete them anyway
        if (node.isLink) {
          return mkdir(dirname(node.path), { recursive: true, force: true })
            .then(() => this[_reifyNode](node))
        }

        // will have been moved/unpacked along with bundler
        if (node.inDepBundle && !this.#bundleMissing.has(node)) {
          return
        }

        this.#retiredUnchanged[retireFolder].push(node)

        const rel = relative(realFolder, node.path)
        const fromPath = resolve(retireFolder, rel)
        // if it has bundleDependencies, then make node_modules.  otherwise
        // skip it.
        const bd = node.package.bundleDependencies
        const dir = bd && bd.length ? node.path + '/node_modules' : node.path
        return mkdir(dir, { recursive: true }).then(() => this[_moveContents](node, fromPath))
      }))
    })).then(timeEnd)
  }

  // move the contents from the fromPath to the node.path
  [_moveContents] (node, fromPath) {
    return packageContents({
      path: fromPath,
      depth: 1,
      packageJsonCache: new Map([[fromPath + '/package.json', node.package]]),
    }).then(res => promiseAllRejectLate(res.map(path => {
      const rel = relative(fromPath, path)
      const to = resolve(node.path, rel)
      return this[_renamePath](path, to)
    })))
  }

  [_rollbackMoveBackRetiredUnchanged] (er) {
    const moves = this.#retiredPaths
    // flip the mapping around to go back
    const realFolders = new Map(Object.entries(moves).map(([k, v]) => [v, k]))
    const promises = Object.entries(this.#retiredUnchanged)
      .map(([retireFolder, nodes]) => promiseAllRejectLate(nodes.map(node => {
        const realFolder = realFolders.get(retireFolder)
        const rel = relative(realFolder, node.path)
        const fromPath = resolve(retireFolder, rel)
        return this[_moveContents]({ ...node, path: fromPath }, node.path)
      })))
    return promiseAllRejectLate(promises)
      .then(() => this[_rollbackCreateSparseTree](er))
  }

  [_build] () {
    const timeEnd = time.start('reify:build')

    // for all the things being installed, run their appropriate scripts
    // run in tip->root order, so as to be more likely to build a node's
    // deps before attempting to build it itself
    const nodes = []
    dfwalk({
      tree: this.diff,
      leave: diff => {
        if (!diff.ideal.isProjectRoot) {
          nodes.push(diff.ideal)
        }
      },
      // process adds before changes, ignore removals
      getChildren: diff => diff && diff.children,
      filter: diff => diff.action === 'ADD' || diff.action === 'CHANGE',
    })

    // pick up link nodes from the unchanged list as we want to run their
    // scripts in every install despite of having a diff status change
    for (const node of this.diff.unchanged) {
      const tree = node.root.target

      // skip links that only live within node_modules as they are most
      // likely managed by packages we installed, we only want to rebuild
      // unchanged links we directly manage
      const linkedFromRoot = node.parent === tree || node.target.fsTop === tree
      if (node.isLink && linkedFromRoot) {
        nodes.push(node)
      }
    }

    return this.rebuild({ nodes, handleOptionalFailure: true }).then(timeEnd)
  }

  // the tree is pretty much built now, so it's cleanup time.
  // remove the retired folders, and any deleted nodes
  // If this fails, there isn't much we can do but tell the user about it.
  // Thankfully, it's pretty unlikely that it'll fail, since rm is a node builtin.
  async [_removeTrash] () {
    const timeEnd = time.start('reify:trash')
    const promises = []
    const failures = []
    const _rm = path => rm(path, { recursive: true, force: true }).catch(er => failures.push([path, er]))

    for (const path of this[_trashList]) {
      promises.push(_rm(path))
    }

    await promiseAllRejectLate(promises)
    if (failures.length) {
      log.warn('cleanup', 'Failed to remove some directories', failures)
    }

    timeEnd()
  }

  // last but not least, we save the ideal tree metadata to the package-lock
  // or shrinkwrap file, and any additions or removals to package.json
  async [_saveIdealTree] (options) {
    // the ideal tree is actualized now, hooray!
    // it still contains all the references to optional nodes that were removed
    // for install failures.  Those still end up in the shrinkwrap, so we
    // save it first, then prune out the optional trash, and then return it.

    const save = !(options.save === false)

    // we check for updates in order to make sure we run save ideal tree
    // even though save=false since we want `npm update` to be able to
    // write to package-lock files by default
    const hasUpdates = this[_updateAll] || this[_updateNames].length

    // we're going to completely skip save ideal tree in case of a global or
    // dry-run install and also if the save option is set to false, EXCEPT for
    // update since the expected behavior for npm7+ is for update to
    // NOT save to package.json, we make that exception since we still want
    // saveIdealTree to be able to write the lockfile by default.
    const saveIdealTree = !(
      (!save && !hasUpdates)
      || this.options.global
      || this.options.dryRun
    )

    if (!saveIdealTree) {
      return false
    }

    const timeEnd = time.start('reify:save')

    const updatedTrees = new Set()
    const updateNodes = nodes => {
      for (const { name, tree: addTree } of nodes) {
        // addTree either the root, or a workspace
        const edge = addTree.edgesOut.get(name)
        const pkg = addTree.package
        const req = npa.resolve(name, edge.spec, addTree.realpath)
        const { rawSpec, subSpec } = req

        const spec = subSpec ? subSpec.rawSpec : rawSpec
        const child = edge.to

        // if we tried to install an optional dep, but it was a version
        // that we couldn't resolve, this MAY be missing.  if we haven't
        // blown up by now, it's because it was not a problem, though, so
        // just move on.
        if (!child || !addTree.isTop) {
          continue
        }

        let newSpec
        // True if the dependency is getting installed from a local file path
        // In this case it is not possible to do the normal version comparisons
        // as the new version will be a file path
        const isLocalDep = req.type === 'directory' || req.type === 'file'
        if (req.registry) {
          const version = child.version
          const prefixRange = version ? this.options.savePrefix + version : '*'
          // if we installed a range, then we save the range specified
          // if it is not a subset of the ^x.y.z.  eg, installing a range
          // of `1.x <1.2.3` will not be saved as `^1.2.0`, because that
          // would allow versions outside the requested range.  Tags and
          // specific versions save with the save-prefix.
          const isRange = (subSpec || req).type === 'range'

          let range = spec
          if (
            !isRange ||
            spec === '*' ||
            subset(prefixRange, spec, { loose: true })
          ) {
            range = prefixRange
          }

          const pname = child.packageName
          const alias = name !== pname
          newSpec = alias ? `npm:${pname}@${range}` : range
        } else if (req.hosted) {
          // save the git+https url if it has auth, otherwise shortcut
          const h = req.hosted
          const opt = { noCommittish: false }
          if (h.https && h.auth) {
            newSpec = `git+${h.https(opt)}`
          } else {
            newSpec = h.shortcut(opt)
          }
        } else if (isLocalDep) {
          // when finding workspace nodes, make sure that
          // we save them using their version instead of
          // using their relative path
          if (edge.type === 'workspace') {
            const { version } = edge.to.target
            const prefixRange = version ? this.options.savePrefix + version : '*'
            newSpec = prefixRange
          } else {
            // save the relative path in package.json
            // Normally saveSpec is updated with the proper relative
            // path already, but it's possible to specify a full absolute
            // path initially, in which case we can end up with the wrong
            // thing, so just get the ultimate fetchSpec and relativize it.
            const p = req.fetchSpec.replace(/^file:/, '')
            const rel = relpath(addTree.realpath, p).replace(/#/g, '%23')
            newSpec = `file:${rel}`
          }
        } else {
          newSpec = req.saveSpec
        }

        if (options.saveType) {
          const depType = saveTypeMap.get(options.saveType)
          pkg[depType][name] = newSpec
          // rpj will have moved it here if it was in both
          // if it is empty it will be deleted later
          if (options.saveType === 'prod' && pkg.optionalDependencies) {
            delete pkg.optionalDependencies[name]
          }
        } else {
          if (hasSubKey(pkg, 'dependencies', name)) {
            pkg.dependencies[name] = newSpec
          }

          if (hasSubKey(pkg, 'devDependencies', name)) {
            pkg.devDependencies[name] = newSpec
            // don't update peer or optional if we don't have to
            if (hasSubKey(pkg, 'peerDependencies', name) && (isLocalDep || !intersects(newSpec, pkg.peerDependencies[name]))) {
              pkg.peerDependencies[name] = newSpec
            }

            if (hasSubKey(pkg, 'optionalDependencies', name) && (isLocalDep || !intersects(newSpec, pkg.optionalDependencies[name]))) {
              pkg.optionalDependencies[name] = newSpec
            }
          } else {
            if (hasSubKey(pkg, 'peerDependencies', name)) {
              pkg.peerDependencies[name] = newSpec
            }

            if (hasSubKey(pkg, 'optionalDependencies', name)) {
              pkg.optionalDependencies[name] = newSpec
            }
          }
        }

        updatedTrees.add(addTree)
      }
    }

    // Returns true if any of the edges from this node has a semver
    // range definition that is an exact match to the version installed
    // e.g: should return true if for a given an installed version 1.0.0,
    // range is either =1.0.0 or 1.0.0
    const exactVersion = node => {
      for (const edge of node.edgesIn) {
        try {
          if (semver.subset(edge.spec, node.version)) {
            return false
          }
        } catch {
          // ignore errors
        }
      }
      return true
    }

    // helper that retrieves an array of nodes that were
    // potentially updated during the reify process, in order
    // to limit the number of nodes to check and update, only
    // select nodes from the inventory that are direct deps
    // of a given package.json (project root or a workspace)
    // and in ase of using a list of `names`, restrict nodes
    // to only names that are found in this list
    const retrieveUpdatedNodes = names => {
      const filterDirectDependencies = node =>
        !node.isRoot && node.resolveParent && node.resolveParent.isRoot
        && (!names || names.includes(node.name))
        && exactVersion(node) // skip update for exact ranges

      const directDeps = this.idealTree.inventory
        .filter(filterDirectDependencies)

      // traverses the list of direct dependencies and collect all nodes
      // to be updated, since any of them might have changed during reify
      const nodes = []
      for (const node of directDeps) {
        for (const edgeIn of node.edgesIn) {
          nodes.push({
            name: node.name,
            tree: edgeIn.from.target,
          })
        }
      }
      return nodes
    }

    if (save) {
      // when using update all alongside with save, we'll make
      // sure to refresh every dependency of the root idealTree
      if (this[_updateAll]) {
        const nodes = retrieveUpdatedNodes()
        updateNodes(nodes)
      } else {
        // resolvedAdd is the list of user add requests, but with names added
        // to things like git repos and tarball file/urls.  However, if the
        // user requested 'foo@', and we have a foo@file:../foo, then we should
        // end up saving the spec we actually used, not whatever they gave us.
        if (this[_resolvedAdd].length) {
          updateNodes(this[_resolvedAdd])
        }

        // if updating given dependencies by name, restrict the list of
        // nodes to check to only those currently in _updateNames
        if (this[_updateNames].length) {
          const nodes = retrieveUpdatedNodes(this[_updateNames])
          updateNodes(nodes)
        }

        // grab any from explicitRequests that had deps removed
        for (const { from: tree } of this.explicitRequests) {
          updatedTrees.add(tree)
        }
      }
    }

    if (save) {
      for (const tree of updatedTrees) {
        // refresh the edges so they have the correct specs
        tree.package = tree.package
        const pkgJson = await PackageJson.load(tree.path, { create: true })
        const {
          dependencies = {},
          devDependencies = {},
          optionalDependencies = {},
          peerDependencies = {},
          // bundleDependencies is not required by PackageJson like the other
          // fields here PackageJson also doesn't omit an empty array for this
          // field so defaulting this to an empty array would add that field to
          // every package.json file.
          bundleDependencies,
        } = tree.package

        pkgJson.update({
          dependencies,
          devDependencies,
          optionalDependencies,
          peerDependencies,
          bundleDependencies,
        })
        await pkgJson.save()
      }
    }

    // before now edge specs could be changing, affecting the `requires` field
    // in the package lock, so we hold off saving to the very last action
    if (this[_usePackageLock]) {
      // preserve indentation, if possible
      let format = this.idealTree.package[Symbol.for('indent')]
      if (format === undefined) {
        format = '  '
      }

      // TODO this ignores options.save
      await this.idealTree.meta.save({
        format: (this.options.formatPackageLock && format) ? format
        : this.options.formatPackageLock,
      })
    }

    timeEnd()
    return true
  }
}
