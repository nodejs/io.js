const pacote = require('pacote')
const libpack = require('libnpmpack')
const npa = require('npm-package-arg')
const { log, output } = require('proc-log')
const { getContents, logTar } = require('../utils/tar.js')
const BaseCommand = require('../base-cmd.js')

class Pack extends BaseCommand {
  static description = 'Create a tarball from a package'
  static name = 'pack'
  static params = [
    'dry-run',
    'json',
    'pack-destination',
    'workspace',
    'workspaces',
    'include-workspace-root',
  ]

  static usage = ['<package-spec>']
  static workspaces = true
  static ignoreImplicitWorkspace = false

  async exec (args) {
    if (args.length === 0) {
      args = ['.']
    }

    const unicode = this.npm.config.get('unicode')
    const json = this.npm.config.get('json')

    // Get the manifests and filenames first so we can bail early on manifest
    // errors before making any tarballs
    const manifests = []
    for (const arg of args) {
      const spec = npa(arg)
      const manifest = await pacote.manifest(spec, this.npm.flatOptions)
      if (!manifest._id) {
        throw new Error('Invalid package, must have name and version')
      }
      manifests.push({ arg, manifest })
    }

    // Load tarball names up for printing afterward to isolate from the
    // noise generated during packing
    const tarballs = []
    for (const { arg, manifest } of manifests) {
      const tarballData = await libpack(arg, {
        ...this.npm.flatOptions,
        foregroundScripts: this.npm.config.isDefault('foreground-scripts')
          ? true
          : this.npm.config.get('foreground-scripts'),
        prefix: this.npm.localPrefix,
        workspaces: this.workspacePaths,
      })
      tarballs.push(await getContents(manifest, tarballData))
    }

    for (const [index, tar] of Object.entries(tarballs)) {
      // XXX(BREAKING_CHANGE): publish outputs a json object with package
      // names as keys. Pack should do the same here instead of an array
      logTar(tar, { unicode, json, key: index })
      if (!json) {
        output.standard(tar.filename.replace(/^@/, '').replace(/\//, '-'))
      }
    }
  }

  async execWorkspaces (args) {
    // If they either ask for nothing, or explicitly include '.' in the args,
    // we effectively translate that into each workspace requested

    const useWorkspaces = args.length === 0 || args.includes('.')

    if (!useWorkspaces) {
      log.warn('Ignoring workspaces for specified package(s)')
      return this.exec(args)
    }

    await this.setWorkspaces()
    return this.exec([...this.workspacePaths, ...args.filter(a => a !== '.')])
  }
}

module.exports = Pack
