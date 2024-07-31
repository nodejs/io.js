const reifyFinish = require('../utils/reify-finish.js')
const runScript = require('@npmcli/run-script')
const fs = require('node:fs/promises')
const path = require('node:path')
const { log, time } = require('proc-log')
const validateLockfile = require('../utils/validate-lockfile.js')
const ArboristWorkspaceCmd = require('../arborist-cmd.js')
const getWorkspaces = require('../utils/get-workspaces.js')

class CI extends ArboristWorkspaceCmd {
  static description = 'Clean install a project'
  static name = 'ci'

  // These are in the order they will show up in when running "-h"
  static params = [
    'install-strategy',
    'legacy-bundling',
    'global-style',
    'omit',
    'include',
    'strict-peer-deps',
    'foreground-scripts',
    'ignore-scripts',
    'audit',
    'bin-links',
    'fund',
    'dry-run',
    ...super.params,
  ]

  async exec () {
    if (this.npm.global) {
      throw Object.assign(new Error('`npm ci` does not work for global packages'), {
        code: 'ECIGLOBAL',
      })
    }

    const where = this.npm.prefix
    const Arborist = require('@npmcli/arborist')
    const opts = {
      ...this.npm.flatOptions,
      packageLock: true, // npm ci should never skip lock files
      path: where,
      save: false, // npm ci should never modify the lockfile or package.json
      workspaces: this.workspaceNames,
    }

    const arb = new Arborist(opts)
    await arb.loadVirtual().catch(er => {
      log.verbose('loadVirtual', er.stack)
      const msg =
        'The `npm ci` command can only install with an existing package-lock.json or\n' +
        'npm-shrinkwrap.json with lockfileVersion >= 1. Run an install with npm@5 or\n' +
        'later to generate a package-lock.json file, then try again.'
      throw this.usageError(msg)
    })

    // retrieves inventory of packages from loaded virtual tree (lock file)
    const virtualInventory = new Map(arb.virtualTree.inventory)

    // build ideal tree step needs to come right after retrieving the virtual
    // inventory since it's going to erase the previous ref to virtualTree
    await arb.buildIdealTree()

    // verifies that the packages from the ideal tree will match
    // the same versions that are present in the virtual tree (lock file)
    // throws a validation error in case of mismatches
    const errors = validateLockfile(virtualInventory, arb.idealTree.inventory)
    if (errors.length) {
      throw this.usageError(
        '`npm ci` can only install packages when your package.json and ' +
        'package-lock.json or npm-shrinkwrap.json are in sync. Please ' +
        'update your lock file with `npm install` ' +
        'before continuing.\n\n' +
        errors.join('\n')
      )
    }

    const dryRun = this.npm.config.get('dry-run')
    if (!dryRun) {
      const workspacePaths = await getWorkspaces([], {
        path: this.npm.localPrefix,
        includeWorkspaceRoot: true,
      })

      // Only remove node_modules after we've successfully loaded the virtual
      // tree and validated the lockfile
      await time.start('npm-ci:rm', async () => {
        return await Promise.all([...workspacePaths.values()].map(async modulePath => {
          const fullPath = path.join(modulePath, 'node_modules')
          // get the list of entries so we can skip the glob for performance
          const entries = await fs.readdir(fullPath, null).catch(() => [])
          return Promise.all(entries.map(folder => {
            return fs.rm(path.join(fullPath, folder), { force: true, recursive: true })
          }))
        }))
      })
    }

    await arb.reify(opts)

    const ignoreScripts = this.npm.config.get('ignore-scripts')
    // run the same set of scripts that `npm install` runs.
    if (!ignoreScripts) {
      const scripts = [
        'preinstall',
        'install',
        'postinstall',
        'prepublish', // XXX should we remove this finally??
        'preprepare',
        'prepare',
        'postprepare',
      ]
      const scriptShell = this.npm.config.get('script-shell') || undefined
      for (const event of scripts) {
        await runScript({
          path: where,
          args: [],
          scriptShell,
          stdio: 'inherit',
          event,
        })
      }
    }
    await reifyFinish(this.npm, arb)
  }
}

module.exports = CI
