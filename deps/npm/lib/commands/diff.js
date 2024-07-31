const { resolve } = require('node:path')
const semver = require('semver')
const libnpmdiff = require('libnpmdiff')
const npa = require('npm-package-arg')
const pacote = require('pacote')
const pickManifest = require('npm-pick-manifest')
const { log, output } = require('proc-log')
const pkgJson = require('@npmcli/package-json')
const BaseCommand = require('../base-cmd.js')

class Diff extends BaseCommand {
  static description = 'The registry diff command'
  static name = 'diff'
  static usage = [
    '[...<paths>]',
  ]

  static params = [
    'diff',
    'diff-name-only',
    'diff-unified',
    'diff-ignore-all-space',
    'diff-no-prefix',
    'diff-src-prefix',
    'diff-dst-prefix',
    'diff-text',
    'global',
    'tag',
    'workspace',
    'workspaces',
    'include-workspace-root',
  ]

  static workspaces = true
  static ignoreImplicitWorkspace = false

  async exec (args) {
    const specs = this.npm.config.get('diff').filter(d => d)
    if (specs.length > 2) {
      throw this.usageError(`Can't use more than two --diff arguments.`)
    }

    // execWorkspaces may have set this already
    if (!this.prefix) {
      this.prefix = this.npm.prefix
    }

    // this is the "top" directory, one up from node_modules
    // in global mode we have to walk one up from globalDir because our
    // node_modules is sometimes under ./lib, and in global mode we're only ever
    // walking through node_modules (because we will have been given a package
    // name already)
    if (this.npm.global) {
      this.top = resolve(this.npm.globalDir, '..')
    } else {
      this.top = this.prefix
    }

    const [a, b] = await this.retrieveSpecs(specs)
    log.info('diff', { src: a, dst: b })

    const res = await libnpmdiff([a, b], {
      ...this.npm.flatOptions,
      diffFiles: args,
      where: this.top,
    })
    return output.standard(res)
  }

  async execWorkspaces (args) {
    await this.setWorkspaces()
    for (const workspacePath of this.workspacePaths) {
      this.top = workspacePath
      this.prefix = workspacePath
      await this.exec(args)
    }
  }

  // get the package name from the packument at `path`
  // throws if no packument is present OR if it does not have `name` attribute
  async packageName () {
    let name
    try {
      const { content: pkg } = await pkgJson.normalize(this.prefix)
      name = pkg.name
    } catch (e) {
      log.verbose('diff', 'could not read project dir package.json')
    }

    if (!name) {
      throw this.usageError('Needs multiple arguments to compare or run from a project dir.')
    }

    return name
  }

  async retrieveSpecs ([a, b]) {
    if (a && b) {
      const specs = await this.convertVersionsToSpecs([a, b])
      return this.findVersionsByPackageName(specs)
    }

    // no arguments, defaults to comparing cwd
    // to its latest published registry version
    if (!a) {
      const pkgName = await this.packageName()
      return [
        `${pkgName}@${this.npm.config.get('tag')}`,
        `file:${this.prefix.replace(/#/g, '%23')}`,
      ]
    }

    // single argument, used to compare wanted versions of an
    // installed dependency or to compare the cwd to a published version
    let noPackageJson
    let pkgName
    try {
      const { content: pkg } = await pkgJson.normalize(this.prefix)
      pkgName = pkg.name
    } catch (e) {
      log.verbose('diff', 'could not read project dir package.json')
      noPackageJson = true
    }

    const missingPackageJson =
      this.usageError('Needs multiple arguments to compare or run from a project dir.')

    // using a valid semver range, that means it should just diff
    // the cwd against a published version to the registry using the
    // same project name and the provided semver range
    if (semver.validRange(a)) {
      if (!pkgName) {
        throw missingPackageJson
      }
      return [
        `${pkgName}@${a}`,
        `file:${this.prefix.replace(/#/g, '%23')}`,
      ]
    }

    // when using a single package name as arg and it's part of the current
    // install tree, then retrieve the current installed version and compare
    // it against the same value `npm outdated` would suggest you to update to
    const spec = npa(a)
    if (spec.registry) {
      let actualTree
      let node
      const Arborist = require('@npmcli/arborist')
      try {
        const opts = {
          ...this.npm.flatOptions,
          path: this.top,
        }
        const arb = new Arborist(opts)
        actualTree = await arb.loadActual(opts)
        node = actualTree &&
          actualTree.inventory.query('name', spec.name)
            .values().next().value
      } catch (e) {
        log.verbose('diff', 'failed to load actual install tree')
      }

      if (!node || !node.name || !node.package || !node.package.version) {
        if (noPackageJson) {
          throw missingPackageJson
        }
        return [
          `${spec.name}@${spec.fetchSpec}`,
          `file:${this.prefix.replace(/#/g, '%23')}`,
        ]
      }

      const tryRootNodeSpec = () =>
        (actualTree && actualTree.edgesOut.get(spec.name) || {}).spec

      const tryAnySpec = () => {
        for (const edge of node.edgesIn) {
          return edge.spec
        }
      }

      const aSpec = `file:${node.realpath.replace(/#/g, '%23')}`

      // finds what version of the package to compare against, if a exact
      // version or tag was passed than it should use that, otherwise
      // work from the top of the arborist tree to find the original semver
      // range declared in the package that depends on the package.
      let bSpec
      if (spec.rawSpec !== '*') {
        bSpec = spec.rawSpec
      } else {
        const bTargetVersion =
          tryRootNodeSpec()
          || tryAnySpec()

        // figure out what to compare against,
        // follows same logic to npm outdated "Wanted" results
        const packument = await pacote.packument(spec, {
          ...this.npm.flatOptions,
          preferOnline: true,
        })
        bSpec = pickManifest(
          packument,
          bTargetVersion,
          { ...this.npm.flatOptions }
        ).version
      }

      return [
        `${spec.name}@${aSpec}`,
        `${spec.name}@${bSpec}`,
      ]
    } else if (spec.type === 'directory') {
      return [
        `file:${spec.fetchSpec.replace(/#/g, '%23')}`,
        `file:${this.prefix.replace(/#/g, '%23')}`,
      ]
    } else {
      throw this.usageError(`Spec type ${spec.type} not supported.`)
    }
  }

  async convertVersionsToSpecs ([a, b]) {
    const semverA = semver.validRange(a)
    const semverB = semver.validRange(b)

    // both specs are semver versions, assume current project dir name
    if (semverA && semverB) {
      let pkgName
      try {
        const { content: pkg } = await pkgJson.normalize(this.prefix)
        pkgName = pkg.name
      } catch (e) {
        log.verbose('diff', 'could not read project dir package.json')
      }

      if (!pkgName) {
        throw this.usageError('Needs to be run from a project dir in order to diff two versions.')
      }

      return [`${pkgName}@${a}`, `${pkgName}@${b}`]
    }

    // otherwise uses the name from the other arg to
    // figure out the spec.name of what to compare
    if (!semverA && semverB) {
      return [a, `${npa(a).name}@${b}`]
    }

    if (semverA && !semverB) {
      return [`${npa(b).name}@${a}`, b]
    }

    // no valid semver ranges used
    return [a, b]
  }

  async findVersionsByPackageName (specs) {
    let actualTree
    const Arborist = require('@npmcli/arborist')
    try {
      const opts = {
        ...this.npm.flatOptions,
        path: this.top,
      }
      const arb = new Arborist(opts)
      actualTree = await arb.loadActual(opts)
    } catch (e) {
      log.verbose('diff', 'failed to load actual install tree')
    }

    return specs.map(i => {
      const spec = npa(i)
      if (spec.rawSpec !== '*') {
        return i
      }

      const node = actualTree
        && actualTree.inventory.query('name', spec.name)
          .values().next().value

      const res = !node || !node.package || !node.package.version
        ? spec.fetchSpec
        : `file:${node.realpath.replace(/#/g, '%23')}`

      return `${spec.name}@${res}`
    })
  }
}

module.exports = Diff
