// mix-in implementing the loadActual method

const { relative, dirname, resolve, join, normalize } = require('node:path')

const rpj = require('read-package-json-fast')
const { readdirScoped } = require('@npmcli/fs')
const { walkUp } = require('walk-up-path')
const ancestorPath = require('common-ancestor-path')
const treeCheck = require('../tree-check.js')

const Shrinkwrap = require('../shrinkwrap.js')
const calcDepFlags = require('../calc-dep-flags.js')
const Node = require('../node.js')
const Link = require('../link.js')
const realpath = require('../realpath.js')

// public symbols
const _changePath = Symbol.for('_changePath')
const _setWorkspaces = Symbol.for('setWorkspaces')
const _rpcache = Symbol.for('realpathCache')
const _stcache = Symbol.for('statCache')

module.exports = cls => class ActualLoader extends cls {
  #actualTree
  // ensure when walking the tree that we don't call loadTree on the same
  // actual node more than one time.
  #actualTreeLoaded = new Set()
  #actualTreePromise

  // cache of nodes when loading the actualTree, so that we avoid loaded the
  // same node multiple times when symlinks attack.
  #cache = new Map()
  #filter

  // cache of link targets for setting fsParent links
  // We don't do fsParent as a magic getter/setter, because it'd be too costly
  // to keep up to date along the walk.
  // And, we know that it can ONLY be relevant when the node is a target of a
  // link, otherwise it'd be in a node_modules folder, so take advantage of
  // that to limit the scans later.
  #topNodes = new Set()
  #transplantFilter

  constructor (options) {
    super(options)

    // the tree of nodes on disk
    this.actualTree = options.actualTree

    // caches for cached realpath calls
    const cwd = process.cwd()
    // assume that the cwd is real enough for our purposes
    this[_rpcache] = new Map([[cwd, cwd]])
    this[_stcache] = new Map()
  }

  // public method
  // TODO remove options param in next semver major
  async loadActual (options = {}) {
    // In the past this.actualTree was set as a promise that eventually
    // resolved, and overwrite this.actualTree with the resolved value.  This
    // was a problem because virtually no other code expects this.actualTree to
    // be a promise.  Instead we only set it once resolved, and also return it
    // from the promise so that it is what's returned from this function when
    // awaited.
    if (this.actualTree) {
      return this.actualTree
    }
    if (!this.#actualTreePromise) {
      // allow the user to set options on the ctor as well.
      // XXX: deprecate separate method options objects.
      options = { ...this.options, ...options }

      this.#actualTreePromise = this.#loadActual(options)
        .then(tree => {
          // reset all deps to extraneous prior to recalc
          if (!options.root) {
            for (const node of tree.inventory.values()) {
              node.extraneous = true
            }
          }

          // only reset root flags if we're not re-rooting,
          // otherwise leave as-is
          calcDepFlags(tree, !options.root)
          this.actualTree = treeCheck(tree)
          return this.actualTree
        })
    }
    return this.#actualTreePromise
  }

  // return the promise so that we don't ever have more than one going at the
  // same time.  This is so that buildIdealTree can default to the actualTree
  // if no shrinkwrap present, but reify() can still call buildIdealTree and
  // loadActual in parallel safely.

  async #loadActual (options) {
    // mostly realpath to throw if the root doesn't exist
    const {
      global,
      filter = () => true,
      root = null,
      transplantFilter = () => true,
      ignoreMissing = false,
      forceActual = false,
    } = options
    this.#filter = filter
    this.#transplantFilter = transplantFilter

    if (global) {
      const real = await realpath(this.path, this[_rpcache], this[_stcache])
      const params = {
        path: this.path,
        realpath: real,
        pkg: {},
        global,
        loadOverrides: true,
      }
      if (this.path === real) {
        this.#actualTree = this.#newNode(params)
      } else {
        this.#actualTree = await this.#newLink(params)
      }
    } else {
      // not in global mode, hidden lockfile is allowed, load root pkg too
      this.#actualTree = await this.#loadFSNode({
        path: this.path,
        real: await realpath(this.path, this[_rpcache], this[_stcache]),
        loadOverrides: true,
      })

      this.#actualTree.assertRootOverrides()

      // if forceActual is set, don't even try the hidden lockfile
      if (!forceActual) {
        // Note: hidden lockfile will be rejected if it's not the latest thing
        // in the folder, or if any of the entries in the hidden lockfile are
        // missing.
        const meta = await Shrinkwrap.load({
          path: this.#actualTree.path,
          hiddenLockfile: true,
          resolveOptions: this.options,
        })

        if (meta.loadedFromDisk) {
          this.#actualTree.meta = meta
          // have to load on a new Arborist object, so we don't assign
          // the virtualTree on this one!  Also, the weird reference is because
          // we can't easily get a ref to Arborist in this module, without
          // creating a circular reference, since this class is a mixin used
          // to build up the Arborist class itself.
          await new this.constructor({ ...this.options }).loadVirtual({
            root: this.#actualTree,
          })
          await this[_setWorkspaces](this.#actualTree)

          this.#transplant(root)
          return this.#actualTree
        }
      }

      const meta = await Shrinkwrap.load({
        path: this.#actualTree.path,
        lockfileVersion: this.options.lockfileVersion,
        resolveOptions: this.options,
      })
      this.#actualTree.meta = meta
    }

    await this.#loadFSTree(this.#actualTree)
    await this[_setWorkspaces](this.#actualTree)

    // if there are workspace targets without Link nodes created, load
    // the targets, so that we know what they are.
    if (this.#actualTree.workspaces && this.#actualTree.workspaces.size) {
      const promises = []
      for (const path of this.#actualTree.workspaces.values()) {
        if (!this.#cache.has(path)) {
          // workspace overrides use the root overrides
          const p = this.#loadFSNode({ path, root: this.#actualTree, useRootOverrides: true })
            .then(node => this.#loadFSTree(node))
          promises.push(p)
        }
      }
      await Promise.all(promises)
    }

    if (!ignoreMissing) {
      await this.#findMissingEdges()
    }

    // try to find a node that is the parent in a fs tree sense, but not a
    // node_modules tree sense, of any link targets.  this allows us to
    // resolve deps that node will find, but a legacy npm view of the
    // world would not have noticed.
    for (const path of this.#topNodes) {
      const node = this.#cache.get(path)
      if (node && !node.parent && !node.fsParent) {
        for (const p of walkUp(dirname(path))) {
          if (this.#cache.has(p)) {
            node.fsParent = this.#cache.get(p)
            break
          }
        }
      }
    }

    this.#transplant(root)

    if (global) {
      // need to depend on the children, or else all of them
      // will end up being flagged as extraneous, since the
      // global root isn't a "real" project
      const tree = this.#actualTree
      const actualRoot = tree.isLink ? tree.target : tree
      const { dependencies = {} } = actualRoot.package
      for (const [name, kid] of actualRoot.children.entries()) {
        const def = kid.isLink ? `file:${kid.realpath.replace(/#/g, '%23')}` : '*'
        dependencies[name] = dependencies[name] || def
      }
      actualRoot.package = { ...actualRoot.package, dependencies }
    }
    return this.#actualTree
  }

  #transplant (root) {
    if (!root || root === this.#actualTree) {
      return
    }

    this.#actualTree[_changePath](root.path)
    for (const node of this.#actualTree.children.values()) {
      if (!this.#transplantFilter(node)) {
        node.root = null
      }
    }

    root.replace(this.#actualTree)
    for (const node of this.#actualTree.fsChildren) {
      node.root = this.#transplantFilter(node) ? root : null
    }

    this.#actualTree = root
  }

  async #loadFSNode ({ path, parent, real, root, loadOverrides, useRootOverrides }) {
    if (!real) {
      try {
        real = await realpath(path, this[_rpcache], this[_stcache])
      } catch (error) {
        // if realpath fails, just provide a dummy error node
        return new Node({
          error,
          path,
          realpath: path,
          parent,
          root,
          loadOverrides,
        })
      }
    }

    const cached = this.#cache.get(path)
    let node
    // missing edges get a dummy node, assign the parent and return it
    if (cached && !cached.dummy) {
      cached.parent = parent
      return cached
    } else {
      const params = {
        installLinks: this.installLinks,
        legacyPeerDeps: this.legacyPeerDeps,
        path,
        realpath: real,
        parent,
        root,
        loadOverrides,
      }

      try {
        const pkg = await rpj(join(real, 'package.json'))
        params.pkg = pkg
        if (useRootOverrides && root.overrides) {
          params.overrides = root.overrides.getNodeRule({ name: pkg.name, version: pkg.version })
        }
      } catch (err) {
        params.error = err
      }

      // soldier on if read-package-json raises an error, passing it to the
      // Node which will attach it to its errors array (Link passes it along to
      // its target node)
      if (normalize(path) === real) {
        node = this.#newNode(params)
      } else {
        node = await this.#newLink(params)
      }
    }
    this.#cache.set(path, node)
    return node
  }

  #newNode (options) {
    // check it for an fsParent if it's a tree top.  there's a decent chance
    // it'll get parented later, making the fsParent scan a no-op, but better
    // safe than sorry, since it's cheap.
    const { parent, realpath } = options
    if (!parent) {
      this.#topNodes.add(realpath)
    }
    return new Node(options)
  }

  async #newLink (options) {
    const { realpath } = options
    this.#topNodes.add(realpath)
    const target = this.#cache.get(realpath)
    const link = new Link({ ...options, target })

    if (!target) {
      // Link set its target itself in this case
      this.#cache.set(realpath, link.target)
      // if a link target points at a node outside of the root tree's
      // node_modules hierarchy, then load that node as well.
      await this.#loadFSTree(link.target)
    }

    return link
  }

  async #loadFSTree (node) {
    const did = this.#actualTreeLoaded
    if (!node.isLink && !did.has(node.target.realpath)) {
      did.add(node.target.realpath)
      await this.#loadFSChildren(node.target)
      return Promise.all(
        [...node.target.children.entries()]
          .filter(([, kid]) => !did.has(kid.realpath))
          .map(([, kid]) => this.#loadFSTree(kid))
      )
    }
  }

  // create child nodes for all the entries in node_modules
  // and attach them to the node as a parent
  async #loadFSChildren (node) {
    const nm = resolve(node.realpath, 'node_modules')
    try {
      const kids = await readdirScoped(nm).then(paths => paths.map(p => p.replace(/\\/g, '/')))
      return Promise.all(
        // ignore . dirs and retired scoped package folders
        kids.filter(kid => !/^(@[^/]+\/)?\./.test(kid))
          .filter(kid => this.#filter(node, kid))
          .map(kid => this.#loadFSNode({
            parent: node,
            path: resolve(nm, kid),
          })))
    } catch {
      // error in the readdir is not fatal, just means no kids
    }
  }

  async #findMissingEdges () {
    // try to resolve any missing edges by walking up the directory tree,
    // checking for the package in each node_modules folder.  stop at the
    // root directory.
    // The tricky move here is that we load a "dummy" node for the folder
    // containing the node_modules folder, so that it can be assigned as
    // the fsParent.  It's a bad idea to *actually* load that full node,
    // because people sometimes develop in ~/projects/node_modules/...
    // so we'd end up loading a massive tree with lots of unrelated junk.
    const nmContents = new Map()
    const tree = this.#actualTree
    for (const node of tree.inventory.values()) {
      const ancestor = ancestorPath(node.realpath, this.path)

      const depPromises = []
      for (const [name, edge] of node.edgesOut.entries()) {
        const notMissing = !edge.missing &&
          !(edge.to && (edge.to.dummy || edge.to.parent !== node))
        if (notMissing) {
          continue
        }

        // start the walk from the dirname, because we would have found
        // the dep in the loadFSTree step already if it was local.
        for (const p of walkUp(dirname(node.realpath))) {
          // only walk as far as the nearest ancestor
          // this keeps us from going into completely unrelated
          // places when a project is just missing something, but
          // allows for finding the transitive deps of link targets.
          // ie, if it has to go up and back out to get to the path
          // from the nearest common ancestor, we've gone too far.
          if (ancestor && /^\.\.(?:[\\/]|$)/.test(relative(ancestor, p))) {
            break
          }

          let entries
          if (!nmContents.has(p)) {
            entries = await readdirScoped(p + '/node_modules')
              .catch(() => []).then(paths => paths.map(p => p.replace(/\\/g, '/')))
            nmContents.set(p, entries)
          } else {
            entries = nmContents.get(p)
          }

          if (!entries.includes(name)) {
            continue
          }

          let d
          if (!this.#cache.has(p)) {
            d = new Node({ path: p, root: node.root, dummy: true })
            this.#cache.set(p, d)
          } else {
            d = this.#cache.get(p)
          }
          if (d.dummy) {
            // it's a placeholder, so likely would not have loaded this dep,
            // unless another dep in the tree also needs it.
            const depPath = normalize(`${p}/node_modules/${name}`)
            const cached = this.#cache.get(depPath)
            if (!cached || cached.dummy) {
              depPromises.push(this.#loadFSNode({
                path: depPath,
                root: node.root,
                parent: d,
              }).then(node => this.#loadFSTree(node)))
            }
          }
          break
        }
      }
      await Promise.all(depPromises)
    }
  }
}
