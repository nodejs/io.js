const { readdir } = require('node:fs/promises')
const { resolve, join } = require('node:path')
const { log } = require('proc-log')
const runScript = require('@npmcli/run-script')
const pacote = require('pacote')
const checks = require('npm-install-checks')
const reifyFinish = require('../utils/reify-finish.js')
const ArboristWorkspaceCmd = require('../arborist-cmd.js')

class Install extends ArboristWorkspaceCmd {
  static description = 'Install a package'
  static name = 'install'

  // These are in the order they will show up in when running "-h"
  // If adding to this list, consider adding also to ci.js
  static params = [
    'save',
    'save-exact',
    'global',
    'install-strategy',
    'legacy-bundling',
    'global-style',
    'omit',
    'include',
    'strict-peer-deps',
    'prefer-dedupe',
    'package-lock',
    'package-lock-only',
    'foreground-scripts',
    'ignore-scripts',
    'audit',
    'bin-links',
    'fund',
    'dry-run',
    'cpu',
    'os',
    'libc',
    ...super.params,
  ]

  static usage = ['[<package-spec> ...]']

  static async completion (opts) {
    const { partialWord } = opts
    // install can complete to a folder with a package.json, or any package.
    // if it has a slash, then it's gotta be a folder
    // if it starts with https?://, then just give up, because it's a url
    if (/^https?:\/\//.test(partialWord)) {
      // do not complete to URLs
      return []
    }

    if (/\//.test(partialWord)) {
      // Complete fully to folder if there is exactly one match and it
      // is a folder containing a package.json file.  If that is not the
      // case we return 0 matches, which will trigger the default bash
      // complete.
      const lastSlashIdx = partialWord.lastIndexOf('/')
      const partialName = partialWord.slice(lastSlashIdx + 1)
      const partialPath = partialWord.slice(0, lastSlashIdx) || '/'

      const isDirMatch = async sibling => {
        if (sibling.slice(0, partialName.length) !== partialName) {
          return false
        }

        try {
          const contents = await readdir(join(partialPath, sibling))
          const result = (contents.indexOf('package.json') !== -1)
          return result
        } catch {
          return false
        }
      }

      try {
        const siblings = await readdir(partialPath)
        const matches = []
        for (const sibling of siblings) {
          if (await isDirMatch(sibling)) {
            matches.push(sibling)
          }
        }
        if (matches.length === 1) {
          return [join(partialPath, matches[0])]
        }
        // no matches
        return []
      } catch {
        return [] // invalid dir: no matching
      }
    }
    // Note: there used to be registry completion here,
    // but it stopped making sense somewhere around
    // 50,000 packages on the registry
  }

  async exec (args) {
    // the /path/to/node_modules/..
    const globalTop = resolve(this.npm.globalDir, '..')
    const ignoreScripts = this.npm.config.get('ignore-scripts')
    const isGlobalInstall = this.npm.global
    const where = isGlobalInstall ? globalTop : this.npm.prefix
    const forced = this.npm.config.get('force')
    const scriptShell = this.npm.config.get('script-shell') || undefined

    // be very strict about engines when trying to update npm itself
    const npmInstall = args.find(arg => arg.startsWith('npm@') || arg === 'npm')
    if (isGlobalInstall && npmInstall) {
      const npmOptions = this.npm.flatOptions
      const npmManifest = await pacote.manifest(npmInstall, npmOptions)
      try {
        checks.checkEngine(npmManifest, npmManifest.version, process.version)
      } catch (e) {
        if (forced) {
          log.warn(
            'install',
            `Forcing global npm install with incompatible version ${npmManifest.version} into node ${process.version}`
          )
        } else {
          throw e
        }
      }
    }

    // don't try to install the prefix into itself
    args = args.filter(a => resolve(a) !== this.npm.prefix)

    // `npm i -g` => "install this package globally"
    if (where === globalTop && !args.length) {
      args = ['.']
    }

    // throw usage error if trying to install empty package
    // name to global space, e.g: `npm i -g ""`
    if (where === globalTop && !args.every(Boolean)) {
      throw this.usageError()
    }

    const Arborist = require('@npmcli/arborist')
    const opts = {
      ...this.npm.flatOptions,
      auditLevel: null,
      path: where,
      add: args,
      workspaces: this.workspaceNames,
    }
    const arb = new Arborist(opts)
    await arb.reify(opts)

    if (!args.length && !isGlobalInstall && !ignoreScripts) {
      const scripts = [
        'preinstall',
        'install',
        'postinstall',
        'prepublish', // XXX(npm9) should we remove this finally??
        'preprepare',
        'prepare',
        'postprepare',
      ]
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

module.exports = Install
