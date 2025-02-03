const { log, output } = require('proc-log')
const semver = require('semver')
const pack = require('libnpmpack')
const libpub = require('libnpmpublish').publish
const runScript = require('@npmcli/run-script')
const pacote = require('pacote')
const npa = require('npm-package-arg')
const npmFetch = require('npm-registry-fetch')
const { redactLog: replaceInfo } = require('@npmcli/redact')
const { otplease } = require('../utils/auth.js')
const { getContents, logTar } = require('../utils/tar.js')
// for historical reasons, publishConfig in package.json can contain ANY config
// keys that npm supports in .npmrc files and elsewhere.  We *may* want to
// revisit this at some point, and have a minimal set that's a SemVer-major
// change that ought to get a RFC written on it.
const { flatten } = require('@npmcli/config/lib/definitions')
const pkgJson = require('@npmcli/package-json')
const BaseCommand = require('../base-cmd.js')

class Publish extends BaseCommand {
  static description = 'Publish a package'
  static name = 'publish'
  static params = [
    'tag',
    'access',
    'dry-run',
    'otp',
    'workspace',
    'workspaces',
    'include-workspace-root',
    'provenance',
  ]

  static usage = ['<package-spec>']
  static workspaces = true
  static ignoreImplicitWorkspace = false

  async exec (args) {
    if (args.length === 0) {
      args = ['.']
    }
    if (args.length !== 1) {
      throw this.usageError()
    }

    await this.#publish(args)
  }

  async execWorkspaces (args) {
    const useWorkspaces = args.length === 0 || args.includes('.')
    if (!useWorkspaces) {
      log.warn('Ignoring workspaces for specified package(s)')
      return this.exec(args)
    }
    await this.setWorkspaces()

    for (const [name, workspace] of this.workspaces.entries()) {
      try {
        await this.#publish([workspace], { workspace: name })
      } catch (err) {
        if (err.code !== 'EPRIVATE') {
          throw err
        }
        log.warn('publish', `Skipping workspace ${this.npm.chalk.cyan(name)}, marked as ${this.npm.chalk.bold('private')}`)
      }
    }
  }

  async #publish (args, { workspace } = {}) {
    log.verbose('publish', replaceInfo(args))

    const unicode = this.npm.config.get('unicode')
    const dryRun = this.npm.config.get('dry-run')
    const json = this.npm.config.get('json')
    const defaultTag = this.npm.config.get('tag')
    const ignoreScripts = this.npm.config.get('ignore-scripts')
    const { silent } = this.npm

    if (semver.validRange(defaultTag)) {
      throw new Error('Tag name must not be a valid SemVer range: ' + defaultTag.trim())
    }

    const opts = { ...this.npm.flatOptions, progress: false }

    // you can publish name@version, ./foo.tgz, etc.
    // even though the default is the 'file:.' cwd.
    const spec = npa(args[0])
    let manifest = await this.#getManifest(spec, opts)

    // only run scripts for directory type publishes
    if (spec.type === 'directory' && !ignoreScripts) {
      await runScript({
        event: 'prepublishOnly',
        path: spec.fetchSpec,
        stdio: 'inherit',
        pkg: manifest,
      })
    }

    // we pass dryRun: true to libnpmpack so it doesn't write the file to disk
    const tarballData = await pack(spec, {
      ...opts,
      foregroundScripts: this.npm.config.isDefault('foreground-scripts')
        ? true
        : this.npm.config.get('foreground-scripts'),
      dryRun: true,
      prefix: this.npm.localPrefix,
      workspaces: this.workspacePaths,
    })
    const pkgContents = await getContents(manifest, tarballData)
    const logPkg = () => logTar(pkgContents, { unicode, json, key: workspace })

    // The purpose of re-reading the manifest is in case it changed,
    // so that we send the latest and greatest thing to the registry
    // note that publishConfig might have changed as well!
    manifest = await this.#getManifest(spec, opts, true)
    const force = this.npm.config.get('force')
    const isDefaultTag = this.npm.config.isDefault('tag') && !manifest.publishConfig?.tag

    if (!force) {
      const isPreRelease = Boolean(semver.parse(manifest.version).prerelease.length)
      if (isPreRelease && isDefaultTag) {
        throw new Error('You must specify a tag using --tag when publishing a prerelease version.')
      }
    }

    // If we are not in JSON mode then we show the user the contents of the tarball
    // before it is published so they can see it while their otp is pending
    if (!json) {
      logPkg()
    }

    const resolved = npa.resolve(manifest.name, manifest.version)

    // make sure tag is valid, this will throw if invalid
    npa(`${manifest.name}@${defaultTag}`)

    const registry = npmFetch.pickRegistry(resolved, opts)
    const creds = this.npm.config.getCredentialsByURI(registry)
    const noCreds = !(creds.token || creds.username || creds.certfile && creds.keyfile)
    const outputRegistry = replaceInfo(registry)

    // if a workspace package is marked private then we skip it
    if (workspace && manifest.private) {
      throw Object.assign(
        new Error(`This package has been marked as private
  Remove the 'private' field from the package.json to publish it.`),
        { code: 'EPRIVATE' }
      )
    }

    if (noCreds) {
      const msg = `This command requires you to be logged in to ${outputRegistry}`
      if (dryRun) {
        log.warn('', `${msg} (dry-run)`)
      } else {
        throw Object.assign(new Error(msg), { code: 'ENEEDAUTH' })
      }
    }

    if (!force) {
      const { highestVersion, versions } = await this.#registryVersions(resolved, registry)
      /* eslint-disable-next-line max-len */
      const highestVersionIsGreater = !!highestVersion && semver.gte(highestVersion, manifest.version)

      if (versions.includes(manifest.version)) {
        throw new Error(`You cannot publish over the previously published versions: ${manifest.version}.`)
      }

      if (highestVersionIsGreater && isDefaultTag) {
        throw new Error(`Cannot implicitly apply the "latest" tag because previously published version ${highestVersion} is higher than the new version ${manifest.version}. You must specify a tag using --tag.`)
      }
    }

    const access = opts.access === null ? 'default' : opts.access
    let msg = `Publishing to ${outputRegistry} with tag ${defaultTag} and ${access} access`
    if (dryRun) {
      msg = `${msg} (dry-run)`
    }

    log.notice('', msg)

    if (!dryRun) {
      await otplease(this.npm, opts, o => libpub(manifest, tarballData, o))
    }

    // In json mode we dont log until the publish has completed as this will
    // add it to the output only if completes successfully
    if (json) {
      logPkg()
    }

    if (spec.type === 'directory' && !ignoreScripts) {
      await runScript({
        event: 'publish',
        path: spec.fetchSpec,
        stdio: 'inherit',
        pkg: manifest,
      })

      await runScript({
        event: 'postpublish',
        path: spec.fetchSpec,
        stdio: 'inherit',
        pkg: manifest,
      })
    }

    if (!json && !silent) {
      output.standard(`+ ${pkgContents.id}`)
    }
  }

  async #registryVersions (spec, registry) {
    try {
      const packument = await pacote.packument(spec, {
        ...this.npm.flatOptions,
        preferOnline: true,
        registry,
      })
      if (typeof packument?.versions === 'undefined') {
        return { versions: [], highestVersion: null }
      }
      const ordered = Object.keys(packument?.versions)
        .flatMap(v => {
          const s = new semver.SemVer(v)
          if ((s.prerelease.length > 0) || packument.versions[v].deprecated) {
            return []
          }
          return s
        })
        .sort((a, b) => b.compare(a))
      const highestVersion = ordered.length >= 1 ? ordered[0].version : null
      const versions = ordered.map(v => v.version)
      return { versions, highestVersion }
    } catch (e) {
      return { versions: [], highestVersion: null }
    }
  }

  // if it's a directory, read it from the file system
  // otherwise, get the full metadata from whatever it is
  // XXX can't pacote read the manifest from a directory?
  async #getManifest (spec, opts, logWarnings = false) {
    let manifest
    if (spec.type === 'directory') {
      const changes = []
      const pkg = await pkgJson.fix(spec.fetchSpec, { changes })
      if (changes.length && logWarnings) {
        log.warn('publish', 'npm auto-corrected some errors in your package.json when publishing.  Please run "npm pkg fix" to address these errors.')
        log.warn('publish', `errors corrected:\n${changes.join('\n')}`)
      }
      // Prepare is the special function for publishing, different than normalize
      const { content } = await pkg.prepare()
      manifest = content
    } else {
      manifest = await pacote.manifest(spec, {
        ...opts,
        fullmetadata: true,
        fullReadJson: true,
      })
    }
    if (manifest.publishConfig) {
      const cliFlags = this.npm.config.data.get('cli').raw
      // Filter out properties set in CLI flags to prioritize them over
      // corresponding `publishConfig` settings
      const filteredPublishConfig = Object.fromEntries(
        Object.entries(manifest.publishConfig).filter(([key]) => !(key in cliFlags)))
      flatten(filteredPublishConfig, opts)
    }
    return manifest
  }
}

module.exports = Publish
