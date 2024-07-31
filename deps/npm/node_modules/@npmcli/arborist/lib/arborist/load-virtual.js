// mixin providing the loadVirtual method
const mapWorkspaces = require('@npmcli/map-workspaces')

const { resolve } = require('node:path')

const nameFromFolder = require('@npmcli/name-from-folder')
const consistentResolve = require('../consistent-resolve.js')
const Shrinkwrap = require('../shrinkwrap.js')
const Node = require('../node.js')
const Link = require('../link.js')
const relpath = require('../relpath.js')
const calcDepFlags = require('../calc-dep-flags.js')
const rpj = require('read-package-json-fast')
const treeCheck = require('../tree-check.js')

const flagsSuspect = Symbol.for('flagsSuspect')
const setWorkspaces = Symbol.for('setWorkspaces')

module.exports = cls => class VirtualLoader extends cls {
  #rootOptionProvided

  constructor (options) {
    super(options)

    // the virtual tree we load from a shrinkwrap
    this.virtualTree = options.virtualTree
    this[flagsSuspect] = false
  }

  // public method
  async loadVirtual (options = {}) {
    if (this.virtualTree) {
      return this.virtualTree
    }

    // allow the user to set reify options on the ctor as well.
    // XXX: deprecate separate reify() options object.
    options = { ...this.options, ...options }

    if (options.root && options.root.meta) {
      await this.#loadFromShrinkwrap(options.root.meta, options.root)
      return treeCheck(this.virtualTree)
    }

    const s = await Shrinkwrap.load({
      path: this.path,
      lockfileVersion: this.options.lockfileVersion,
      resolveOptions: this.options,
    })
    if (!s.loadedFromDisk && !options.root) {
      const er = new Error('loadVirtual requires existing shrinkwrap file')
      throw Object.assign(er, { code: 'ENOLOCK' })
    }

    // when building the ideal tree, we pass in a root node to this function
    // otherwise, load it from the root package json or the lockfile
    const {
      root = await this.#loadRoot(s),
    } = options

    this.#rootOptionProvided = options.root

    await this.#loadFromShrinkwrap(s, root)
    root.assertRootOverrides()
    return treeCheck(this.virtualTree)
  }

  async #loadRoot (s) {
    const pj = this.path + '/package.json'
    const pkg = await rpj(pj).catch(() => s.data.packages['']) || {}
    return this[setWorkspaces](this.#loadNode('', pkg, true))
  }

  async #loadFromShrinkwrap (s, root) {
    if (!this.#rootOptionProvided) {
      // root is never any of these things, but might be a brand new
      // baby Node object that never had its dep flags calculated.
      root.extraneous = false
      root.dev = false
      root.optional = false
      root.devOptional = false
      root.peer = false
    } else {
      this[flagsSuspect] = true
    }

    this.#checkRootEdges(s, root)
    root.meta = s
    this.virtualTree = root
    const { links, nodes } = this.#resolveNodes(s, root)
    await this.#resolveLinks(links, nodes)
    if (!(s.originalLockfileVersion >= 2)) {
      this.#assignBundles(nodes)
    }
    if (this[flagsSuspect]) {
      // reset all dep flags
      // can't use inventory here, because virtualTree might not be root
      for (const node of nodes.values()) {
        if (node.isRoot || node === this.#rootOptionProvided) {
          continue
        }
        node.extraneous = true
        node.dev = true
        node.optional = true
        node.devOptional = true
        node.peer = true
      }
      calcDepFlags(this.virtualTree, !this.#rootOptionProvided)
    }
    return root
  }

  // check the lockfile deps, and see if they match.  if they do not
  // then we have to reset dep flags at the end.  for example, if the
  // user manually edits their package.json file, then we need to know
  // that the idealTree is no longer entirely trustworthy.
  #checkRootEdges (s, root) {
    // loaded virtually from tree, no chance of being out of sync
    // ancient lockfiles are critically damaged by this process,
    // so we need to just hope for the best in those cases.
    if (!s.loadedFromDisk || s.ancientLockfile) {
      return
    }

    const lock = s.get('')
    const prod = lock.dependencies || {}
    const dev = lock.devDependencies || {}
    const optional = lock.optionalDependencies || {}
    const peer = lock.peerDependencies || {}
    const peerOptional = {}

    if (lock.peerDependenciesMeta) {
      for (const [name, meta] of Object.entries(lock.peerDependenciesMeta)) {
        if (meta.optional && peer[name] !== undefined) {
          peerOptional[name] = peer[name]
          delete peer[name]
        }
      }
    }

    for (const name of Object.keys(optional)) {
      delete prod[name]
    }

    const lockWS = {}
    const workspaces = mapWorkspaces.virtual({
      cwd: this.path,
      lockfile: s.data,
    })

    for (const [name, path] of workspaces.entries()) {
      lockWS[name] = `file:${path.replace(/#/g, '%23')}`
    }

    // Should rootNames exclude optional?
    const rootNames = new Set(root.edgesOut.keys())

    const lockByType = ({ dev, optional, peer, peerOptional, prod, workspace: lockWS })

    // Find anything in shrinkwrap deps that doesn't match root's type or spec
    for (const type in lockByType) {
      const deps = lockByType[type]
      for (const name in deps) {
        const edge = root.edgesOut.get(name)
        if (!edge || edge.type !== type || edge.spec !== deps[name]) {
          return this[flagsSuspect] = true
        }
        rootNames.delete(name)
      }
    }
    // Something was in root that's not accounted for in shrinkwrap
    if (rootNames.size) {
      return this[flagsSuspect] = true
    }
  }

  // separate out link metadatas, and create Node objects for nodes
  #resolveNodes (s, root) {
    const links = new Map()
    const nodes = new Map([['', root]])
    for (const [location, meta] of Object.entries(s.data.packages)) {
      // skip the root because we already got it
      if (!location) {
        continue
      }

      if (meta.link) {
        links.set(location, meta)
      } else {
        nodes.set(location, this.#loadNode(location, meta))
      }
    }
    return { links, nodes }
  }

  // links is the set of metadata, and nodes is the map of non-Link nodes
  // Set the targets to nodes in the set, if we have them (we might not)
  async #resolveLinks (links, nodes) {
    for (const [location, meta] of links.entries()) {
      const targetPath = resolve(this.path, meta.resolved)
      const targetLoc = relpath(this.path, targetPath)
      const target = nodes.get(targetLoc)
      const link = this.#loadLink(location, targetLoc, target, meta)
      nodes.set(location, link)
      nodes.set(targetLoc, link.target)

      // we always need to read the package.json for link targets
      // outside node_modules because they can be changed by the local user
      if (!link.target.parent) {
        const pj = link.realpath + '/package.json'
        const pkg = await rpj(pj).catch(() => null)
        if (pkg) {
          link.target.package = pkg
        }
      }
    }
  }

  #assignBundles (nodes) {
    for (const [location, node] of nodes) {
      // Skip assignment of parentage for the root package
      if (!location || node.isLink && !node.target.location) {
        continue
      }
      const { name, parent, package: { inBundle } } = node

      if (!parent) {
        continue
      }

      // read inBundle from package because 'package' here is
      // actually a v2 lockfile metadata entry.
      // If the *parent* is also bundled, though, or if the parent has
      // no dependency on it, then we assume that it's being pulled in
      // just by virtue of its parent or a transitive dep being bundled.
      const { package: ppkg } = parent
      const { inBundle: parentBundled } = ppkg
      if (inBundle && !parentBundled && parent.edgesOut.has(node.name)) {
        if (!ppkg.bundleDependencies) {
          ppkg.bundleDependencies = [name]
        } else {
          ppkg.bundleDependencies.push(name)
        }
      }
    }
  }

  #loadNode (location, sw, loadOverrides) {
    const p = this.virtualTree ? this.virtualTree.realpath : this.path
    const path = resolve(p, location)
    // shrinkwrap doesn't include package name unless necessary
    if (!sw.name) {
      sw.name = nameFromFolder(path)
    }

    const dev = sw.dev
    const optional = sw.optional
    const devOptional = dev || optional || sw.devOptional
    const peer = sw.peer

    const node = new Node({
      installLinks: this.installLinks,
      legacyPeerDeps: this.legacyPeerDeps,
      root: this.virtualTree,
      path,
      realpath: path,
      integrity: sw.integrity,
      resolved: consistentResolve(sw.resolved, this.path, path),
      pkg: sw,
      hasShrinkwrap: sw.hasShrinkwrap,
      dev,
      optional,
      devOptional,
      peer,
      loadOverrides,
    })
    // cast to boolean because they're undefined in the lock file when false
    node.extraneous = !!sw.extraneous
    node.devOptional = !!(sw.devOptional || sw.dev || sw.optional)
    node.peer = !!sw.peer
    node.optional = !!sw.optional
    node.dev = !!sw.dev
    return node
  }

  #loadLink (location, targetLoc, target) {
    const path = resolve(this.path, location)
    const link = new Link({
      installLinks: this.installLinks,
      legacyPeerDeps: this.legacyPeerDeps,
      path,
      realpath: resolve(this.path, targetLoc),
      target,
      pkg: target && target.package,
    })
    link.extraneous = target.extraneous
    link.devOptional = target.devOptional
    link.peer = target.peer
    link.optional = target.optional
    link.dev = target.dev
    return link
  }
}
