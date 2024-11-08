const path = require('path')

const getName = require('@npmcli/name-from-folder')
const { minimatch } = require('minimatch')
const pkgJson = require('@npmcli/package-json')
const { glob } = require('glob')

function appendNegatedPatterns (allPatterns) {
  const patterns = []
  const negatedPatterns = []
  for (let pattern of allPatterns) {
    const excl = pattern.match(/^!+/)
    if (excl) {
      pattern = pattern.slice(excl[0].length)
    }

    // strip off any / or ./ from the start of the pattern.  /foo => foo
    pattern = pattern.replace(/^\.?\/+/, '')

    // an odd number of ! means a negated pattern.  !!foo ==> foo
    const negate = excl && excl[0].length % 2 === 1
    if (negate) {
      negatedPatterns.push(pattern)
    } else {
      // remove negated patterns that appeared before this pattern to avoid
      // ignoring paths that were matched afterwards
      // e.g: ['packages/**', '!packages/b/**', 'packages/b/a']
      // in the above list, the last pattern overrides the negated pattern
      // right before it. In effect, the above list would become:
      // ['packages/**', 'packages/b/a']
      // The order matters here which is why we must do it inside the loop
      // as opposed to doing it all together at the end.
      for (let i = 0; i < negatedPatterns.length; ++i) {
        const negatedPattern = negatedPatterns[i]
        if (minimatch(pattern, negatedPattern)) {
          negatedPatterns.splice(i, 1)
        }
      }
      patterns.push(pattern)
    }
  }

  // use the negated patterns to eagerly remove all the patterns that
  // can be removed to avoid unnecessary crawling
  for (const negated of negatedPatterns) {
    for (const pattern of minimatch.match(patterns, negated)) {
      patterns.splice(patterns.indexOf(pattern), 1)
    }
  }
  return { patterns, negatedPatterns }
}

function getPatterns (workspaces) {
  const workspacesDeclaration =
    Array.isArray(workspaces.packages)
      ? workspaces.packages
      : workspaces

  if (!Array.isArray(workspacesDeclaration)) {
    throw getError({
      message: 'workspaces config expects an Array',
      code: 'EWORKSPACESCONFIG',
    })
  }

  return appendNegatedPatterns(workspacesDeclaration)
}

function getPackageName (pkg, pathname) {
  return pkg.name || getName(pathname)
}

// make sure glob pattern only matches folders
function getGlobPattern (pattern) {
  pattern = pattern.replace(/\\/g, '/')
  return pattern.endsWith('/')
    ? pattern
    : `${pattern}/`
}

function getError ({ Type = TypeError, message, code }) {
  return Object.assign(new Type(message), { code })
}

function reverseResultMap (map) {
  return new Map(Array.from(map, item => item.reverse()))
}

async function mapWorkspaces (opts = {}) {
  if (!opts || !opts.pkg) {
    throw getError({
      message: 'mapWorkspaces missing pkg info',
      code: 'EMAPWORKSPACESPKG',
    })
  }
  if (!opts.cwd) {
    opts.cwd = process.cwd()
  }

  const { workspaces = [] } = opts.pkg
  const { patterns, negatedPatterns } = getPatterns(workspaces)
  const results = new Map()

  if (!patterns.length && !negatedPatterns.length) {
    return results
  }

  const seen = new Map()
  const getGlobOpts = () => ({
    ...opts,
    ignore: [
      ...opts.ignore || [],
      '**/node_modules/**',
      // just ignore the negated patterns to avoid unnecessary crawling
      ...negatedPatterns,
    ],
  })

  let matches = await glob(patterns.map((p) => getGlobPattern(p)), getGlobOpts())
  // preserves glob@8 behavior
  matches = matches.sort((a, b) => a.localeCompare(b, 'en'))

  // we must preserve the order of results according to the given list of
  // workspace patterns
  const orderedMatches = []
  for (const pattern of patterns) {
    orderedMatches.push(...matches.filter((m) => {
      return minimatch(m, pattern, { partial: true, windowsPathsNoEscape: true })
    }))
  }

  for (const match of orderedMatches) {
    let pkg
    try {
      pkg = await pkgJson.normalize(path.join(opts.cwd, match))
    } catch (err) {
      if (err.code === 'ENOENT') {
        continue
      } else {
        throw err
      }
    }

    const name = getPackageName(pkg.content, pkg.path)

    let seenPackagePathnames = seen.get(name)
    if (!seenPackagePathnames) {
      seenPackagePathnames = new Set()
      seen.set(name, seenPackagePathnames)
    }
    seenPackagePathnames.add(pkg.path)
  }

  const errorMessageArray = ['must not have multiple workspaces with the same name']
  for (const [packageName, seenPackagePathnames] of seen) {
    if (seenPackagePathnames.size > 1) {
      addDuplicateErrorMessages(errorMessageArray, packageName, seenPackagePathnames)
    } else {
      results.set(packageName, seenPackagePathnames.values().next().value)
    }
  }

  if (errorMessageArray.length > 1) {
    throw getError({
      Type: Error,
      message: errorMessageArray.join('\n'),
      code: 'EDUPLICATEWORKSPACE',
    })
  }

  return results
}

function addDuplicateErrorMessages (messageArray, packageName, packagePathnames) {
  messageArray.push(
    `package '${packageName}' has conflicts in the following paths:`
  )

  for (const packagePathname of packagePathnames) {
    messageArray.push(
      '    ' + packagePathname
    )
  }
}

mapWorkspaces.virtual = function (opts = {}) {
  if (!opts || !opts.lockfile) {
    throw getError({
      message: 'mapWorkspaces.virtual missing lockfile info',
      code: 'EMAPWORKSPACESLOCKFILE',
    })
  }
  if (!opts.cwd) {
    opts.cwd = process.cwd()
  }

  const { packages = {} } = opts.lockfile
  const { workspaces = [] } = packages[''] || {}
  // uses a pathname-keyed map in order to negate the exact items
  const results = new Map()
  const { patterns, negatedPatterns } = getPatterns(workspaces)
  if (!patterns.length && !negatedPatterns.length) {
    return results
  }
  negatedPatterns.push('**/node_modules/**')

  const packageKeys = Object.keys(packages)
  for (const pattern of negatedPatterns) {
    for (const packageKey of minimatch.match(packageKeys, pattern)) {
      packageKeys.splice(packageKeys.indexOf(packageKey), 1)
    }
  }

  for (const pattern of patterns) {
    for (const packageKey of minimatch.match(packageKeys, pattern)) {
      const packagePathname = path.join(opts.cwd, packageKey)
      const name = getPackageName(packages[packageKey], packagePathname)
      results.set(packagePathname, name)
    }
  }

  // Invert pathname-keyed to a proper name-to-pathnames Map
  return reverseResultMap(results)
}

module.exports = mapWorkspaces
