const libnpmaccess = require('libnpmaccess')
const npa = require('npm-package-arg')
const { output } = require('proc-log')
const pkgJson = require('@npmcli/package-json')
const localeCompare = require('@isaacs/string-locale-compare')('en')
const { otplease } = require('../utils/auth.js')
const getIdentity = require('../utils/get-identity.js')
const BaseCommand = require('../base-cmd.js')

const commands = [
  'get',
  'grant',
  'list',
  'revoke',
  'set',
]

const setCommands = [
  'status=public',
  'status=private',
  'mfa=none',
  'mfa=publish',
  'mfa=automation',
  '2fa=none',
  '2fa=publish',
  '2fa=automation',
]

class Access extends BaseCommand {
  static description = 'Set access level on published packages'
  static name = 'access'
  static params = [
    'json',
    'otp',
    'registry',
  ]

  static usage = [
    'list packages [<user>|<scope>|<scope:team>] [<package>]',
    'list collaborators [<package> [<user>]]',
    'get status [<package>]',
    'set status=public|private [<package>]',
    'set mfa=none|publish|automation [<package>]',
    'grant <read-only|read-write> <scope:team> [<package>]',
    'revoke <scope:team> [<package>]',
  ]

  static async completion (opts) {
    const argv = opts.conf.argv.remain
    if (argv.length === 2) {
      return commands
    }

    if (argv.length === 3) {
      switch (argv[2]) {
        case 'grant':
          return ['read-only', 'read-write']
        case 'revoke':
          return []
        case 'list':
        case 'ls':
          return ['packages', 'collaborators']
        case 'get':
          return ['status']
        case 'set':
          return setCommands
        default:
          throw new Error(argv[2] + ' not recognized')
      }
    }
  }

  async exec ([cmd, subcmd, ...args]) {
    if (!cmd) {
      throw this.usageError()
    }
    if (!commands.includes(cmd)) {
      throw this.usageError(`${cmd} is not a valid access command`)
    }
    // All commands take at least one more parameter so we can do this check up front
    if (!subcmd) {
      throw this.usageError()
    }

    switch (cmd) {
      case 'grant':
        if (!['read-only', 'read-write'].includes(subcmd)) {
          throw this.usageError('grant must be either `read-only` or `read-write`')
        }
        if (!args[0]) {
          throw this.usageError('`<scope:team>` argument is required')
        }
        return this.#grant(subcmd, args[0], args[1])
      case 'revoke':
        return this.#revoke(subcmd, args[0])
      case 'list':
      case 'ls':
        if (subcmd === 'packages') {
          return this.#listPackages(args[0], args[1])
        }
        if (subcmd === 'collaborators') {
          return this.#listCollaborators(args[0], args[1])
        }
        throw this.usageError(`list ${subcmd} is not a valid access command`)
      case 'get':
        if (subcmd !== 'status') {
          throw this.usageError(`get ${subcmd} is not a valid access command`)
        }
        return this.#getStatus(args[0])
      case 'set':
        if (!setCommands.includes(subcmd)) {
          throw this.usageError(`set ${subcmd} is not a valid access command`)
        }
        return this.#set(subcmd, args[0])
    }
  }

  async #grant (permissions, scope, pkg) {
    await libnpmaccess.setPermissions(scope, pkg, permissions, this.npm.flatOptions)
  }

  async #revoke (scope, pkg) {
    await libnpmaccess.removePermissions(scope, pkg, this.npm.flatOptions)
  }

  async #listPackages (owner, pkg) {
    if (!owner) {
      owner = await getIdentity(this.npm, this.npm.flatOptions)
    }
    const pkgs = await libnpmaccess.getPackages(owner, this.npm.flatOptions)
    this.#output(pkgs, pkg)
  }

  async #listCollaborators (pkg, user) {
    const pkgName = await this.#getPackage(pkg, false)
    const collabs = await libnpmaccess.getCollaborators(pkgName, this.npm.flatOptions)
    this.#output(collabs, user)
  }

  async #getStatus (pkg) {
    const pkgName = await this.#getPackage(pkg, false)
    const visibility = await libnpmaccess.getVisibility(pkgName, this.npm.flatOptions)
    this.#output({ [pkgName]: visibility.public ? 'public' : 'private' })
  }

  async #set (subcmd, pkg) {
    const [subkey, subval] = subcmd.split('=')
    switch (subkey) {
      case 'mfa':
      case '2fa':
        return this.#setMfa(pkg, subval)
      case 'status':
        return this.#setStatus(pkg, subval)
    }
  }

  async #setMfa (pkg, level) {
    const pkgName = await this.#getPackage(pkg, false)
    await otplease(this.npm, this.npm.flatOptions, (opts) => {
      return libnpmaccess.setMfa(pkgName, level, opts)
    })
  }

  async #setStatus (pkg, status) {
    // only scoped packages can have their access changed
    const pkgName = await this.#getPackage(pkg, true)
    if (status === 'private') {
      status = 'restricted'
    }
    await otplease(this.npm, this.npm.flatOptions, (opts) => {
      return libnpmaccess.setAccess(pkgName, status, opts)
    })
    return this.#getStatus(pkgName)
  }

  async #getPackage (name, requireScope) {
    if (!name) {
      try {
        const { content } = await pkgJson.normalize(this.npm.prefix)
        name = content.name
      } catch (err) {
        if (err.code === 'ENOENT') {
          throw Object.assign(new Error('no package name given and no package.json found'), {
            code: 'ENOENT',
          })
        } else {
          throw err
        }
      }
    }

    const spec = npa(name)
    if (requireScope && !spec.scope) {
      throw this.usageError('This command is only available for scoped packages.')
    }
    return name
  }

  #output (items, limiter) {
    const outputs = {}
    const lookup = {
      __proto__: null,
      read: 'read-only',
      write: 'read-write',
    }
    for (const item in items) {
      const val = items[item]
      outputs[item] = lookup[val] || val
    }
    if (this.npm.config.get('json')) {
      output.buffer(outputs)
    } else {
      for (const item of Object.keys(outputs).sort(localeCompare)) {
        if (!limiter || limiter === item) {
          output.standard(`${item}: ${outputs[item]}`)
        }
      }
    }
  }
}

module.exports = Access
