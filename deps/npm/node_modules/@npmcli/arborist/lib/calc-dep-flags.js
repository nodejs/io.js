const { depth } = require('treeverse')

const calcDepFlags = (tree, resetRoot = true) => {
  if (resetRoot) {
    tree.dev = false
    tree.optional = false
    tree.devOptional = false
    tree.peer = false
  }
  const ret = depth({
    tree,
    visit: node => calcDepFlagsStep(node),
    filter: node => node,
    getChildren: (node, tree) =>
      [...tree.edgesOut.values()].map(edge => edge.to),
  })
  return ret
}

const calcDepFlagsStep = (node) => {
  // This rewalk is necessary to handle cases where devDep and optional
  // or normal dependency graphs overlap deep in the dep graph.
  // Since we're only walking through deps that are not already flagged
  // as non-dev/non-optional, it's typically a very shallow traversal
  node.extraneous = false
  resetParents(node, 'extraneous')
  resetParents(node, 'dev')
  resetParents(node, 'peer')
  resetParents(node, 'devOptional')
  resetParents(node, 'optional')

  // for links, map their hierarchy appropriately
  if (node.isLink) {
    // node.target can be null, we check to ensure it's not null before proceeding
    if (node.target == null) {
      return node
    }
    node.target.dev = node.dev
    node.target.optional = node.optional
    node.target.devOptional = node.devOptional
    node.target.peer = node.peer
    return calcDepFlagsStep(node.target)
  }

  node.edgesOut.forEach(({ peer, optional, dev, to }) => {
    // if the dep is missing, then its flags are already maximally unset
    if (!to) {
      return
    }

    // everything with any kind of edge into it is not extraneous
    to.extraneous = false

    // devOptional is the *overlap* of the dev and optional tree.
    // however, for convenience and to save an extra rewalk, we leave
    // it set when we are in *either* tree, and then omit it from the
    // package-lock if either dev or optional are set.
    const unsetDevOpt = !node.devOptional && !node.dev && !node.optional && !dev && !optional

    // if we are not in the devOpt tree, then we're also not in
    // either the dev or opt trees
    const unsetDev = unsetDevOpt || !node.dev && !dev
    const unsetOpt = unsetDevOpt || !node.optional && !optional
    const unsetPeer = !node.peer && !peer

    if (unsetPeer) {
      unsetFlag(to, 'peer')
    }

    if (unsetDevOpt) {
      unsetFlag(to, 'devOptional')
    }

    if (unsetDev) {
      unsetFlag(to, 'dev')
    }

    if (unsetOpt) {
      unsetFlag(to, 'optional')
    }
  })

  return node
}

const resetParents = (node, flag) => {
  if (node[flag]) {
    return
  }

  for (let p = node; p && (p === node || p[flag]); p = p.resolveParent) {
    p[flag] = false
  }
}

// typically a short walk, since it only traverses deps that have the flag set.
const unsetFlag = (node, flag) => {
  if (node[flag]) {
    node[flag] = false
    depth({
      tree: node,
      visit: node => {
        node.extraneous = node[flag] = false
        if (node.isLink && node.target) {
          node.target.extraneous = node.target[flag] = false
        }
      },
      getChildren: node => {
        const children = []
        const targetNode = node.isLink && node.target ? node.target : node
        for (const edge of targetNode.edgesOut.values()) {
          if (
            edge.to &&
            edge.to[flag] &&
            ((flag !== 'peer' && edge.type === 'peer') || edge.type === 'prod')
          ) {
            children.push(edge.to)
          }
        }
        return children
      },
    })
  }
}

module.exports = calcDepFlags
