// inventory, path, realpath, root, and parent
//
// node.root is a reference to the root module in the tree (ie, typically the
// cwd project folder)
//
// node.location is the /-delimited path from the root module to the node.  In
// the case of link targets that may be outside of the root's package tree,
// this can include some number of /../ path segments.  The location of the
// root module is always '.'.  node.location thus never contains drive letters
// or absolute paths, and is portable within a given project, suitable for
// inclusion in lockfiles and metadata.
//
// node.path is the path to the place where this node lives on disk.  It is
// system-specific and absolute.
//
// node.realpath is the path to where the module actually resides on disk.  In
// the case of non-link nodes, node.realpath is equivalent to node.path.  In
// the case of link nodes, it is equivalent to node.target.path.
//
// Setting node.parent will set the node's root to the parent's root, as well
// as updating edgesIn and edgesOut to reload dependency resolutions as needed,
// and setting node.path to parent.path/node_modules/name.
//
// node.inventory is a Map of name to a Set() of all the nodes under a given
// root by that name.  It's empty for non-root nodes, and changing the root
// reference will remove it from the old root's inventory and add it to the new
// one.  This map is useful for cases like `npm update foo` or `npm ls foo`
// where we need to quickly find all instances of a given package name within a
// tree.

const semver = require('semver')
const nameFromFolder = require('@npmcli/name-from-folder')
const Edge = require('./edge.js')
const Inventory = require('./inventory.js')
const OverrideSet = require('./override-set.js')
const { normalize } = require('read-package-json-fast')
const { getPaths: getBinPaths } = require('bin-links')
const npa = require('npm-package-arg')
const debug = require('./debug.js')
const gatherDepSet = require('./gather-dep-set.js')
const treeCheck = require('./tree-check.js')
const { walkUp } = require('walk-up-path')

const { resolve, relative, dirname, basename } = require('node:path')
const util = require('node:util')
const _package = Symbol('_package')
const _parent = Symbol('_parent')
const _target = Symbol.for('_target')
const _fsParent = Symbol('_fsParent')
const _reloadNamedEdges = Symbol('_reloadNamedEdges')
// overridden by Link class
const _loadDeps = Symbol.for('Arborist.Node._loadDeps')
const _refreshLocation = Symbol.for('_refreshLocation')
const _changePath = Symbol.for('_changePath')
// used by Link class as well
const _delistFromMeta = Symbol.for('_delistFromMeta')
const _explain = Symbol('_explain')
const _explanation = Symbol('_explanation')

const relpath = require('./relpath.js')
const consistentResolve = require('./consistent-resolve.js')

const printableTree = require('./printable.js')
const CaseInsensitiveMap = require('./case-insensitive-map.js')

const querySelectorAll = require('./query-selector-all.js')

class Node {
  #global
  #meta
  #root
  #workspaces

  constructor (options) {
    // NB: path can be null if it's a link target
    const {
      root,
      path,
      realpath,
      parent,
      error,
      meta,
      fsParent,
      resolved,
      integrity,
      // allow setting name explicitly when we haven't set a path yet
      name,
      children,
      fsChildren,
      installLinks = false,
      legacyPeerDeps = false,
      linksIn,
      isInStore = false,
      hasShrinkwrap,
      overrides,
      loadOverrides = false,
      extraneous = true,
      dev = true,
      optional = true,
      devOptional = true,
      peer = true,
      global = false,
      dummy = false,
      sourceReference = null,
    } = options
    // this object gives querySelectorAll somewhere to stash context about a node
    // while processing a query
    this.queryContext = {}

    // true if part of a global install
    this.#global = global

    this.#workspaces = null

    this.errors = error ? [error] : []
    this.isInStore = isInStore

    // this will usually be null, except when modeling a
    // package's dependencies in a virtual root.
    this.sourceReference = sourceReference

    // TODO if this came from pacote.manifest we don't have to do this,
    // we can be told to skip this step
    const pkg = sourceReference ? sourceReference.package
      : normalize(options.pkg || {})

    this.name = name ||
      nameFromFolder(path || pkg.name || realpath) ||
      pkg.name ||
      null

    // should be equal if not a link
    this.path = path ? resolve(path) : null

    if (!this.name && (!this.path || this.path !== dirname(this.path))) {
      throw new TypeError('could not detect node name from path or package')
    }

    this.realpath = !this.isLink ? this.path : resolve(realpath)

    this.resolved = resolved || null
    if (!this.resolved) {
      // note: this *only* works for non-file: deps, so we avoid even
      // trying here.
      // file: deps are tracked in package.json will _resolved set to the
      // full path to the tarball or link target.  However, if the package
      // is checked into git or moved to another location, that's 100% not
      // portable at all!  The _where and _location don't provide much help,
      // since _location is just where the module ended up in the tree,
      // and _where can be different than the actual root if it's a
      // meta-dep deeper in the dependency graph.
      //
      // If we don't have the other oldest indicators of legacy npm, then it's
      // probably what we're getting from pacote, which IS trustworthy.
      //
      // Otherwise, hopefully a shrinkwrap will help us out.
      const resolved = consistentResolve(pkg._resolved)
      if (resolved && !(/^file:/.test(resolved) && pkg._where)) {
        this.resolved = resolved
      }
    }
    this.integrity = integrity || pkg._integrity || null
    this.hasShrinkwrap = hasShrinkwrap || pkg._hasShrinkwrap || false
    this.installLinks = installLinks
    this.legacyPeerDeps = legacyPeerDeps

    this.children = new CaseInsensitiveMap()
    this.fsChildren = new Set()
    this.inventory = new Inventory()
    this.tops = new Set()
    this.linksIn = new Set(linksIn || [])

    // these three are set by an Arborist taking a catalog
    // after the tree is built.  We don't get this along the way,
    // because they have a tendency to change as new children are
    // added, especially when they're deduped.  Eg, a dev dep may be
    // a 3-levels-deep dependency of a non-dev dep.  If we calc the
    // flags along the way, then they'll tend to be invalid  by the
    // time we need to look at them.
    if (!dummy) {
      this.dev = dev
      this.optional = optional
      this.devOptional = devOptional
      this.peer = peer
      this.extraneous = extraneous
      this.dummy = false
    } else {
      // true if this is a placeholder for the purpose of serving as a
      // fsParent to link targets that get their deps resolved outside
      // the root tree folder.
      this.dummy = true
      this.dev = false
      this.optional = false
      this.devOptional = false
      this.peer = false
      this.extraneous = false
    }

    this.edgesIn = new Set()
    this.edgesOut = new CaseInsensitiveMap()

    // have to set the internal package ref before assigning the parent,
    // because this.package is read when adding to inventory
    this[_package] = pkg && typeof pkg === 'object' ? pkg : {}

    if (overrides) {
      this.overrides = overrides
    } else if (loadOverrides) {
      const overrides = this[_package].overrides || {}
      if (Object.keys(overrides).length > 0) {
        this.overrides = new OverrideSet({
          overrides: this[_package].overrides,
        })
      }
    }

    // only relevant for the root and top nodes
    this.meta = meta

    // Note: this is _slightly_ less efficient for the initial tree
    // building than it could be, but in exchange, it's a much simpler
    // algorithm.
    // If this node has a bunch of children, and those children satisfy
    // its various deps, then we're going to _first_ create all the
    // edges, and _then_ assign the children into place, re-resolving
    // them all in _reloadNamedEdges.
    // A more efficient, but more complicated, approach would be to
    // flag this node as being a part of a tree build, so it could
    // hold off on resolving its deps until its children are in place.

    // call the parent setter
    // Must be set prior to calling _loadDeps, because top-ness is relevant

    // will also assign root if present on the parent
    this[_parent] = null
    this.parent = parent || null

    this[_fsParent] = null
    this.fsParent = fsParent || null

    // see parent/root setters below.
    // root is set to parent's root if we have a parent, otherwise if it's
    // null, then it's set to the node itself.
    if (!parent && !fsParent) {
      this.root = root || null
    }

    // mostly a convenience for testing, but also a way to create
    // trees in a more declarative way than setting parent on each
    if (children) {
      for (const c of children) {
        new Node({ ...c, parent: this })
      }
    }
    if (fsChildren) {
      for (const c of fsChildren) {
        new Node({ ...c, fsParent: this })
      }
    }

    // now load all the dep edges
    this[_loadDeps]()
  }

  get meta () {
    return this.#meta
  }

  set meta (meta) {
    this.#meta = meta
    if (meta) {
      meta.add(this)
    }
  }

  get global () {
    if (this.#root === this) {
      return this.#global
    }
    return this.#root.global
  }

  // true for packages installed directly in the global node_modules folder
  get globalTop () {
    return this.global && this.parent && this.parent.isProjectRoot
  }

  get workspaces () {
    return this.#workspaces
  }

  set workspaces (workspaces) {
    // deletes edges if they already exists
    if (this.#workspaces) {
      for (const name of this.#workspaces.keys()) {
        if (!workspaces.has(name)) {
          this.edgesOut.get(name).detach()
        }
      }
    }

    this.#workspaces = workspaces
    this.#loadWorkspaces()
    this[_loadDeps]()
  }

  get binPaths () {
    if (!this.parent) {
      return []
    }

    return getBinPaths({
      pkg: this[_package],
      path: this.path,
      global: this.global,
      top: this.globalTop,
    })
  }

  get hasInstallScript () {
    const { hasInstallScript, scripts } = this.package
    const { install, preinstall, postinstall } = scripts || {}
    return !!(hasInstallScript || install || preinstall || postinstall)
  }

  get version () {
    return this[_package].version || ''
  }

  get packageName () {
    return this[_package].name || null
  }

  get pkgid () {
    const { name = '', version = '' } = this.package
    // root package will prefer package name over folder name,
    // and never be called an alias.
    const { isProjectRoot } = this
    const myname = isProjectRoot ? name || this.name
      : this.name
    const alias = !isProjectRoot && name && myname !== name ? `npm:${name}@`
      : ''
    return `${myname}@${alias}${version}`
  }

  get overridden () {
    return !!(this.overrides && this.overrides.value && this.overrides.name === this.name)
  }

  get package () {
    return this[_package]
  }

  set package (pkg) {
    // just detach them all.  we could make this _slightly_ more efficient
    // by only detaching the ones that changed, but we'd still have to walk
    // them all, and the comparison logic gets a bit tricky.  we generally
    // only do this more than once at the root level, so the resolve() calls
    // are only one level deep, and there's not much to be saved, anyway.
    // simpler to just toss them all out.
    for (const edge of this.edgesOut.values()) {
      edge.detach()
    }

    this[_explanation] = null
    /* istanbul ignore next - should be impossible */
    if (!pkg || typeof pkg !== 'object') {
      debug(() => {
        throw new Error('setting Node.package to non-object')
      })
      pkg = {}
    }
    this[_package] = pkg
    this.#loadWorkspaces()
    this[_loadDeps]()
    // do a hard reload, since the dependents may now be valid or invalid
    // as a result of the package change.
    this.edgesIn.forEach(edge => edge.reload(true))
  }

  // node.explain(nodes seen already, edge we're trying to satisfy
  // if edge is not specified, it lists every edge into the node.
  explain (edge = null, seen = []) {
    if (this[_explanation]) {
      return this[_explanation]
    }

    return this[_explanation] = this[_explain](edge, seen)
  }

  [_explain] (edge, seen) {
    if (this.isProjectRoot && !this.sourceReference) {
      return {
        location: this.path,
      }
    }

    const why = {
      name: this.isProjectRoot || this.isTop ? this.packageName : this.name,
      version: this.package.version,
    }
    if (this.errors.length || !this.packageName || !this.package.version) {
      why.errors = this.errors.length ? this.errors : [
        new Error('invalid package: lacks name and/or version'),
      ]
      why.package = this.package
    }

    if (this.root.sourceReference) {
      const { name, version } = this.root.package
      why.whileInstalling = {
        name,
        version,
        path: this.root.sourceReference.path,
      }
    }

    if (this.sourceReference) {
      return this.sourceReference.explain(edge, seen)
    }

    if (seen.includes(this)) {
      return why
    }

    why.location = this.location
    why.isWorkspace = this.isWorkspace

    // make a new list each time.  we can revisit, but not loop.
    seen = seen.concat(this)

    why.dependents = []
    if (edge) {
      why.dependents.push(edge.explain(seen))
    } else {
      // ignore invalid edges, since those aren't satisfied by this thing,
      // and are not keeping it held in this spot anyway.
      const edges = []
      for (const edge of this.edgesIn) {
        if (!edge.valid && !edge.from.isProjectRoot) {
          continue
        }

        edges.push(edge)
      }
      for (const edge of edges) {
        why.dependents.push(edge.explain(seen))
      }
    }

    if (this.linksIn.size) {
      why.linksIn = [...this.linksIn].map(link => link[_explain](edge, seen))
    }

    return why
  }

  isDescendantOf (node) {
    for (let p = this; p; p = p.resolveParent) {
      if (p === node) {
        return true
      }
    }
    return false
  }

  getBundler (path = []) {
    // made a cycle, definitely not bundled!
    if (path.includes(this)) {
      return null
    }

    path.push(this)

    const parent = this[_parent]
    if (!parent) {
      return null
    }

    const pBundler = parent.getBundler(path)
    if (pBundler) {
      return pBundler
    }

    const ppkg = parent.package
    const bd = ppkg && ppkg.bundleDependencies
    // explicit bundling
    if (Array.isArray(bd) && bd.includes(this.name)) {
      return parent
    }

    // deps that are deduped up to the bundling level are bundled.
    // however, if they get their dep met further up than that,
    // then they are not bundled.  Ie, installing a package with
    // unmet bundled deps will not cause your deps to be bundled.
    for (const edge of this.edgesIn) {
      const eBundler = edge.from.getBundler(path)
      if (!eBundler) {
        continue
      }

      if (eBundler === parent) {
        return eBundler
      }
    }

    return null
  }

  get inBundle () {
    return !!this.getBundler()
  }

  // when reifying, if a package is technically in a bundleDependencies list,
  // but that list is the root project, we still have to install it.  This
  // getter returns true if it's in a dependency's bundle list, not the root's.
  get inDepBundle () {
    const bundler = this.getBundler()
    return !!bundler && bundler !== this.root
  }

  get isWorkspace () {
    if (this.isProjectRoot) {
      return false
    }
    const { root } = this
    const { type, to } = root.edgesOut.get(this.packageName) || {}
    return type === 'workspace' && to && (to.target === this || to === this)
  }

  get isRoot () {
    return this === this.root
  }

  get isProjectRoot () {
    // only treat as project root if it's the actual link that is the root,
    // or the target of the root link, but NOT if it's another link to the
    // same root that happens to be somewhere else.
    return this === this.root || this === this.root.target
  }

  get isRegistryDependency () {
    if (this.edgesIn.size === 0) {
      return false
    }
    for (const edge of this.edgesIn) {
      if (!npa(edge.spec).registry) {
        return false
      }
    }
    return true
  }

  * ancestry () {
    for (let anc = this; anc; anc = anc.resolveParent) {
      yield anc
    }
  }

  set root (root) {
    // setting to null means this is the new root
    // should only ever be one step
    while (root && root.root !== root) {
      root = root.root
    }

    root = root || this

    // delete from current root inventory
    this[_delistFromMeta]()

    // can't set the root (yet) if there's no way to determine location
    // this allows us to do new Node({...}) and then set the root later.
    // just make the assignment so we don't lose it, and move on.
    if (!this.path || !root.realpath || !root.path) {
      this.#root = root
      return
    }

    // temporarily become a root node
    this.#root = this

    // break all linksIn, we're going to re-set them if needed later
    for (const link of this.linksIn) {
      link[_target] = null
      this.linksIn.delete(link)
    }

    // temporarily break this link as well, we'll re-set if possible later
    const { target } = this
    if (this.isLink) {
      if (target) {
        target.linksIn.delete(this)
        if (target.root === this) {
          target[_delistFromMeta]()
        }
      }
      this[_target] = null
    }

    // if this is part of a cascading root set, then don't do this bit
    // but if the parent/fsParent is in a different set, we have to break
    // that reference before proceeding
    if (this.parent && this.parent.root !== root) {
      this.parent.children.delete(this.name)
      this[_parent] = null
    }
    if (this.fsParent && this.fsParent.root !== root) {
      this.fsParent.fsChildren.delete(this)
      this[_fsParent] = null
    }

    if (root === this) {
      this[_refreshLocation]()
    } else {
      // setting to some different node.
      const loc = relpath(root.realpath, this.path)
      const current = root.inventory.get(loc)

      // clobber whatever is there now
      if (current) {
        current.root = null
      }

      this.#root = root
      // set this.location and add to inventory
      this[_refreshLocation]()

      // try to find our parent/fsParent in the new root inventory
      for (const p of walkUp(dirname(this.path))) {
        if (p === this.path) {
          continue
        }
        const ploc = relpath(root.realpath, p)
        const parent = root.inventory.get(ploc)
        if (parent) {
          /* istanbul ignore next - impossible */
          if (parent.isLink) {
            debug(() => {
              throw Object.assign(new Error('assigning parentage to link'), {
                path: this.path,
                parent: parent.path,
                parentReal: parent.realpath,
              })
            })
            continue
          }
          const childLoc = `${ploc}${ploc ? '/' : ''}node_modules/${this.name}`
          const isParent = this.location === childLoc
          if (isParent) {
            const oldChild = parent.children.get(this.name)
            if (oldChild && oldChild !== this) {
              oldChild.root = null
            }
            if (this.parent) {
              this.parent.children.delete(this.name)
              this.parent[_reloadNamedEdges](this.name)
            }
            parent.children.set(this.name, this)
            this[_parent] = parent
            // don't do it for links, because they don't have a target yet
            // we'll hit them up a bit later on.
            if (!this.isLink) {
              parent[_reloadNamedEdges](this.name)
            }
          } else {
            /* istanbul ignore if - should be impossible, since we break
             * all fsParent/child relationships when moving? */
            if (this.fsParent) {
              this.fsParent.fsChildren.delete(this)
            }
            parent.fsChildren.add(this)
            this[_fsParent] = parent
          }
          break
        }
      }

      // if it doesn't have a parent, it's a top node
      if (!this.parent) {
        root.tops.add(this)
      } else {
        root.tops.delete(this)
      }

      // assign parentage for any nodes that need to have this as a parent
      // this can happen when we have a node at nm/a/nm/b added *before*
      // the node at nm/a, which might have the root node as a fsParent.
      // we can't rely on the public setter here, because it calls into
      // this function to set up these references!
      // check dirname so that /foo isn't treated as the fsparent of /foo-bar
      const nmloc = `${this.location}${this.location ? '/' : ''}node_modules/`
      // only walk top nodes, since anything else already has a parent.
      for (const child of root.tops) {
        const isChild = child.location === nmloc + child.name
        const isFsChild =
          dirname(child.path).startsWith(this.path) &&
          child !== this &&
          !child.parent &&
          (
            !child.fsParent ||
            child.fsParent === this ||
            dirname(this.path).startsWith(child.fsParent.path)
          )

        if (!isChild && !isFsChild) {
          continue
        }

        // set up the internal parentage links
        if (this.isLink) {
          child.root = null
        } else {
          // can't possibly have a parent, because it's in tops
          if (child.fsParent) {
            child.fsParent.fsChildren.delete(child)
          }
          child[_fsParent] = null
          if (isChild) {
            this.children.set(child.name, child)
            child[_parent] = this
            root.tops.delete(child)
          } else {
            this.fsChildren.add(child)
            child[_fsParent] = this
          }
        }
      }

      // look for any nodes with the same realpath.  either they're links
      // to that realpath, or a thing at that realpath if we're adding a link
      // (if we're adding a regular node, we already deleted the old one)
      for (const node of root.inventory.query('realpath', this.realpath)) {
        if (node === this) {
          continue
        }

        /* istanbul ignore next - should be impossible */
        debug(() => {
          if (node.root !== root) {
            throw new Error('inventory contains node from other root')
          }
        })

        if (this.isLink) {
          const target = node.target
          this[_target] = target
          this[_package] = target.package
          target.linksIn.add(this)
          // reload edges here, because now we have a target
          if (this.parent) {
            this.parent[_reloadNamedEdges](this.name)
          }
          break
        } else {
          /* istanbul ignore else - should be impossible */
          if (node.isLink) {
            node[_target] = this
            node[_package] = this.package
            this.linksIn.add(node)
            if (node.parent) {
              node.parent[_reloadNamedEdges](node.name)
            }
          } else {
            debug(() => {
              throw Object.assign(new Error('duplicate node in root setter'), {
                path: this.path,
                realpath: this.realpath,
                root: root.realpath,
              })
            })
          }
        }
      }
    }

    // reload all edgesIn where the root doesn't match, so we don't have
    // cross-tree dependency graphs
    for (const edge of this.edgesIn) {
      if (edge.from.root !== root) {
        edge.reload()
      }
    }
    // reload all edgesOut where root doens't match, or is missing, since
    // it might not be missing in the new tree
    for (const edge of this.edgesOut.values()) {
      if (!edge.to || edge.to.root !== root) {
        edge.reload()
      }
    }

    // now make sure our family comes along for the ride!
    const family = new Set([
      ...this.fsChildren,
      ...this.children.values(),
      ...this.inventory.values(),
    ].filter(n => n !== this))

    for (const child of family) {
      if (child.root !== root) {
        child[_delistFromMeta]()
        child[_parent] = null
        this.children.delete(child.name)
        child[_fsParent] = null
        this.fsChildren.delete(child)
        for (const l of child.linksIn) {
          l[_target] = null
          child.linksIn.delete(l)
        }
      }
    }
    for (const child of family) {
      if (child.root !== root) {
        child.root = root
      }
    }

    // if we had a target, and didn't find one in the new root, then bring
    // it over as well, but only if we're setting the link into a new root,
    // as we don't want to lose the target any time we remove a link.
    if (this.isLink && target && !this.target && root !== this) {
      target.root = root
    }

    if (!this.overrides && this.parent && this.parent.overrides) {
      this.overrides = this.parent.overrides.getNodeRule(this)
    }
    // tree should always be valid upon root setter completion.
    treeCheck(this)
    if (this !== root) {
      treeCheck(root)
    }
  }

  get root () {
    return this.#root || this
  }

  #loadWorkspaces () {
    if (!this.#workspaces) {
      return
    }

    for (const [name, path] of this.#workspaces.entries()) {
      new Edge({ from: this, name, spec: `file:${path.replace(/#/g, '%23')}`, type: 'workspace' })
    }
  }

  [_loadDeps] () {
    // Caveat!  Order is relevant!
    // Packages in optionalDependencies are optional.
    // Packages in both deps and devDeps are required.
    // Note the subtle breaking change from v6: it is no longer possible
    // to have a different spec for a devDep than production dep.

    // Linked targets that are disconnected from the tree are tops,
    // but don't have a 'path' field, only a 'realpath', because we
    // don't know their canonical location. We don't need their devDeps.
    const pd = this.package.peerDependencies
    const ad = this.package.acceptDependencies || {}
    if (pd && typeof pd === 'object' && !this.legacyPeerDeps) {
      const pm = this.package.peerDependenciesMeta || {}
      const peerDependencies = {}
      const peerOptional = {}
      for (const [name, dep] of Object.entries(pd)) {
        if (pm[name]?.optional) {
          peerOptional[name] = dep
        } else {
          peerDependencies[name] = dep
        }
      }
      this.#loadDepType(peerDependencies, 'peer', ad)
      this.#loadDepType(peerOptional, 'peerOptional', ad)
    }

    this.#loadDepType(this.package.dependencies, 'prod', ad)
    this.#loadDepType(this.package.optionalDependencies, 'optional', ad)

    const { globalTop, isTop, path, sourceReference } = this
    const {
      globalTop: srcGlobalTop,
      isTop: srcTop,
      path: srcPath,
    } = sourceReference || {}
    const thisDev = isTop && !globalTop && path
    const srcDev = !sourceReference || srcTop && !srcGlobalTop && srcPath
    if (thisDev && srcDev) {
      this.#loadDepType(this.package.devDependencies, 'dev', ad)
    }
  }

  #loadDepType (deps, type, ad) {
    // Because of the order in which _loadDeps runs, we always want to
    // prioritize a new edge over an existing one
    for (const [name, spec] of Object.entries(deps || {})) {
      const current = this.edgesOut.get(name)
      if (!current || current.type !== 'workspace') {
        new Edge({ from: this, name, spec, accept: ad[name], type })
      }
    }
  }

  get fsParent () {
    // in debug setter prevents fsParent from being this
    return this[_fsParent]
  }

  set fsParent (fsParent) {
    if (!fsParent) {
      if (this[_fsParent]) {
        this.root = null
      }
      return
    }

    debug(() => {
      if (fsParent === this) {
        throw new Error('setting node to its own fsParent')
      }

      if (fsParent.realpath === this.realpath) {
        throw new Error('setting fsParent to same path')
      }

      // the initial set MUST be an actual walk-up from the realpath
      // subsequent sets will re-root on the new fsParent's path.
      if (!this[_fsParent] && this.realpath.indexOf(fsParent.realpath) !== 0) {
        throw Object.assign(new Error('setting fsParent improperly'), {
          path: this.path,
          realpath: this.realpath,
          fsParent: {
            path: fsParent.path,
            realpath: fsParent.realpath,
          },
        })
      }
    })

    if (fsParent.isLink) {
      fsParent = fsParent.target
    }

    // setting a thing to its own fsParent is not normal, but no-op for safety
    if (this === fsParent || fsParent.realpath === this.realpath) {
      return
    }

    // nothing to do
    if (this[_fsParent] === fsParent) {
      return
    }

    const oldFsParent = this[_fsParent]
    const newPath = !oldFsParent ? this.path
      : resolve(fsParent.path, relative(oldFsParent.path, this.path))
    const nmPath = resolve(fsParent.path, 'node_modules', this.name)

    // this is actually the parent, set that instead
    if (newPath === nmPath) {
      this.parent = fsParent
      return
    }

    const pathChange = newPath !== this.path

    // remove from old parent/fsParent
    const oldParent = this.parent
    const oldName = this.name
    if (this.parent) {
      this.parent.children.delete(this.name)
      this[_parent] = null
    }
    if (this.fsParent) {
      this.fsParent.fsChildren.delete(this)
      this[_fsParent] = null
    }

    // update this.path/realpath for this and all children/fsChildren
    if (pathChange) {
      this[_changePath](newPath)
    }

    if (oldParent) {
      oldParent[_reloadNamedEdges](oldName)
    }

    // clobbers anything at that path, resets all appropriate references
    this.root = fsParent.root
  }

  // is it safe to replace one node with another?  check the edges to
  // make sure no one will get upset.  Note that the node might end up
  // having its own unmet dependencies, if the new node has new deps.
  // Note that there are cases where Arborist will opt to insert a node
  // into the tree even though this function returns false!  This is
  // necessary when a root dependency is added or updated, or when a
  // root dependency brings peer deps along with it.  In that case, we
  // will go ahead and create the invalid state, and then try to resolve
  // it with more tree construction, because it's a user request.
  canReplaceWith (node, ignorePeers) {
    if (node.name !== this.name) {
      return false
    }

    if (node.packageName !== this.packageName) {
      return false
    }

    // XXX need to check for two root nodes?
    if (node.overrides !== this.overrides) {
      return false
    }
    ignorePeers = new Set(ignorePeers)

    // gather up all the deps of this node and that are only depended
    // upon by deps of this node.  those ones don't count, since
    // they'll be replaced if this node is replaced anyway.
    const depSet = gatherDepSet([this], e => e.to !== this && e.valid)

    for (const edge of this.edgesIn) {
      // when replacing peer sets, we need to be able to replace the entire
      // peer group, which means we ignore incoming edges from other peers
      // within the replacement set.
      if (!this.isTop &&
        edge.from.parent === this.parent &&
        edge.peer &&
        ignorePeers.has(edge.from.name)) {
        continue
      }

      // only care about edges that don't originate from this node
      if (!depSet.has(edge.from) && !edge.satisfiedBy(node)) {
        return false
      }
    }

    return true
  }

  canReplace (node, ignorePeers) {
    return node.canReplaceWith(this, ignorePeers)
  }

  // return true if it's safe to remove this node, because anything that
  // is depending on it would be fine with the thing that they would resolve
  // to if it was removed, or nothing is depending on it in the first place.
  canDedupe (preferDedupe = false) {
    // not allowed to mess with shrinkwraps or bundles
    if (this.inDepBundle || this.inShrinkwrap) {
      return false
    }

    // it's a top level pkg, or a dep of one
    if (!this.resolveParent || !this.resolveParent.resolveParent) {
      return false
    }

    // no one wants it, remove it
    if (this.edgesIn.size === 0) {
      return true
    }

    const other = this.resolveParent.resolveParent.resolve(this.name)

    // nothing else, need this one
    if (!other) {
      return false
    }

    // if it's the same thing, then always fine to remove
    if (other.matches(this)) {
      return true
    }

    // if the other thing can't replace this, then skip it
    if (!other.canReplace(this)) {
      return false
    }

    // if we prefer dedupe, or if the version is greater/equal, take the other
    if (preferDedupe || semver.gte(other.version, this.version)) {
      return true
    }

    return false
  }

  satisfies (requested) {
    if (requested instanceof Edge) {
      return this.name === requested.name && requested.satisfiedBy(this)
    }

    const parsed = npa(requested)
    const { name = this.name, rawSpec: spec } = parsed
    return this.name === name && this.satisfies(new Edge({
      from: new Node({ path: this.root.realpath }),
      type: 'prod',
      name,
      spec,
    }))
  }

  matches (node) {
    // if the nodes are literally the same object, obviously a match.
    if (node === this) {
      return true
    }

    // if the names don't match, they're different things, even if
    // the package contents are identical.
    if (node.name !== this.name) {
      return false
    }

    // if they're links, they match if the targets match
    if (this.isLink) {
      return node.isLink && this.target.matches(node.target)
    }

    // if they're two project root nodes, they're different if the paths differ
    if (this.isProjectRoot && node.isProjectRoot) {
      return this.path === node.path
    }

    // if the integrity matches, then they're the same.
    if (this.integrity && node.integrity) {
      return this.integrity === node.integrity
    }

    // if no integrity, check resolved
    if (this.resolved && node.resolved) {
      return this.resolved === node.resolved
    }

    // if no resolved, check both package name and version
    // otherwise, conclude that they are different things
    return this.packageName && node.packageName &&
      this.packageName === node.packageName &&
      this.version && node.version &&
      this.version === node.version
  }

  // replace this node with the supplied argument
  // Useful when mutating an ideal tree, so we can avoid having to call
  // the parent/root setters more than necessary.
  replaceWith (node) {
    node.replace(this)
  }

  replace (node) {
    this[_delistFromMeta]()

    // if the name matches, but is not identical, we are intending to clobber
    // something case-insensitively, so merely setting name and path won't
    // have the desired effect.  just set the path so it'll collide in the
    // parent's children map, and leave it at that.
    if (node.parent?.children.get(this.name) === node) {
      this.path = resolve(node.parent.path, 'node_modules', this.name)
    } else {
      this.path = node.path
      this.name = node.name
    }

    if (!this.isLink) {
      this.realpath = this.path
    }
    this[_refreshLocation]()

    // keep children when a node replaces another
    if (!this.isLink) {
      for (const kid of node.children.values()) {
        kid.parent = this
      }
      if (node.isLink && node.target) {
        node.target.root = null
      }
    }

    if (!node.isRoot) {
      this.root = node.root
    }

    treeCheck(this)
  }

  get inShrinkwrap () {
    return this.parent &&
      (this.parent.hasShrinkwrap || this.parent.inShrinkwrap)
  }

  get parent () {
    // setter prevents _parent from being this
    return this[_parent]
  }

  // This setter keeps everything in order when we move a node from
  // one point in a logical tree to another.  Edges get reloaded,
  // metadata updated, etc.  It's also called when we *replace* a node
  // with another by the same name (eg, to update or dedupe).
  // This does a couple of walks out on the node_modules tree, recursing
  // into child nodes.  However, as setting the parent is typically done
  // with nodes that don't have have many children, and (deduped) package
  // trees tend to be broad rather than deep, it's not that bad.
  // The only walk that starts from the parent rather than this node is
  // limited by edge name.
  set parent (parent) {
    // when setting to null, just remove it from the tree entirely
    if (!parent) {
      // but only delete it if we actually had a parent in the first place
      // otherwise it's just setting to null when it's already null
      if (this[_parent]) {
        this.root = null
      }
      return
    }

    if (parent.isLink) {
      parent = parent.target
    }

    // setting a thing to its own parent is not normal, but no-op for safety
    if (this === parent) {
      return
    }

    const oldParent = this[_parent]

    // nothing to do
    if (oldParent === parent) {
      return
    }

    // ok now we know something is actually changing, and parent is not a link
    const newPath = resolve(parent.path, 'node_modules', this.name)
    const pathChange = newPath !== this.path

    // remove from old parent/fsParent
    if (oldParent) {
      oldParent.children.delete(this.name)
      this[_parent] = null
    }
    if (this.fsParent) {
      this.fsParent.fsChildren.delete(this)
      this[_fsParent] = null
    }

    // update this.path/realpath for this and all children/fsChildren
    if (pathChange) {
      this[_changePath](newPath)
    }

    if (parent.overrides) {
      this.overrides = parent.overrides.getNodeRule(this)
    }

    // clobbers anything at that path, resets all appropriate references
    this.root = parent.root
  }

  // Call this before changing path or updating the _root reference.
  // Removes the node from its root the metadata and inventory.
  [_delistFromMeta] () {
    const root = this.root
    if (!root.realpath || !this.path) {
      return
    }
    root.inventory.delete(this)
    root.tops.delete(this)
    if (root.meta) {
      root.meta.delete(this.path)
    }
    /* istanbul ignore next - should be impossible */
    debug(() => {
      if ([...root.inventory.values()].includes(this)) {
        throw new Error('failed to delist')
      }
    })
  }

  // update this.path/realpath and the paths of all children/fsChildren
  [_changePath] (newPath) {
    // have to de-list before changing paths
    this[_delistFromMeta]()
    const oldPath = this.path
    this.path = newPath
    const namePattern = /(?:^|\/|\\)node_modules[\\/](@[^/\\]+[\\/][^\\/]+|[^\\/]+)$/
    const nameChange = newPath.match(namePattern)
    if (nameChange && this.name !== nameChange[1]) {
      this.name = nameChange[1].replace(/\\/g, '/')
    }

    // if we move a link target, update link realpaths
    if (!this.isLink) {
      this.realpath = newPath
      for (const link of this.linksIn) {
        link[_delistFromMeta]()
        link.realpath = newPath
        link[_refreshLocation]()
      }
    }
    // if we move /x to /y, then a module at /x/a/b becomes /y/a/b
    for (const child of this.fsChildren) {
      child[_changePath](resolve(newPath, relative(oldPath, child.path)))
    }
    for (const [name, child] of this.children.entries()) {
      child[_changePath](resolve(newPath, 'node_modules', name))
    }

    this[_refreshLocation]()
  }

  // Called whenever the root/parent is changed.
  // NB: need to remove from former root's meta/inventory and then update
  // this.path BEFORE calling this method!
  [_refreshLocation] () {
    const root = this.root
    const loc = relpath(root.realpath, this.path)

    this.location = loc

    root.inventory.add(this)
    if (root.meta) {
      root.meta.add(this)
    }
  }

  assertRootOverrides () {
    if (!this.isProjectRoot || !this.overrides) {
      return
    }

    for (const edge of this.edgesOut.values()) {
      // if these differ an override has been applied, those are not allowed
      // for top level dependencies so throw an error
      if (edge.spec !== edge.rawSpec && !edge.spec.startsWith('$')) {
        throw Object.assign(new Error(`Override for ${edge.name}@${edge.rawSpec} conflicts with direct dependency`), { code: 'EOVERRIDE' })
      }
    }
  }

  addEdgeOut (edge) {
    if (this.overrides) {
      edge.overrides = this.overrides.getEdgeRule(edge)
    }

    this.edgesOut.set(edge.name, edge)
  }

  addEdgeIn (edge) {
    if (edge.overrides) {
      this.overrides = edge.overrides
    }

    this.edgesIn.add(edge)

    // try to get metadata from the yarn.lock file
    if (this.root.meta) {
      this.root.meta.addEdge(edge)
    }
  }

  [_reloadNamedEdges] (name, rootLoc = this.location) {
    const edge = this.edgesOut.get(name)
    // if we don't have an edge, do nothing, but keep descending
    const rootLocResolved = edge && edge.to &&
      edge.to.location === `${rootLoc}/node_modules/${edge.name}`
    const sameResolved = edge && this.resolve(name) === edge.to
    const recheck = rootLocResolved || !sameResolved
    if (edge && recheck) {
      edge.reload(true)
    }
    for (const c of this.children.values()) {
      c[_reloadNamedEdges](name, rootLoc)
    }

    for (const c of this.fsChildren) {
      c[_reloadNamedEdges](name, rootLoc)
    }
  }

  get isLink () {
    return false
  }

  get target () {
    return this
  }

  set target (n) {
    debug(() => {
      throw Object.assign(new Error('cannot set target on non-Link Nodes'), {
        path: this.path,
      })
    })
  }

  get depth () {
    if (this.isTop) {
      return 0
    }
    return this.parent.depth + 1
  }

  get isTop () {
    return !this.parent || this.globalTop
  }

  get top () {
    if (this.isTop) {
      return this
    }
    return this.parent.top
  }

  get isFsTop () {
    return !this.fsParent
  }

  get fsTop () {
    if (this.isFsTop) {
      return this
    }
    return this.fsParent.fsTop
  }

  get resolveParent () {
    return this.parent || this.fsParent
  }

  resolve (name) {
    /* istanbul ignore next - should be impossible,
     * but I keep doing this mistake in tests */
    debug(() => {
      if (typeof name !== 'string' || !name) {
        throw new Error('non-string passed to Node.resolve')
      }
    })
    const mine = this.children.get(name)
    if (mine) {
      return mine
    }
    const resolveParent = this.resolveParent
    if (resolveParent) {
      return resolveParent.resolve(name)
    }
    return null
  }

  inNodeModules () {
    const rp = this.realpath
    const name = this.name
    const scoped = name.charAt(0) === '@'
    const d = dirname(rp)
    const nm = scoped ? dirname(d) : d
    const dir = dirname(nm)
    const base = scoped ? `${basename(d)}/${basename(rp)}` : basename(rp)
    return base === name && basename(nm) === 'node_modules' ? dir : false
  }

  // maybe accept both string value or array of strings
  // seems to be what dom API does
  querySelectorAll (query, opts) {
    return querySelectorAll(this, query, opts)
  }

  toJSON () {
    return printableTree(this)
  }

  [util.inspect.custom] () {
    return this.toJSON()
  }
}

module.exports = Node
