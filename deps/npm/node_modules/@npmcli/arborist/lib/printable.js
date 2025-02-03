// helper function to output a clearer visualization
// of the current node and its descendents
const localeCompare = require('@isaacs/string-locale-compare')('en')
const util = require('node:util')
const relpath = require('./relpath.js')

class ArboristNode {
  constructor (tree, path) {
    this.name = tree.name
    if (tree.packageName && tree.packageName !== this.name) {
      this.packageName = tree.packageName
    }
    if (tree.version) {
      this.version = tree.version
    }
    this.location = tree.location
    this.path = tree.path
    if (tree.realpath !== this.path) {
      this.realpath = tree.realpath
    }
    if (tree.resolved !== null) {
      this.resolved = tree.resolved
    }
    if (tree.extraneous) {
      this.extraneous = true
    }
    if (tree.dev) {
      this.dev = true
    }
    if (tree.optional) {
      this.optional = true
    }
    if (tree.devOptional && !tree.dev && !tree.optional) {
      this.devOptional = true
    }
    if (tree.peer) {
      this.peer = true
    }
    if (tree.inBundle) {
      this.bundled = true
    }
    if (tree.inDepBundle) {
      this.bundler = tree.getBundler().location
    }
    if (tree.isProjectRoot) {
      this.isProjectRoot = true
    }
    if (tree.isWorkspace) {
      this.isWorkspace = true
    }
    const bd = tree.package && tree.package.bundleDependencies
    if (bd && bd.length) {
      this.bundleDependencies = bd
    }
    if (tree.inShrinkwrap) {
      this.inShrinkwrap = true
    } else if (tree.hasShrinkwrap) {
      this.hasShrinkwrap = true
    }
    if (tree.error) {
      this.error = treeError(tree.error)
    }
    if (tree.errors && tree.errors.length) {
      this.errors = tree.errors.map(treeError)
    }

    if (tree.overrides) {
      this.overrides = new Map([...tree.overrides.ruleset.values()]
        .map((override) => [override.key, override.value]))
    }

    // edgesOut sorted by name
    if (tree.edgesOut.size) {
      this.edgesOut = new Map([...tree.edgesOut.entries()]
        .sort(([a], [b]) => localeCompare(a, b))
        .map(([name, edge]) => [name, new EdgeOut(edge)]))
    }

    // edgesIn sorted by location
    if (tree.edgesIn.size) {
      this.edgesIn = new Set([...tree.edgesIn]
        .sort((a, b) => localeCompare(a.from.location, b.from.location))
        .map(edge => new EdgeIn(edge)))
    }

    if (tree.workspaces && tree.workspaces.size) {
      this.workspaces = new Map([...tree.workspaces.entries()]
        .map(([name, path]) => [name, relpath(tree.root.realpath, path)]))
    }

    // fsChildren sorted by path
    if (tree.fsChildren.size) {
      this.fsChildren = new Set([...tree.fsChildren]
        .sort(({ path: a }, { path: b }) => localeCompare(a, b))
        .map(tree => printableTree(tree, path)))
    }

    // children sorted by name
    if (tree.children.size) {
      this.children = new Map([...tree.children.entries()]
        .sort(([a], [b]) => localeCompare(a, b))
        .map(([name, tree]) => [name, printableTree(tree, path)]))
    }
  }
}

class ArboristVirtualNode extends ArboristNode {
  constructor (tree, path) {
    super(tree, path)
    this.sourceReference = printableTree(tree.sourceReference, path)
  }
}

class ArboristLink extends ArboristNode {
  constructor (tree, path) {
    super(tree, path)
    this.target = printableTree(tree.target, path)
  }
}

const treeError = ({ code, path }) => ({
  code,
  ...(path ? { path } : {}),
})

// print out edges without dumping the full node all over again
// this base class will toJSON as a plain old object, but the
// util.inspect() output will be a bit cleaner
class Edge {
  constructor (edge) {
    this.type = edge.type
    this.name = edge.name
    this.spec = edge.rawSpec || '*'
    if (edge.rawSpec !== edge.spec) {
      this.override = edge.spec
    }
    if (edge.error) {
      this.error = edge.error
    }
    if (edge.peerConflicted) {
      this.peerConflicted = edge.peerConflicted
    }
  }
}

// don't care about 'from' for edges out
class EdgeOut extends Edge {
  constructor (edge) {
    super(edge)
    this.to = edge.to && edge.to.location
  }

  [util.inspect.custom] () {
    return `{ ${this.type} ${this.name}@${this.spec}${
      this.override ? ` overridden:${this.override}` : ''
    }${
      this.to ? ' -> ' + this.to : ''
    }${
      this.error ? ' ' + this.error : ''
    }${
      this.peerConflicted ? ' peerConflicted' : ''
    } }`
  }
}

// don't care about 'to' for edges in
class EdgeIn extends Edge {
  constructor (edge) {
    super(edge)
    this.from = edge.from && edge.from.location
  }

  [util.inspect.custom] () {
    return `{ ${this.from || '""'} ${this.type} ${this.name}@${this.spec}${
      this.error ? ' ' + this.error : ''
    }${
      this.peerConflicted ? ' peerConflicted' : ''
    } }`
  }
}

const printableTree = (tree, path = []) => {
  if (!tree) {
    return tree
  }

  const Cls = tree.isLink ? ArboristLink
    : tree.sourceReference ? ArboristVirtualNode
    : ArboristNode
  if (path.includes(tree)) {
    const obj = Object.create(Cls.prototype)
    return Object.assign(obj, { location: tree.location })
  }
  path.push(tree)
  return new Cls(tree, path)
}

module.exports = printableTree
