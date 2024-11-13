const { log } = require('proc-log')

class BaseCommand {
  // these defaults can be overridden by individual commands
  static workspaces = false
  static ignoreImplicitWorkspace = true
  static checkDevEngines = false

  // these should always be overridden by individual commands
  static name = null
  static description = null
  static params = null

  // this is a static so that we can read from it without instantiating a command
  // which would require loading the config
  static get describeUsage () {
    const { definitions } = require('@npmcli/config/lib/definitions')
    const { aliases: cmdAliases } = require('./utils/cmd-list')
    const seenExclusive = new Set()
    const wrapWidth = 80
    const { description, usage = [''], name, params } = this

    const fullUsage = [
      `${description}`,
      '',
      'Usage:',
      ...usage.map(u => `npm ${name} ${u}`.trim()),
    ]

    if (params) {
      let results = ''
      let line = ''
      for (const param of params) {
        /* istanbul ignore next */
        if (seenExclusive.has(param)) {
          continue
        }
        const { exclusive } = definitions[param]
        let paramUsage = `${definitions[param].usage}`
        if (exclusive) {
          const exclusiveParams = [paramUsage]
          seenExclusive.add(param)
          for (const e of exclusive) {
            seenExclusive.add(e)
            exclusiveParams.push(definitions[e].usage)
          }
          paramUsage = `${exclusiveParams.join('|')}`
        }
        paramUsage = `[${paramUsage}]`
        if (line.length + paramUsage.length > wrapWidth) {
          results = [results, line].filter(Boolean).join('\n')
          line = ''
        }
        line = [line, paramUsage].filter(Boolean).join(' ')
      }
      fullUsage.push('')
      fullUsage.push('Options:')
      fullUsage.push([results, line].filter(Boolean).join('\n'))
    }

    const aliases = Object.entries(cmdAliases).reduce((p, [k, v]) => {
      return p.concat(v === name ? k : [])
    }, [])

    if (aliases.length) {
      const plural = aliases.length === 1 ? '' : 'es'
      fullUsage.push('')
      fullUsage.push(`alias${plural}: ${aliases.join(', ')}`)
    }

    fullUsage.push('')
    fullUsage.push(`Run "npm help ${name}" for more info`)

    return fullUsage.join('\n')
  }

  constructor (npm) {
    this.npm = npm

    const { config } = this.npm

    if (!this.constructor.skipConfigValidation) {
      config.validate()
    }

    if (config.get('workspaces') === false && config.get('workspace').length) {
      throw new Error('Can not use --no-workspaces and --workspace at the same time')
    }
  }

  get name () {
    return this.constructor.name
  }

  get description () {
    return this.constructor.description
  }

  get params () {
    return this.constructor.params
  }

  get usage () {
    return this.constructor.describeUsage
  }

  usageError (prefix = '') {
    if (prefix) {
      prefix += '\n\n'
    }
    return Object.assign(new Error(`\n${prefix}${this.usage}`), {
      code: 'EUSAGE',
    })
  }

  // Compare the number of entries with what was expected
  checkExpected (entries) {
    if (!this.npm.config.isDefault('expect-results')) {
      const expected = this.npm.config.get('expect-results')
      if (!!entries !== !!expected) {
        log.warn(this.name, `Expected ${expected ? '' : 'no '}results, got ${entries}`)
        process.exitCode = 1
      }
    } else if (!this.npm.config.isDefault('expect-result-count')) {
      const expected = this.npm.config.get('expect-result-count')
      if (expected !== entries) {
        /* eslint-disable-next-line max-len */
        log.warn(this.name, `Expected ${expected} result${expected === 1 ? '' : 's'}, got ${entries}`)
        process.exitCode = 1
      }
    }
  }

  // Checks the devEngines entry in the package.json at this.localPrefix
  async checkDevEngines () {
    const force = this.npm.flatOptions.force

    const { devEngines } = await require('@npmcli/package-json')
      .normalize(this.npm.config.localPrefix)
      .then(p => p.content)
      .catch(() => ({}))

    if (typeof devEngines === 'undefined') {
      return
    }

    const { checkDevEngines, currentEnv } = require('npm-install-checks')
    const current = currentEnv.devEngines({
      nodeVersion: this.npm.nodeVersion,
      npmVersion: this.npm.version,
    })

    const failures = checkDevEngines(devEngines, current)
    const warnings = failures.filter(f => f.isWarn)
    const errors = failures.filter(f => f.isError)

    const genMsg = (failure, i = 0) => {
      return [...new Set([
        // eslint-disable-next-line
        i === 0 ? 'The developer of this package has specified the following through devEngines' : '',
        `${failure.message}`,
        `${failure.errors.map(e => e.message).join('\n')}`,
      ])].filter(v => v).join('\n')
    }

    [...warnings, ...(force ? errors : [])].forEach((failure, i) => {
      const message = genMsg(failure, i)
      log.warn('EBADDEVENGINES', message)
      log.warn('EBADDEVENGINES', {
        current: failure.current,
        required: failure.required,
      })
    })

    if (force) {
      return
    }

    if (errors.length) {
      const failure = errors[0]
      const message = genMsg(failure)
      throw Object.assign(new Error(message), {
        engine: failure.engine,
        code: 'EBADDEVENGINES',
        current: failure.current,
        required: failure.required,
      })
    }
  }

  async setWorkspaces () {
    const { relative } = require('node:path')

    const includeWorkspaceRoot = this.isArboristCmd
      ? false
      : this.npm.config.get('include-workspace-root')

    const prefixInsideCwd = relative(this.npm.localPrefix, process.cwd()).startsWith('..')
    const relativeFrom = prefixInsideCwd ? this.npm.localPrefix : process.cwd()

    const filters = this.npm.config.get('workspace')
    const getWorkspaces = require('./utils/get-workspaces.js')
    const ws = await getWorkspaces(filters, {
      path: this.npm.localPrefix,
      includeWorkspaceRoot,
      relativeFrom,
    })

    this.workspaces = ws
    this.workspaceNames = [...ws.keys()]
    this.workspacePaths = [...ws.values()]
  }
}

module.exports = BaseCommand
