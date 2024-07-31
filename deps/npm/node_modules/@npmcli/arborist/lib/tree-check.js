const debug = require('./debug.js')

const checkTree = (tree, checkUnreachable = true) => {
  const log = [['START TREE CHECK', tree.path]]

  // this can only happen in tests where we have a "tree" object
  // that isn't actually a tree.
  if (!tree.root || !tree.root.inventory) {
    return tree
  }

  const { inventory } = tree.root
  const seen = new Set()
  const check = (node, via = tree, viaType = 'self') => {
    log.push([
      'CHECK',
      node && node.location,
      via && via.location,
      viaType,
      'seen=' + seen.has(node),
      'promise=' + !!(node && node.then),
      'root=' + !!(node && node.isRoot),
    ])

    if (!node || seen.has(node) || node.then) {
      return
    }

    seen.add(node)

    if (node.isRoot && node !== tree.root) {
      throw Object.assign(new Error('double root'), {
        node: node.path,
        realpath: node.realpath,
        tree: tree.path,
        root: tree.root.path,
        via: via.path,
        viaType,
        log,
      })
    }

    if (node.root !== tree.root) {
      throw Object.assign(new Error('node from other root in tree'), {
        node: node.path,
        realpath: node.realpath,
        tree: tree.path,
        root: tree.root.path,
        via: via.path,
        viaType,
        otherRoot: node.root && node.root.path,
        log,
      })
    }

    if (!node.isRoot && node.inventory.size !== 0) {
      throw Object.assign(new Error('non-root has non-zero inventory'), {
        node: node.path,
        tree: tree.path,
        root: tree.root.path,
        via: via.path,
        viaType,
        inventory: [...node.inventory.values()].map(node =>
          [node.path, node.location]),
        log,
      })
    }

    if (!node.isRoot && !inventory.has(node) && !node.dummy) {
      throw Object.assign(new Error('not in inventory'), {
        node: node.path,
        tree: tree.path,
        root: tree.root.path,
        via: via.path,
        viaType,
        log,
      })
    }

    const devEdges = [...node.edgesOut.values()].filter(e => e.dev)
    if (!node.isTop && devEdges.length) {
      throw Object.assign(new Error('dev edges on non-top node'), {
        node: node.path,
        tree: tree.path,
        root: tree.root.path,
        via: via.path,
        viaType,
        devEdges: devEdges.map(e => [e.type, e.name, e.spec, e.error]),
        log,
      })
    }

    if (node.path === tree.root.path && node !== tree.root && !tree.root.isLink) {
      throw Object.assign(new Error('node with same path as root'), {
        node: node.path,
        tree: tree.path,
        root: tree.root.path,
        via: via.path,
        viaType,
        log,
      })
    }

    if (!node.isLink && node.path !== node.realpath) {
      throw Object.assign(new Error('non-link with mismatched path/realpath'), {
        node: node.path,
        tree: tree.path,
        realpath: node.realpath,
        root: tree.root.path,
        via: via.path,
        viaType,
        log,
      })
    }

    const { parent, fsParent, target } = node
    check(parent, node, 'parent')
    check(fsParent, node, 'fsParent')
    check(target, node, 'target')
    log.push(['CHILDREN', node.location, ...node.children.keys()])
    for (const kid of node.children.values()) {
      check(kid, node, 'children')
    }
    for (const kid of node.fsChildren) {
      check(kid, node, 'fsChildren')
    }
    for (const link of node.linksIn) {
      check(link, node, 'linksIn')
    }
    for (const top of node.tops) {
      check(top, node, 'tops')
    }
    log.push(['DONE', node.location])
  }
  check(tree)
  if (checkUnreachable) {
    for (const node of inventory.values()) {
      if (!seen.has(node) && node !== tree.root) {
        throw Object.assign(new Error('unreachable in inventory'), {
          node: node.path,
          realpath: node.realpath,
          location: node.location,
          root: tree.root.path,
          tree: tree.path,
          log,
        })
      }
    }
  }
  return tree
}

// should only ever run this check in debug mode
module.exports = tree => tree
debug(() => module.exports = checkTree)
