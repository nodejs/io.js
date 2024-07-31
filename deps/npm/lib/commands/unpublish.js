const libaccess = require('libnpmaccess')
const libunpub = require('libnpmpublish').unpublish
const npa = require('npm-package-arg')
const pacote = require('pacote')
const { output, log } = require('proc-log')
const pkgJson = require('@npmcli/package-json')
const { flatten } = require('@npmcli/config/lib/definitions')
const getIdentity = require('../utils/get-identity.js')
const { otplease } = require('../utils/auth.js')
const BaseCommand = require('../base-cmd.js')

const LAST_REMAINING_VERSION_ERROR = 'Refusing to delete the last version of the package. ' +
'It will block from republishing a new version for 24 hours.\n' +
'Run with --force to do this.'

class Unpublish extends BaseCommand {
  static description = 'Remove a package from the registry'
  static name = 'unpublish'
  static params = ['dry-run', 'force', 'workspace', 'workspaces']
  static usage = ['[<package-spec>]']
  static workspaces = true
  static ignoreImplicitWorkspace = false

  static async getKeysOfVersions (name, opts) {
    const packument = await pacote.packument(name, {
      ...opts,
      spec: name,
      query: { write: true },
    })
    return Object.keys(packument.versions)
  }

  static async completion (args, npm) {
    const { partialWord, conf } = args

    if (conf.argv.remain.length >= 3) {
      return []
    }

    const opts = { ...npm.flatOptions }
    const username = await getIdentity(npm, { ...opts }).catch(() => null)
    if (!username) {
      return []
    }

    const access = await libaccess.getPackages(username, opts)
    // do a bit of filtering at this point, so that we don't need
    // to fetch versions for more than one thing, but also don't
    // accidentally unpublish a whole project
    let pkgs = Object.keys(access)
    if (!partialWord || !pkgs.length) {
      return pkgs
    }

    const pp = npa(partialWord).name
    pkgs = pkgs.filter(p => !p.indexOf(pp))
    if (pkgs.length > 1) {
      return pkgs
    }

    const versions = await Unpublish.getKeysOfVersions(pkgs[0], opts)
    if (!versions.length) {
      return pkgs
    } else {
      return versions.map(v => `${pkgs[0]}@${v}`)
    }
  }

  async exec (args, { localPrefix } = {}) {
    if (args.length > 1) {
      throw this.usageError()
    }

    // workspace mode
    if (!localPrefix) {
      localPrefix = this.npm.localPrefix
    }

    const force = this.npm.config.get('force')
    const { silent } = this.npm
    const dryRun = this.npm.config.get('dry-run')

    let spec
    if (args.length) {
      spec = npa(args[0])
      if (spec.type !== 'version' && spec.rawSpec !== '*') {
        throw this.usageError(
          'Can only unpublish a single version, or the entire project.\n' +
          'Tags and ranges are not supported.'
        )
      }
    }

    log.silly('unpublish', 'args[0]', args[0])
    log.silly('unpublish', 'spec', spec)

    if (spec?.rawSpec === '*' && !force) {
      throw this.usageError(
        'Refusing to delete entire project.\n' +
        'Run with --force to do this.'
      )
    }

    const opts = { ...this.npm.flatOptions }

    let manifest
    try {
      const { content } = await pkgJson.prepare(localPrefix)
      manifest = content
    } catch (err) {
      if (err.code === 'ENOENT' || err.code === 'ENOTDIR') {
        if (!spec) {
          // We needed a local package.json to figure out what package to
          // unpublish
          throw this.usageError()
        }
      } else {
        // folks should know if ANY local package.json had a parsing error.
        // They may be relying on `publishConfig` to be loading and we don't
        // want to ignore errors in that case.
        throw err
      }
    }

    let pkgVersion // for cli output
    if (spec) {
      pkgVersion = spec.type === 'version' ? `@${spec.rawSpec}` : ''
    } else {
      spec = npa.resolve(manifest.name, manifest.version)
      log.verbose('unpublish', manifest)
      pkgVersion = manifest.version ? `@${manifest.version}` : ''
      if (!manifest.version && !force) {
        throw this.usageError(
          'Refusing to delete entire project.\n' +
          'Run with --force to do this.'
        )
      }
    }

    // If localPrefix has a package.json with a name that matches the package
    // being unpublished, load up the publishConfig
    if (manifest?.name === spec.name && manifest.publishConfig) {
      const cliFlags = this.npm.config.data.get('cli').raw
      // Filter out properties set in CLI flags to prioritize them over
      // corresponding `publishConfig` settings
      const filteredPublishConfig = Object.fromEntries(
        Object.entries(manifest.publishConfig).filter(([key]) => !(key in cliFlags)))
      flatten(filteredPublishConfig, opts)
    }

    const versions = await Unpublish.getKeysOfVersions(spec.name, opts)
    if (versions.length === 1 && spec.rawSpec === versions[0] && !force) {
      throw this.usageError(LAST_REMAINING_VERSION_ERROR)
    }
    if (versions.length === 1) {
      pkgVersion = ''
    }

    if (!dryRun) {
      await otplease(this.npm, opts, o => libunpub(spec, o))
    }
    if (!silent) {
      output.standard(`- ${spec.name}${pkgVersion}`)
    }
  }

  async execWorkspaces (args) {
    await this.setWorkspaces()

    for (const path of this.workspacePaths) {
      await this.exec(args, { localPrefix: path })
    }
  }
}

module.exports = Unpublish
