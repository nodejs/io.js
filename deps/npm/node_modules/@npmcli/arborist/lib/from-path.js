// file dependencies need their dependencies resolved based on the location
// where the tarball was found, not the location where they end up getting
// installed.  directory (ie, symlink) deps also need to be resolved based on
// their targets, but that's what realpath is

const { dirname } = require('node:path')
const npa = require('npm-package-arg')

const fromPath = (node, edge) => {
  if (edge && edge.overrides && edge.overrides.name === edge.name && edge.overrides.value) {
    // fromPath could be called with a node that has a virtual root, if that
    // happens we want to make sure we get the real root node when overrides
    // are in use. this is to allow things like overriding a dependency with a
    // tarball file that's a relative path from the project root
    if (node.sourceReference) {
      return node.sourceReference.root.realpath
    }
    return node.root.realpath
  }

  if (node.resolved) {
    const spec = npa(node.resolved)
    if (spec?.type === 'file') {
      return dirname(spec.fetchSpec)
    }
  }
  return node.realpath
}

module.exports = fromPath
