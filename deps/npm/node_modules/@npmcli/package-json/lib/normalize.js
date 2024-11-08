const valid = require('semver/functions/valid')
const clean = require('semver/functions/clean')
const fs = require('node:fs/promises')
const path = require('node:path')
const { log } = require('proc-log')

/**
 * @type {import('hosted-git-info')}
 */
let _hostedGitInfo
function lazyHostedGitInfo () {
  if (!_hostedGitInfo) {
    _hostedGitInfo = require('hosted-git-info')
  }
  return _hostedGitInfo
}

/**
 * @type {import('glob').glob}
 */
let _glob
function lazyLoadGlob () {
  if (!_glob) {
    _glob = require('glob').glob
  }
  return _glob
}

// used to be npm-normalize-package-bin
function normalizePackageBin (pkg, changes) {
  if (pkg.bin) {
    if (typeof pkg.bin === 'string' && pkg.name) {
      changes?.push('"bin" was converted to an object')
      pkg.bin = { [pkg.name]: pkg.bin }
    } else if (Array.isArray(pkg.bin)) {
      changes?.push('"bin" was converted to an object')
      pkg.bin = pkg.bin.reduce((acc, k) => {
        acc[path.basename(k)] = k
        return acc
      }, {})
    }
    if (typeof pkg.bin === 'object') {
      for (const binKey in pkg.bin) {
        if (typeof pkg.bin[binKey] !== 'string') {
          delete pkg.bin[binKey]
          changes?.push(`removed invalid "bin[${binKey}]"`)
          continue
        }
        const base = path.basename(secureAndUnixifyPath(binKey))
        if (!base) {
          delete pkg.bin[binKey]
          changes?.push(`removed invalid "bin[${binKey}]"`)
          continue
        }

        const binTarget = secureAndUnixifyPath(pkg.bin[binKey])

        if (!binTarget) {
          delete pkg.bin[binKey]
          changes?.push(`removed invalid "bin[${binKey}]"`)
          continue
        }

        if (base !== binKey) {
          delete pkg.bin[binKey]
          changes?.push(`"bin[${binKey}]" was renamed to "bin[${base}]"`)
        }
        if (binTarget !== pkg.bin[binKey]) {
          changes?.push(`"bin[${base}]" script name was cleaned`)
        }
        pkg.bin[base] = binTarget
      }

      if (Object.keys(pkg.bin).length === 0) {
        changes?.push('empty "bin" was removed')
        delete pkg.bin
      }

      return pkg
    }
  }
  delete pkg.bin
}

function normalizePackageMan (pkg, changes) {
  if (pkg.man) {
    const mans = []
    for (const man of (Array.isArray(pkg.man) ? pkg.man : [pkg.man])) {
      if (typeof man !== 'string') {
        changes?.push(`removed invalid "man [${man}]"`)
      } else {
        mans.push(secureAndUnixifyPath(man))
      }
    }

    if (!mans.length) {
      changes?.push('empty "man" was removed')
    } else {
      pkg.man = mans
      return pkg
    }
  }
  delete pkg.man
}

function isCorrectlyEncodedName (spec) {
  return !spec.match(/[/@\s+%:]/) &&
    spec === encodeURIComponent(spec)
}

function isValidScopedPackageName (spec) {
  if (spec.charAt(0) !== '@') {
    return false
  }

  const rest = spec.slice(1).split('/')
  if (rest.length !== 2) {
    return false
  }

  return rest[0] && rest[1] &&
    rest[0] === encodeURIComponent(rest[0]) &&
    rest[1] === encodeURIComponent(rest[1])
}

function unixifyPath (ref) {
  return ref.replace(/\\|:/g, '/')
}

function secureAndUnixifyPath (ref) {
  const secured = unixifyPath(path.join('.', path.join('/', unixifyPath(ref))))
  return secured.startsWith('./') ? '' : secured
}

// We don't want the `changes` array in here by default because this is a hot
// path for parsing packuments during install.  So the calling method passes it
// in if it wants to track changes.
const normalize = async (pkg, { strict, steps, root, changes, allowLegacyCase }) => {
  if (!pkg.content) {
    throw new Error('Can not normalize without content')
  }
  const data = pkg.content
  const scripts = data.scripts || {}
  const pkgId = `${data.name ?? ''}@${data.version ?? ''}`

  // name and version are load bearing so we have to clean them up first
  if (steps.includes('fixNameField') || steps.includes('normalizeData')) {
    if (!data.name && !strict) {
      changes?.push('Missing "name" field was set to an empty string')
      data.name = ''
    } else {
      if (typeof data.name !== 'string') {
        throw new Error('name field must be a string.')
      }
      if (!strict) {
        const name = data.name.trim()
        if (data.name !== name) {
          changes?.push(`Whitespace was trimmed from "name"`)
          data.name = name
        }
      }

      if (data.name.startsWith('.') ||
        !(isValidScopedPackageName(data.name) || isCorrectlyEncodedName(data.name)) ||
        (strict && (!allowLegacyCase) && data.name !== data.name.toLowerCase()) ||
        data.name.toLowerCase() === 'node_modules' ||
        data.name.toLowerCase() === 'favicon.ico') {
        throw new Error('Invalid name: ' + JSON.stringify(data.name))
      }
    }
  }

  if (steps.includes('fixVersionField') || steps.includes('normalizeData')) {
    // allow "loose" semver 1.0 versions in non-strict mode
    // enforce strict semver 2.0 compliance in strict mode
    const loose = !strict
    if (!data.version) {
      data.version = ''
    } else {
      if (!valid(data.version, loose)) {
        throw new Error(`Invalid version: "${data.version}"`)
      }
      const version = clean(data.version, loose)
      if (version !== data.version) {
        changes?.push(`"version" was cleaned and set to "${version}"`)
        data.version = version
      }
    }
  }
  // remove attributes that start with "_"
  if (steps.includes('_attributes')) {
    for (const key in data) {
      if (key.startsWith('_')) {
        changes?.push(`"${key}" was removed`)
        delete pkg.content[key]
      }
    }
  }

  // build the "_id" attribute
  if (steps.includes('_id')) {
    if (data.name && data.version) {
      changes?.push(`"_id" was set to ${pkgId}`)
      data._id = pkgId
    }
  }

  // fix bundledDependencies typo
  // normalize bundleDependencies
  if (steps.includes('bundledDependencies')) {
    if (data.bundleDependencies === undefined && data.bundledDependencies !== undefined) {
      data.bundleDependencies = data.bundledDependencies
    }
    changes?.push(`Deleted incorrect "bundledDependencies"`)
    delete data.bundledDependencies
  }
  // expand "bundleDependencies: true or translate from object"
  if (steps.includes('bundleDependencies')) {
    const bd = data.bundleDependencies
    if (bd === false && !steps.includes('bundleDependenciesDeleteFalse')) {
      changes?.push(`"bundleDependencies" was changed from "false" to "[]"`)
      data.bundleDependencies = []
    } else if (bd === true) {
      changes?.push(`"bundleDependencies" was auto-populated from "dependencies"`)
      data.bundleDependencies = Object.keys(data.dependencies || {})
    } else if (bd && typeof bd === 'object') {
      if (!Array.isArray(bd)) {
        changes?.push(`"bundleDependencies" was changed from an object to an array`)
        data.bundleDependencies = Object.keys(bd)
      }
    } else if ('bundleDependencies' in data) {
      changes?.push(`"bundleDependencies" was removed`)
      delete data.bundleDependencies
    }
  }

  // it was once common practice to list deps both in optionalDependencies and
  // in dependencies, to support npm versions that did not know about
  // optionalDependencies.  This is no longer a relevant need, so duplicating
  // the deps in two places is unnecessary and excessive.
  if (steps.includes('optionalDedupe')) {
    if (data.dependencies &&
      data.optionalDependencies && typeof data.optionalDependencies === 'object') {
      for (const name in data.optionalDependencies) {
        changes?.push(`optionalDependencies."${name}" was removed`)
        delete data.dependencies[name]
      }
      if (!Object.keys(data.dependencies).length) {
        changes?.push(`Empty "optionalDependencies" was removed`)
        delete data.dependencies
      }
    }
  }

  // add "install" attribute if any "*.gyp" files exist
  if (steps.includes('gypfile')) {
    if (!scripts.install && !scripts.preinstall && data.gypfile !== false) {
      const files = await lazyLoadGlob()('*.gyp', { cwd: pkg.path })
      if (files.length) {
        scripts.install = 'node-gyp rebuild'
        data.scripts = scripts
        data.gypfile = true
        changes?.push(`"scripts.install" was set to "node-gyp rebuild"`)
        changes?.push(`"gypfile" was set to "true"`)
      }
    }
  }

  // add "start" attribute if "server.js" exists
  if (steps.includes('serverjs') && !scripts.start) {
    try {
      await fs.access(path.join(pkg.path, 'server.js'))
      scripts.start = 'node server.js'
      data.scripts = scripts
      changes?.push('"scripts.start" was set to "node server.js"')
    } catch {
      // do nothing
    }
  }

  // strip "node_modules/.bin" from scripts entries
  // remove invalid scripts entries (non-strings)
  if ((steps.includes('scripts') || steps.includes('scriptpath')) && data.scripts !== undefined) {
    const spre = /^(\.[/\\])?node_modules[/\\].bin[\\/]/
    if (typeof data.scripts === 'object') {
      for (const name in data.scripts) {
        if (typeof data.scripts[name] !== 'string') {
          delete data.scripts[name]
          changes?.push(`Invalid scripts."${name}" was removed`)
        } else if (steps.includes('scriptpath') && spre.test(data.scripts[name])) {
          data.scripts[name] = data.scripts[name].replace(spre, '')
          changes?.push(`scripts entry "${name}" was fixed to remove node_modules/.bin reference`)
        }
      }
    } else {
      changes?.push(`Removed invalid "scripts"`)
      delete data.scripts
    }
  }

  if (steps.includes('funding')) {
    if (data.funding && typeof data.funding === 'string') {
      data.funding = { url: data.funding }
      changes?.push(`"funding" was changed to an object with a url attribute`)
    }
  }

  // populate "authors" attribute
  if (steps.includes('authors') && !data.contributors) {
    try {
      const authorData = await fs.readFile(path.join(pkg.path, 'AUTHORS'), 'utf8')
      const authors = authorData.split(/\r?\n/g)
        .map(line => line.replace(/^\s*#.*$/, '').trim())
        .filter(line => line)
      data.contributors = authors
      changes?.push('"contributors" was auto-populated with the contents of the "AUTHORS" file')
    } catch {
      // do nothing
    }
  }

  // populate "readme" attribute
  if (steps.includes('readme') && !data.readme) {
    const mdre = /\.m?a?r?k?d?o?w?n?$/i
    const files = await lazyLoadGlob()('{README,README.*}', {
      cwd: pkg.path,
      nocase: true,
      mark: true,
    })
    let readmeFile
    for (const file of files) {
      // don't accept directories.
      if (!file.endsWith(path.sep)) {
        if (file.match(mdre)) {
          readmeFile = file
          break
        }
        if (file.endsWith('README')) {
          readmeFile = file
        }
      }
    }
    if (readmeFile) {
      const readmeData = await fs.readFile(path.join(pkg.path, readmeFile), 'utf8')
      data.readme = readmeData
      data.readmeFilename = readmeFile
      changes?.push(`"readme" was set to the contents of ${readmeFile}`)
      changes?.push(`"readmeFilename" was set to ${readmeFile}`)
    }
    if (!data.readme) {
      // this.warn('missingReadme')
      data.readme = 'ERROR: No README data found!'
    }
  }

  // expand directories.man
  if (steps.includes('mans')) {
    if (data.directories?.man && !data.man) {
      const manDir = secureAndUnixifyPath(data.directories.man)
      const cwd = path.resolve(pkg.path, manDir)
      const files = await lazyLoadGlob()('**/*.[0-9]', { cwd })
      data.man = files.map(man =>
        path.relative(pkg.path, path.join(cwd, man)).split(path.sep).join('/')
      )
    }
    normalizePackageMan(data, changes)
  }

  if (steps.includes('bin') || steps.includes('binDir') || steps.includes('binRefs')) {
    normalizePackageBin(data, changes)
  }

  // expand "directories.bin"
  if (steps.includes('binDir') && data.directories?.bin && !data.bin) {
    const binsDir = path.resolve(pkg.path, secureAndUnixifyPath(data.directories.bin))
    const bins = await lazyLoadGlob()('**', { cwd: binsDir })
    data.bin = bins.reduce((acc, binFile) => {
      if (binFile && !binFile.startsWith('.')) {
        const binName = path.basename(binFile)
        acc[binName] = path.join(data.directories.bin, binFile)
      }
      return acc
    }, {})
    // *sigh*
    normalizePackageBin(data, changes)
  }

  // populate "gitHead" attribute
  if (steps.includes('gitHead') && !data.gitHead) {
    const git = require('@npmcli/git')
    const gitRoot = await git.find({ cwd: pkg.path, root })
    let head
    if (gitRoot) {
      try {
        head = await fs.readFile(path.resolve(gitRoot, '.git/HEAD'), 'utf8')
      } catch (err) {
      // do nothing
      }
    }
    let headData
    if (head) {
      if (head.startsWith('ref: ')) {
        const headRef = head.replace(/^ref: /, '').trim()
        const headFile = path.resolve(gitRoot, '.git', headRef)
        try {
          headData = await fs.readFile(headFile, 'utf8')
          headData = headData.replace(/^ref: /, '').trim()
        } catch (err) {
          // do nothing
        }
        if (!headData) {
          const packFile = path.resolve(gitRoot, '.git/packed-refs')
          try {
            let refs = await fs.readFile(packFile, 'utf8')
            if (refs) {
              refs = refs.split('\n')
              for (let i = 0; i < refs.length; i++) {
                const match = refs[i].match(/^([0-9a-f]{40}) (.+)$/)
                if (match && match[2].trim() === headRef) {
                  headData = match[1]
                  break
                }
              }
            }
          } catch {
            // do nothing
          }
        }
      } else {
        headData = head.trim()
      }
    }
    if (headData) {
      data.gitHead = headData
    }
  }

  // populate "types" attribute
  if (steps.includes('fillTypes')) {
    const index = data.main || 'index.js'

    if (typeof index !== 'string') {
      throw new TypeError('The "main" attribute must be of type string.')
    }

    // TODO exports is much more complicated than this in verbose format
    // We need to support for instance

    // "exports": {
    //   ".": [
    //     {
    //       "default": "./lib/npm.js"
    //     },
    //     "./lib/npm.js"
    //   ],
    //   "./package.json": "./package.json"
    // },
    // as well as conditional exports

    // if (data.exports && typeof data.exports === 'string') {
    //   index = data.exports
    // }

    // if (data.exports && data.exports['.']) {
    //   index = data.exports['.']
    //   if (typeof index !== 'string') {
    //   }
    // }
    const extless = path.join(path.dirname(index), path.basename(index, path.extname(index)))
    const dts = `./${extless}.d.ts`
    const hasDTSFields = 'types' in data || 'typings' in data
    if (!hasDTSFields) {
      try {
        await fs.access(path.join(pkg.path, dts))
        data.types = dts.split(path.sep).join('/')
      } catch {
        // do nothing
      }
    }
  }

  // "normalizeData" from "read-package-json", which was just a call through to
  // "normalize-package-data".  We only call the "fixer" functions because
  // outside of that it was also clobbering _id (which we already conditionally
  // do) and also adding the gypfile script (which we also already
  // conditionally do)

  // Some steps are isolated so we can do a limited subset of these in `fix`
  if (steps.includes('fixRepositoryField') || steps.includes('normalizeData')) {
    if (data.repositories) {
      /* eslint-disable-next-line max-len */
      changes?.push(`"repository" was set to the first entry in "repositories" (${data.repository})`)
      data.repository = data.repositories[0]
    }
    if (data.repository) {
      if (typeof data.repository === 'string') {
        changes?.push('"repository" was changed from a string to an object')
        data.repository = {
          type: 'git',
          url: data.repository,
        }
      }
      if (data.repository.url) {
        const hosted = lazyHostedGitInfo().fromUrl(data.repository.url)
        let r
        if (hosted) {
          if (hosted.getDefaultRepresentation() === 'shortcut') {
            r = hosted.https()
          } else {
            r = hosted.toString()
          }
          if (r !== data.repository.url) {
            changes?.push(`"repository.url" was normalized to "${r}"`)
            data.repository.url = r
          }
        }
      }
    }
  }

  if (steps.includes('fixDependencies') || steps.includes('normalizeData')) {
    // peerDependencies?
    // devDependencies is meaningless here, it's ignored on an installed package
    for (const type of ['dependencies', 'devDependencies', 'optionalDependencies']) {
      if (data[type]) {
        let secondWarning = true
        if (typeof data[type] === 'string') {
          changes?.push(`"${type}" was converted from a string into an object`)
          data[type] = data[type].trim().split(/[\n\r\s\t ,]+/)
          secondWarning = false
        }
        if (Array.isArray(data[type])) {
          if (secondWarning) {
            changes?.push(`"${type}" was converted from an array into an object`)
          }
          const o = {}
          for (const d of data[type]) {
            if (typeof d === 'string') {
              const dep = d.trim().split(/(:?[@\s><=])/)
              const dn = dep.shift()
              const dv = dep.join('').replace(/^@/, '').trim()
              o[dn] = dv
            }
          }
          data[type] = o
        }
      }
    }
    // normalize-package-data used to put optional dependencies BACK into
    // dependencies here, we no longer do this

    for (const deps of ['dependencies', 'devDependencies']) {
      if (deps in data) {
        if (!data[deps] || typeof data[deps] !== 'object') {
          changes?.push(`Removed invalid "${deps}"`)
          delete data[deps]
        } else {
          for (const d in data[deps]) {
            const r = data[deps][d]
            if (typeof r !== 'string') {
              changes?.push(`Removed invalid "${deps}.${d}"`)
              delete data[deps][d]
            }
            const hosted = lazyHostedGitInfo().fromUrl(data[deps][d])?.toString()
            if (hosted && hosted !== data[deps][d]) {
              changes?.push(`Normalized git reference to "${deps}.${d}"`)
              data[deps][d] = hosted.toString()
            }
          }
        }
      }
    }
  }

  if (steps.includes('normalizeData')) {
    const legacyFixer = require('normalize-package-data/lib/fixer.js')
    const legacyMakeWarning = require('normalize-package-data/lib/make_warning.js')
    legacyFixer.warn = function () {
      changes?.push(legacyMakeWarning.apply(null, arguments))
    }

    const legacySteps = [
      'fixDescriptionField',
      'fixModulesField',
      'fixFilesField',
      'fixManField',
      'fixBugsField',
      'fixKeywordsField',
      'fixBundleDependenciesField',
      'fixHomepageField',
      'fixReadmeField',
      'fixLicenseField',
      'fixPeople',
      'fixTypos',
    ]
    for (const legacyStep of legacySteps) {
      legacyFixer[legacyStep](data)
    }
  }

  // Warn if the bin references don't point to anything.  This might be better
  // in normalize-package-data if it had access to the file path.
  if (steps.includes('binRefs') && data.bin instanceof Object) {
    for (const key in data.bin) {
      try {
        await fs.access(path.resolve(pkg.path, data.bin[key]))
      } catch {
        log.warn('package-json', pkgId, `No bin file found at ${data.bin[key]}`)
        // XXX: should a future breaking change delete bin entries that cannot be accessed?
      }
    }
  }
}

module.exports = normalize
