
const promzard = require('promzard')
const path = require('path')
const semver = require('semver')
const { read } = require('read')
const util = require('util')
const PackageJson = require('@npmcli/package-json')

const def = require.resolve('./default-input.js')

const extras = [
  'bundleDependencies',
  'gypfile',
  'serverjs',
  'scriptpath',
  'readme',
  'bin',
  'githead',
  'fillTypes',
  'normalizeData',
]

const isYes = (c) => !!(c.get('yes') || c.get('y') || c.get('force') || c.get('f'))

const getConfig = (c) => {
  // accept either a plain-jane object, or a config object with a "get" method.
  if (typeof c.get !== 'function') {
    const data = c
    return {
      get: (k) => data[k],
      toJSON: () => data,
    }
  }
  return c
}

// Coverage disabled because this is just walking back the fixPeople
// normalization from the normalizeData step and we don't need to re-test all
// of those paths.
/* istanbul ignore next */
const stringifyPerson = (p) => {
  const { name, url, web, email, mail } = p
  const u = url || web
  const e = email || mail
  return `${name}${e ? ` <${e}>` : ''}${u ? ` (${u})` : ''}`
}
async function init (dir,
  // TODO test for non-default definitions
  /* istanbul ignore next */
  input = def,
  c = {}) {
  const config = getConfig(c)
  const yes = isYes(config)
  const packageFile = path.resolve(dir, 'package.json')

  // read what's already there to inform our prompts
  const pkg = await PackageJson.load(dir, { create: true })
  await pkg.normalize()

  if (!semver.valid(pkg.content.version)) {
    delete pkg.content.version
  }

  // make sure that the input is valid. if not, use the default
  const pzData = await promzard(path.resolve(input), {
    yes,
    config,
    filename: packageFile,
    dirname: dir,
    basename: path.basename(dir),
    package: pkg.content,
  }, { backupFile: def })

  for (const [k, v] of Object.entries(pzData)) {
    if (v != null) {
      pkg.content[k] = v
    }
  }

  await pkg.normalize({ steps: extras })

  // turn the objects back into somewhat more humane strings.
  // "normalizeData" does this and there isn't a way to choose which of those steps happen
  if (pkg.content.author) {
    pkg.content.author = stringifyPerson(pkg.content.author)
  }

  // no need for the readme now.
  delete pkg.content.readme
  delete pkg.content.readmeFilename

  // really don't want to have this lying around in the file
  delete pkg.content._id

  // ditto
  delete pkg.content.gitHead

  // if the repo is empty, remove it.
  if (!pkg.content.repository) {
    delete pkg.content.repository
  }

  // readJson filters out empty descriptions, but init-package-json
  // traditionally leaves them alone
  if (!pkg.content.description) {
    pkg.content.description = pzData.description
  }

  // optionalDependencies don't need to be repeated in two places
  if (pkg.content.dependencies) {
    if (pkg.content.optionalDependencies) {
      for (const name of Object.keys(pkg.content.optionalDependencies)) {
        delete pkg.content.dependencies[name]
      }
    }
    if (Object.keys(pkg.content.dependencies).length === 0) {
      delete pkg.content.dependencies
    }
  }

  const stringified = JSON.stringify(pkg.content, null, 2) + '\n'
  const msg = util.format('%s:\n\n%s\n', packageFile, stringified)

  if (yes) {
    await pkg.save()
    if (!config.get('silent')) {
      // eslint-disable-next-line no-console
      console.log(`Wrote to ${msg}`)
    }
    return pkg.content
  }

  // eslint-disable-next-line no-console
  console.log(`About to write to ${msg}`)
  const ok = await read({ prompt: 'Is this OK? ', default: 'yes' })
  if (!ok || !ok.toLowerCase().startsWith('y')) {
    // eslint-disable-next-line no-console
    console.log('Aborted.')
    return
  }

  await pkg.save({ sort: true })
  return pkg.content
}

module.exports = init
module.exports.yes = isYes
