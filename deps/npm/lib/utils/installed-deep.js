const { resolve } = require('node:path')
const localeCompare = require('@isaacs/string-locale-compare')('en')

const installedDeep = async (npm) => {
  const Arborist = require('@npmcli/arborist')
  const {
    depth,
    global,
    prefix,
    workspacesEnabled,
  } = npm.flatOptions

  const getValues = (tree) =>
    [...tree.inventory.values()]
      .filter(i => i.location !== '' && !i.isRoot)
      .map(i => {
        return i
      })
      .filter(i => (i.depth - 1) <= depth)
      .sort((a, b) => (a.depth - b.depth) || localeCompare(a.name, b.name))

  const res = new Set()
  const gArb = new Arborist({
    global: true,
    path: resolve(npm.globalDir, '..'),
    workspacesEnabled,
  })
  const gTree = await gArb.loadActual({ global: true })

  for (const node of getValues(gTree)) {
    res.add(global ? node.name : [node.name, '-g'])
  }

  if (!global) {
    const arb = new Arborist({ global: false, path: prefix, workspacesEnabled })
    const tree = await arb.loadActual()
    for (const node of getValues(tree)) {
      res.add(node.name)
    }
  }

  return [...res]
}

module.exports = installedDeep
