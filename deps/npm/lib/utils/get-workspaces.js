const { resolve, relative } = require('node:path')
const mapWorkspaces = require('@npmcli/map-workspaces')
const { minimatch } = require('minimatch')
const pkgJson = require('@npmcli/package-json')

// minimatch wants forward slashes only for glob patterns
const globify = pattern => pattern.split('\\').join('/')

// Returns an Map of paths to workspaces indexed by workspace name
// { foo => '/path/to/foo' }
const getWorkspaces = async (filters, { path, includeWorkspaceRoot, relativeFrom }) => {
  // TODO we need a better error to be bubbled up here if this call fails
  const { content: pkg } = await pkgJson.normalize(path)
  const workspaces = await mapWorkspaces({ cwd: path, pkg })
  let res = new Map()
  if (includeWorkspaceRoot) {
    res.set(pkg.name, path)
  }

  if (!filters.length) {
    res = new Map([...res, ...workspaces])
  }

  for (const filterArg of filters) {
    for (const [workspaceName, workspacePath] of workspaces.entries()) {
      let relativePath = relative(relativeFrom, workspacePath)
      if (filterArg.startsWith('./')) {
        relativePath = `./${relativePath}`
      }
      const relativeFilter = relative(path, filterArg)
      if (filterArg === workspaceName
        || resolve(relativeFrom, filterArg) === workspacePath
        || minimatch(relativePath, `${globify(relativeFilter)}/*`)
        || minimatch(relativePath, `${globify(filterArg)}/*`)
      ) {
        res.set(workspaceName, workspacePath)
      }
    }
  }

  if (!res.size) {
    let msg = '!'
    if (filters.length) {
      msg = `:\n ${filters.reduce(
        (acc, filterArg) => `${acc} --workspace=${filterArg}`, '')}`
    }

    throw new Error(`No workspaces found${msg}`)
  }

  return res
}

module.exports = getWorkspaces
