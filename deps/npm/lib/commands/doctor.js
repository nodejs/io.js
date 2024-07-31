const cacache = require('cacache')
const { access, lstat, readdir, constants: { R_OK, W_OK, X_OK } } = require('node:fs/promises')
const fetch = require('make-fetch-happen')
const which = require('which')
const pacote = require('pacote')
const { resolve } = require('node:path')
const semver = require('semver')
const { log, output } = require('proc-log')
const ping = require('../utils/ping.js')
const { defaults } = require('@npmcli/config/lib/definitions')
const BaseCommand = require('../base-cmd.js')

const maskLabel = mask => {
  const label = []
  if (mask & R_OK) {
    label.push('readable')
  }

  if (mask & W_OK) {
    label.push('writable')
  }

  if (mask & X_OK) {
    label.push('executable')
  }

  return label.join(', ')
}

const subcommands = [
  {
    // Ping is left in as a legacy command but is listed as "connection" to
    // make more sense to more people
    groups: ['connection', 'ping', 'registry'],
    title: 'Connecting to the registry',
    cmd: 'checkPing',
  }, {
    groups: ['versions'],
    title: 'Checking npm version',
    cmd: 'getLatestNpmVersion',
  }, {
    groups: ['versions'],
    title: 'Checking node version',
    cmd: 'getLatestNodejsVersion',
  }, {
    groups: ['registry'],
    title: 'Checking configured npm registry',
    cmd: 'checkNpmRegistry',
  }, {
    groups: ['environment'],
    title: 'Checking for git executable in PATH',
    cmd: 'getGitPath',
  }, {
    groups: ['environment'],
    title: 'Checking for global bin folder in PATH',
    cmd: 'getBinPath',
  }, {
    groups: ['permissions', 'cache'],
    title: 'Checking permissions on cached files (this may take awhile)',
    cmd: 'checkCachePermission',
    windows: false,
  }, {
    groups: ['permissions'],
    title: 'Checking permissions on local node_modules (this may take awhile)',
    cmd: 'checkLocalModulesPermission',
    windows: false,
  }, {
    groups: ['permissions'],
    title: 'Checking permissions on global node_modules (this may take awhile)',
    cmd: 'checkGlobalModulesPermission',
    windows: false,
  }, {
    groups: ['permissions'],
    title: 'Checking permissions on local bin folder',
    cmd: 'checkLocalBinPermission',
    windows: false,
  }, {
    groups: ['permissions'],
    title: 'Checking permissions on global bin folder',
    cmd: 'checkGlobalBinPermission',
    windows: false,
  }, {
    groups: ['cache'],
    title: 'Verifying cache contents (this may take awhile)',
    cmd: 'verifyCachedFiles',
    windows: false,
  },
  // TODO:
  // group === 'dependencies'?
  //   - ensure arborist.loadActual() runs without errors and no invalid edges
  //   - ensure package-lock.json matches loadActual()
  //   - verify loadActual without hidden lock file matches hidden lockfile
  // group === '???'
  //   - verify all local packages have bins linked
  // What is the fix for these?
]

class Doctor extends BaseCommand {
  static description = 'Check the health of your npm environment'
  static name = 'doctor'
  static params = ['registry']
  static ignoreImplicitWorkspace = false
  static usage = [`[${subcommands.flatMap(s => s.groups)
    .filter((value, index, self) => self.indexOf(value) === index && value !== 'ping')
    .join('] [')}]`]

  static subcommands = subcommands

  async exec (args) {
    log.info('doctor', 'Running checkup')
    let allOk = true

    const actions = this.actions(args)

    const chalk = this.npm.chalk
    for (const { title, cmd } of actions) {
      this.output(title)
      // TODO when we have an in progress indicator that could go here
      let result
      try {
        result = await this[cmd]()
        this.output(`${chalk.green('Ok')}${result ? `\n${result}` : ''}\n`)
      } catch (err) {
        allOk = false
        this.output(`${chalk.red('Not ok')}\n${chalk.cyan(err)}\n`)
      }
    }

    if (!allOk) {
      if (this.npm.silent) {
        /* eslint-disable-next-line max-len */
        throw new Error('Some problems found. Check logs or disable silent mode for recommendations.')
      } else {
        throw new Error('Some problems found. See above for recommendations.')
      }
    }
  }

  async checkPing () {
    log.info('doctor', 'Pinging registry')
    try {
      await ping({ ...this.npm.flatOptions, retry: false })
      return ''
    } catch (er) {
      if (/^E\d{3}$/.test(er.code || '')) {
        throw er.code.slice(1) + ' ' + er.message
      } else {
        throw er.message
      }
    }
  }

  async getLatestNpmVersion () {
    log.info('doctor', 'Getting npm package information')
    const latest = (await pacote.manifest('npm@latest', this.npm.flatOptions)).version
    if (semver.gte(this.npm.version, latest)) {
      return `current: v${this.npm.version}, latest: v${latest}`
    } else {
      throw `Use npm v${latest}`
    }
  }

  async getLatestNodejsVersion () {
    // XXX get the latest in the current major as well
    const current = process.version
    const currentRange = `^${current}`
    const url = 'https://nodejs.org/dist/index.json'
    log.info('doctor', 'Getting Node.js release information')
    const res = await fetch(url, { method: 'GET', ...this.npm.flatOptions })
    const data = await res.json()
    let maxCurrent = '0.0.0'
    let maxLTS = '0.0.0'
    for (const { lts, version } of data) {
      if (lts && semver.gt(version, maxLTS)) {
        maxLTS = version
      }

      if (semver.satisfies(version, currentRange) && semver.gt(version, maxCurrent)) {
        maxCurrent = version
      }
    }
    const recommended = semver.gt(maxCurrent, maxLTS) ? maxCurrent : maxLTS
    if (semver.gte(process.version, recommended)) {
      return `current: ${current}, recommended: ${recommended}`
    } else {
      throw `Use node ${recommended} (current: ${current})`
    }
  }

  async getBinPath () {
    log.info('doctor', 'getBinPath', 'Finding npm global bin in your PATH')
    if (!process.env.PATH.includes(this.npm.globalBin)) {
      throw new Error(`Add ${this.npm.globalBin} to your $PATH`)
    }
    return this.npm.globalBin
  }

  async checkCachePermission () {
    return this.checkFilesPermission(this.npm.cache, true, R_OK)
  }

  async checkLocalModulesPermission () {
    return this.checkFilesPermission(this.npm.localDir, true, R_OK | W_OK, true)
  }

  async checkGlobalModulesPermission () {
    return this.checkFilesPermission(this.npm.globalDir, false, R_OK)
  }

  async checkLocalBinPermission () {
    return this.checkFilesPermission(this.npm.localBin, false, R_OK | W_OK | X_OK, true)
  }

  async checkGlobalBinPermission () {
    return this.checkFilesPermission(this.npm.globalBin, false, X_OK)
  }

  async checkFilesPermission (root, shouldOwn, mask, missingOk) {
    let ok = true

    try {
      const uid = process.getuid()
      const gid = process.getgid()
      const files = new Set([root])
      for (const f of files) {
        const st = await lstat(f).catch(er => {
          // if it can't be missing, or if it can and the error wasn't that it was missing
          if (!missingOk || er.code !== 'ENOENT') {
            ok = false
            log.warn('doctor', 'checkFilesPermission', 'error getting info for ' + f)
          }
        })

        if (!st) {
          continue
        }

        if (shouldOwn && (uid !== st.uid || gid !== st.gid)) {
          log.warn('doctor', 'checkFilesPermission', 'should be owner of ' + f)
          ok = false
        }

        if (!st.isDirectory() && !st.isFile()) {
          continue
        }

        try {
          await access(f, mask)
        } catch (er) {
          ok = false
          const msg = `Missing permissions on ${f} (expect: ${maskLabel(mask)})`
          log.error('doctor', 'checkFilesPermission', msg)
          continue
        }

        if (st.isDirectory()) {
          const entries = await readdir(f).catch(() => {
            ok = false
            log.warn('doctor', 'checkFilesPermission', 'error reading directory ' + f)
            return []
          })
          for (const entry of entries) {
            files.add(resolve(f, entry))
          }
        }
      }
    } finally {
      if (!ok) {
        throw (
          `Check the permissions of files in ${root}` +
          (shouldOwn ? ' (should be owned by current user)' : '')
        )
      } else {
        return ''
      }
    }
  }

  async getGitPath () {
    log.info('doctor', 'Finding git in your PATH')
    return await which('git').catch(er => {
      log.warn('doctor', 'getGitPath', er)
      throw new Error("Install git and ensure it's in your PATH.")
    })
  }

  async verifyCachedFiles () {
    log.info('doctor', 'verifyCachedFiles', 'Verifying the npm cache')

    const stats = await cacache.verify(this.npm.flatOptions.cache)
    const { badContentCount, reclaimedCount, missingContent, reclaimedSize } = stats
    if (badContentCount || reclaimedCount || missingContent) {
      if (badContentCount) {
        log.warn('doctor', 'verifyCachedFiles', `Corrupted content removed: ${badContentCount}`)
      }

      if (reclaimedCount) {
        log.warn(
          'doctor',
          'verifyCachedFiles',
          `Content garbage-collected: ${reclaimedCount} (${reclaimedSize} bytes)`
        )
      }

      if (missingContent) {
        log.warn('doctor', 'verifyCachedFiles', `Missing content: ${missingContent}`)
      }

      log.warn('doctor', 'verifyCachedFiles', 'Cache issues have been fixed')
    }
    log.info(
      'doctor',
      'verifyCachedFiles',
        `Verification complete. Stats: ${JSON.stringify(stats, null, 2)}`
    )
    return `verified ${stats.verifiedContent} tarballs`
  }

  async checkNpmRegistry () {
    if (this.npm.flatOptions.registry !== defaults.registry) {
      throw `Try \`npm config set registry=${defaults.registry}\``
    } else {
      return `using default registry (${defaults.registry})`
    }
  }

  output (...args) {
    // TODO display layer should do this
    if (!this.npm.silent) {
      output.standard(...args)
    }
  }

  actions (params) {
    return this.constructor.subcommands.filter(subcmd => {
      if (process.platform === 'win32' && subcmd.windows === false) {
        return false
      }
      if (params.length) {
        return params.some(param => subcmd.groups.includes(param))
      }
      return true
    })
  }
}

module.exports = Doctor
