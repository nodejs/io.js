const relpath = require('./relpath.js')
const Node = require('./node.js')
const _loadDeps = Symbol.for('Arborist.Node._loadDeps')
const _target = Symbol.for('_target')
const { dirname } = require('node:path')
// defined by Node class
const _delistFromMeta = Symbol.for('_delistFromMeta')
const _refreshLocation = Symbol.for('_refreshLocation')
class Link extends Node {
  constructor (options) {
    const { root, realpath, target, parent, fsParent, isStoreLink } = options

    if (!realpath && !(target && target.path)) {
      throw new TypeError('must provide realpath for Link node')
    }

    super({
      ...options,
      realpath: realpath || target.path,
      root: root || (parent ? parent.root
      : fsParent ? fsParent.root
      : target ? target.root
      : null),
    })

    this.isStoreLink = isStoreLink || false

    if (target) {
      this.target = target
    } else if (this.realpath === this.root.path) {
      this.target = this.root
    } else {
      this.target = new Node({
        ...options,
        path: realpath,
        parent: null,
        fsParent: null,
        root: this.root,
      })
    }
  }

  get version () {
    return this.target ? this.target.version : this.package.version || ''
  }

  get target () {
    return this[_target]
  }

  set target (target) {
    const current = this[_target]
    if (target === current) {
      return
    }

    if (!target) {
      if (current && current.linksIn) {
        current.linksIn.delete(this)
      }
      if (this.path) {
        this[_delistFromMeta]()
        this[_target] = null
        this.package = {}
        this[_refreshLocation]()
      } else {
        this[_target] = null
      }
      return
    }

    if (!this.path) {
      // temp node pending assignment to a tree
      // we know it's not in the inventory yet, because no path.
      if (target.path) {
        this.realpath = target.path
      } else {
        target.path = target.realpath = this.realpath
      }
      target.root = this.root
      this[_target] = target
      target.linksIn.add(this)
      this.package = target.package
      return
    }

    // have to refresh metadata, because either realpath or package
    // is very likely changing.
    this[_delistFromMeta]()
    this.package = target.package
    this.realpath = target.path
    this[_refreshLocation]()

    target.root = this.root
  }

  // a link always resolves to the relative path to its target
  get resolved () {
    // the path/realpath guard is there for the benefit of setting
    // these things in the "wrong" order
    return this.path && this.realpath
      ? `file:${relpath(dirname(this.path), this.realpath).replace(/#/g, '%23')}`
      : null
  }

  set resolved (r) {}

  // deps are resolved on the target, not the Link
  // so this is a no-op
  [_loadDeps] () {}

  // links can't have children, only their targets can
  // fix it to an empty list so that we can still call
  // things that iterate over them, just as a no-op
  get children () {
    return new Map()
  }

  set children (c) {}

  get isLink () {
    return true
  }
}

module.exports = Link
