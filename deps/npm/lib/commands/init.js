const { statSync } = require('node:fs')
const { relative, resolve } = require('node:path')
const { mkdir } = require('node:fs/promises')
const initJson = require('init-package-json')
const npa = require('npm-package-arg')
const libexec = require('libnpmexec')
const mapWorkspaces = require('@npmcli/map-workspaces')
const PackageJson = require('@npmcli/package-json')
const { log, output, input } = require('proc-log')
const updateWorkspaces = require('../utils/update-workspaces.js')
const BaseCommand = require('../base-cmd.js')

const posixPath = p => p.split('\\').join('/')

class Init extends BaseCommand {
  static description = 'Create a package.json file'
  static params = [
    'init-author-name',
    'init-author-url',
    'init-license',
    'init-module',
    'init-version',
    'yes',
    'force',
    'scope',
    'workspace',
    'workspaces',
    'workspaces-update',
    'include-workspace-root',
  ]

  static name = 'init'
  static usage = [
    '<package-spec> (same as `npx create-<package-spec>`)',
    '<@scope> (same as `npx <@scope>/create`)',
  ]

  static workspaces = true
  static ignoreImplicitWorkspace = false

  async exec (args) {
    // npm exec style
    if (args.length) {
      return await this.execCreate(args)
    }

    // no args, uses classic init-package-json boilerplate
    await this.template()
  }

  async execWorkspaces (args) {
    // if the root package is uninitiated, take care of it first
    if (this.npm.flatOptions.includeWorkspaceRoot) {
      await this.exec(args)
    }

    // reads package.json for the top-level folder first, by doing this we
    // ensure the command throw if no package.json is found before trying
    // to create a workspace package.json file or its folders
    const { content: pkg } = await PackageJson.normalize(this.npm.localPrefix).catch(err => {
      if (err.code === 'ENOENT') {
        log.warn('init', 'Missing package.json. Try with `--include-workspace-root`.')
      }
      throw err
    })

    // these are workspaces that are being created, so we cant use
    // this.setWorkspaces()
    const filters = this.npm.config.get('workspace')
    const wPath = filterArg => resolve(this.npm.localPrefix, filterArg)

    const workspacesPaths = []
    // npm-exec style, runs in the context of each workspace filter
    if (args.length) {
      for (const filterArg of filters) {
        const path = wPath(filterArg)
        await mkdir(path, { recursive: true })
        workspacesPaths.push(path)
        await this.execCreate(args, path)
        await this.setWorkspace(pkg, path)
      }
      return
    }

    // no args, uses classic init-package-json boilerplate
    for (const filterArg of filters) {
      const path = wPath(filterArg)
      await mkdir(path, { recursive: true })
      workspacesPaths.push(path)
      await this.template(path)
      await this.setWorkspace(pkg, path)
    }

    // reify packages once all workspaces have been initialized
    await this.update(workspacesPaths)
  }

  async execCreate (args, runPath = process.cwd()) {
    const [initerName, ...otherArgs] = args
    let packageName = initerName

    // Only a scope, possibly with a version
    if (/^@[^/]+$/.test(initerName)) {
      const [, scope, version] = initerName.split('@')
      packageName = `@${scope}/create`
      if (version) {
        packageName = `${packageName}@${version}`
      }
    } else {
      const req = npa(initerName)
      if (req.type === 'git' && req.hosted) {
        const { user, project } = req.hosted
        packageName = initerName.replace(`${user}/${project}`, `${user}/create-${project}`)
      } else if (req.registry) {
        packageName = `${req.name.replace(/^(@[^/]+\/)?/, '$1create-')}@${req.rawSpec}`
      } else {
        throw Object.assign(new Error(
          'Unrecognized initializer: ' + initerName +
          '\nFor more package binary executing power check out `npx`:' +
          '\nhttps://docs.npmjs.com/cli/commands/npx'
        ), { code: 'EUNSUPPORTED' })
      }
    }

    const newArgs = [packageName, ...otherArgs]
    const {
      flatOptions,
      localBin,
      globalBin,
      chalk,
    } = this.npm
    const scriptShell = this.npm.config.get('script-shell') || undefined
    const yes = this.npm.config.get('yes')

    await libexec({
      ...flatOptions,
      args: newArgs,
      localBin,
      globalBin,
      output,
      chalk,
      path: this.npm.localPrefix,
      runPath,
      scriptShell,
      yes,
    })
  }

  async template (path = process.cwd()) {
    const initFile = this.npm.config.get('init-module')
    if (!this.npm.config.get('yes') && !this.npm.config.get('force')) {
      output.standard([
        'This utility will walk you through creating a package.json file.',
        'It only covers the most common items, and tries to guess sensible defaults.',
        '',
        'See `npm help init` for definitive documentation on these fields',
        'and exactly what they do.',
        '',
        'Use `npm install <pkg>` afterwards to install a package and',
        'save it as a dependency in the package.json file.',
        '',
        'Press ^C at any time to quit.',
      ].join('\n'))
    }

    try {
      const data = await input.read(() => initJson(path, initFile, this.npm.config))
      log.silly('package data', data)
      return data
    } catch (er) {
      if (er.message === 'canceled') {
        log.warn('init', 'canceled')
      } else {
        throw er
      }
    }
  }

  async setWorkspace (pkg, workspacePath) {
    const workspaces = await mapWorkspaces({ cwd: this.npm.localPrefix, pkg })

    // skip setting workspace if current package.json glob already satisfies it
    for (const wPath of workspaces.values()) {
      if (wPath === workspacePath) {
        return
      }
    }

    // if a create-pkg didn't generate a package.json at the workspace
    // folder level, it might not be recognized as a workspace by
    // mapWorkspaces, so we're just going to avoid touching the
    // top-level package.json
    try {
      statSync(resolve(workspacePath, 'package.json'))
    } catch {
      return
    }

    const pkgJson = await PackageJson.load(this.npm.localPrefix)

    pkgJson.update({
      workspaces: [
        ...(pkgJson.content.workspaces || []),
        posixPath(relative(this.npm.localPrefix, workspacePath)),
      ],
    })

    await pkgJson.save()
  }

  async update (workspacesPaths) {
    // translate workspaces paths into an array containing workspaces names
    const workspaces = []
    for (const path of workspacesPaths) {
      const { content: { name } } = await PackageJson.normalize(path).catch(() => ({ content: {} }))

      if (name) {
        workspaces.push(name)
      }
    }

    const {
      config,
      flatOptions,
      localPrefix,
    } = this.npm

    await updateWorkspaces({
      config,
      flatOptions,
      localPrefix,
      npm: this.npm,
      workspaces,
    })
  }
}

module.exports = Init
