/* globals config, dirname, package, basename, yes, prompt */

const fs = require('fs/promises')
const path = require('path')
const validateLicense = require('validate-npm-package-license')
const validateName = require('validate-npm-package-name')
const npa = require('npm-package-arg')
const semver = require('semver')

// more popular packages should go here, maybe?
const isTestPkg = (p) => !!p.match(/^(expresso|mocha|tap|coffee-script|coco|streamline)$/)

const invalid = (msg) => Object.assign(new Error(msg), { notValid: true })

const readDeps = (test, excluded) => async () => {
  const dirs = await fs.readdir('node_modules').catch(() => null)

  if (!dirs) {
    return
  }

  const deps = {}
  for (const dir of dirs) {
    if (dir.match(/^\./) || test !== isTestPkg(dir) || excluded[dir]) {
      continue
    }

    const dp = path.join(dirname, 'node_modules', dir, 'package.json')
    const p = await fs.readFile(dp, 'utf8').then((d) => JSON.parse(d)).catch(() => null)

    if (!p || !p.version || p?._requiredBy?.some((r) => r === '#USER')) {
      continue
    }

    deps[dir] = config.get('save-exact') ? p.version : config.get('save-prefix') + p.version
  }

  return deps
}

const getConfig = (key) => {
  // dots take precedence over dashes
  const def = config?.defaults?.[`init.${key}`]
  const val = config.get(`init.${key}`)
  return (val !== def && val) ? val : config.get(`init-${key.replace(/\./g, '-')}`)
}

const getName = () => {
  const rawName = package.name || basename
  let name = rawName
    .replace(/^node-|[.-]js$/g, '')
    .replace(/\s+/g, ' ')
    .replace(/ /g, '-')
    .toLowerCase()

  let spec
  try {
    spec = npa(name)
  } catch {
    spec = {}
  }

  let scope = config.get('scope')

  if (scope) {
    if (scope.charAt(0) !== '@') {
      scope = '@' + scope
    }
    if (spec.scope) {
      name = scope + '/' + spec.name.split('/')[1]
    } else {
      name = scope + '/' + name
    }
  }

  return name
}

const name = getName()
exports.name = yes ? name : prompt('package name', name, (data) => {
  const its = validateName(data)
  if (its.validForNewPackages) {
    return data
  }
  const errors = (its.errors || []).concat(its.warnings || [])
  return invalid(`Sorry, ${errors.join(' and ')}.`)
})

const version = package.version || getConfig('version') || '1.0.0'
exports.version = yes ? version : prompt('version', version, (v) => {
  if (semver.valid(v)) {
    return v
  }
  return invalid(`Invalid version: "${v}"`)
})

if (!package.description) {
  exports.description = yes ? '' : prompt('description')
}

if (!package.main) {
  exports.main = async () => {
    const files = await fs.readdir(dirname)
      .then(list => list.filter((f) => f.match(/\.js$/)))
      .catch(() => [])

    let index
    if (files.includes('index.js')) {
      index = 'index.js'
    } else if (files.includes('main.js')) {
      index = 'main.js'
    } else if (files.includes(basename + '.js')) {
      index = basename + '.js'
    } else {
      index = files[0] || 'index.js'
    }

    return yes ? index : prompt('entry point', index)
  }
}

if (!package.bin) {
  exports.bin = async () => {
    try {
      const d = await fs.readdir(path.resolve(dirname, 'bin'))
      // just take the first js file we find there, or nada
      let r = d.find(f => f.match(/\.js$/))
      if (r) {
        r = `bin/${r}`
      }
      return r
    } catch {
      // no bins
    }
  }
}

exports.directories = async () => {
  const dirs = await fs.readdir(dirname)

  const res = dirs.reduce((acc, d) => {
    if (/^examples?$/.test(d)) {
      acc.example = d
    } else if (/^tests?$/.test(d)) {
      acc.test = d
    } else if (/^docs?$/.test(d)) {
      acc.doc = d
    } else if (d === 'man') {
      acc.man = d
    } else if (d === 'lib') {
      acc.lib = d
    }

    return acc
  }, {})

  return Object.keys(res).length === 0 ? undefined : res
}

if (!package.dependencies) {
  exports.dependencies = readDeps(false, package.devDependencies || {})
}

if (!package.devDependencies) {
  exports.devDependencies = readDeps(true, package.dependencies || {})
}

// MUST have a test script!
if (!package.scripts) {
  const scripts = package.scripts || {}
  const notest = 'echo "Error: no test specified" && exit 1'
  exports.scripts = async () => {
    const d = await fs.readdir(path.join(dirname, 'node_modules')).catch(() => [])

    // check to see what framework is in use, if any
    let command
    if (!scripts.test || scripts.test === notest) {
      const commands = {
        tap: 'tap test/*.js',
        expresso: 'expresso test',
        mocha: 'mocha',
      }
      for (const [k, v] of Object.entries(commands)) {
        if (d.includes(k)) {
          command = v
        }
      }
    }

    const promptArgs = ['test command', (t) => t || notest]
    if (command) {
      promptArgs.splice(1, 0, command)
    }
    scripts.test = yes ? command || notest : prompt(...promptArgs)

    return scripts
  }
}

if (!package.repository) {
  exports.repository = async () => {
    const gitConfigPath = path.resolve(dirname, '.git', 'config')
    const gconf = await fs.readFile(gitConfigPath, 'utf8').catch(() => '')
    const lines = gconf.split(/\r?\n/)

    let url
    const i = lines.indexOf('[remote "origin"]')

    if (i !== -1) {
      url = lines[i + 1]
      if (!url.match(/^\s*url =/)) {
        url = lines[i + 2]
      }
      if (!url.match(/^\s*url =/)) {
        url = null
      } else {
        url = url.replace(/^\s*url = /, '')
      }
    }

    if (url && url.match(/^git@github.com:/)) {
      url = url.replace(/^git@github.com:/, 'https://github.com/')
    }

    return yes ? url || '' : prompt('git repository', url || undefined)
  }
}

if (!package.keywords) {
  exports.keywords = yes ? '' : prompt('keywords', (data) => {
    if (!data) {
      return
    }
    if (Array.isArray(data)) {
      data = data.join(' ')
    }
    if (typeof data !== 'string') {
      return data
    }
    return data.split(/[\s,]+/)
  })
}

if (!package.author) {
  const authorName = getConfig('author.name')
  exports.author = authorName
    ? {
      name: authorName,
      email: getConfig('author.email'),
      url: getConfig('author.url'),
    }
    : yes ? '' : prompt('author')
}

const license = package.license || getConfig('license') || 'ISC'
exports.license = yes ? license : prompt('license', license, (data) => {
  const its = validateLicense(data)
  if (its.validForNewPackages) {
    return data
  }
  const errors = (its.errors || []).concat(its.warnings || [])
  return invalid(`Sorry, ${errors.join(' and ')}.`)
})

const type = package.type || getConfig('type') || 'commonjs'
exports.type = yes ? type : prompt('type', type, (data) => {
  return data
})
