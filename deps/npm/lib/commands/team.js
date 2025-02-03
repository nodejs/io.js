const columns = require('cli-columns')
const libteam = require('libnpmteam')
const { output } = require('proc-log')
const { otplease } = require('../utils/auth.js')

const BaseCommand = require('../base-cmd.js')
class Team extends BaseCommand {
  static description = 'Manage organization teams and team memberships'
  static name = 'team'
  static usage = [
    'create <scope:team> [--otp <otpcode>]',
    'destroy <scope:team> [--otp <otpcode>]',
    'add <scope:team> <user> [--otp <otpcode>]',
    'rm <scope:team> <user> [--otp <otpcode>]',
    'ls <scope>|<scope:team>',
  ]

  static params = [
    'registry',
    'otp',
    'parseable',
    'json',
  ]

  static ignoreImplicitWorkspace = false

  static async completion (opts) {
    const { conf: { argv: { remain: argv } } } = opts
    const subcommands = ['create', 'destroy', 'add', 'rm', 'ls']

    if (argv.length === 2) {
      return subcommands
    }

    if (subcommands.includes(argv[2])) {
      return []
    }

    throw new Error(argv[2] + ' not recognized')
  }

  async exec ([cmd, entity = '', user = '']) {
    // Entities are in the format <scope>:<team>
    // XXX: "description" option to libnpmteam is used as a description of the
    // team, but in npm's options, this is a boolean meaning "show the
    // description in npm search output".  Hence its being set to null here.
    await otplease(this.npm, { ...this.npm.flatOptions }, opts => {
      entity = entity.replace(/^@/, '')
      switch (cmd) {
        case 'create': return this.create(entity, opts)
        case 'destroy': return this.destroy(entity, opts)
        case 'add': return this.add(entity, user, opts)
        case 'rm': return this.rm(entity, user, opts)
        case 'ls': {
          const match = entity.match(/[^:]+:.+/)
          if (match) {
            return this.listUsers(entity, opts)
          } else {
            return this.listTeams(entity, opts)
          }
        }
        default:
          throw this.usageError()
      }
    })
  }

  async create (entity, opts) {
    await libteam.create(entity, opts)
    if (opts.json) {
      output.buffer({
        created: true,
        team: entity,
      })
    } else if (opts.parseable) {
      output.standard(`${entity}\tcreated`)
    } else if (!this.npm.silent) {
      output.standard(`+@${entity}`)
    }
  }

  async destroy (entity, opts) {
    await libteam.destroy(entity, opts)
    if (opts.json) {
      output.buffer({
        deleted: true,
        team: entity,
      })
    } else if (opts.parseable) {
      output.standard(`${entity}\tdeleted`)
    } else if (!this.npm.silent) {
      output.standard(`-@${entity}`)
    }
  }

  async add (entity, user, opts) {
    await libteam.add(user, entity, opts)
    if (opts.json) {
      output.buffer({
        added: true,
        team: entity,
        user,
      })
    } else if (opts.parseable) {
      output.standard(`${user}\t${entity}\tadded`)
    } else if (!this.npm.silent) {
      output.standard(`${user} added to @${entity}`)
    }
  }

  async rm (entity, user, opts) {
    await libteam.rm(user, entity, opts)
    if (opts.json) {
      output.buffer({
        removed: true,
        team: entity,
        user,
      })
    } else if (opts.parseable) {
      output.standard(`${user}\t${entity}\tremoved`)
    } else if (!this.npm.silent) {
      output.standard(`${user} removed from @${entity}`)
    }
  }

  async listUsers (entity, opts) {
    const users = (await libteam.lsUsers(entity, opts)).sort()
    if (opts.json) {
      output.buffer(users)
    } else if (opts.parseable) {
      output.standard(users.join('\n'))
    } else if (!this.npm.silent) {
      const plural = users.length === 1 ? '' : 's'
      const more = users.length === 0 ? '' : ':\n'
      output.standard(`\n@${entity} has ${users.length} user${plural}${more}`)
      output.standard(columns(users, { padding: 1 }))
    }
  }

  async listTeams (entity, opts) {
    const teams = (await libteam.lsTeams(entity, opts)).sort()
    if (opts.json) {
      output.buffer(teams)
    } else if (opts.parseable) {
      output.standard(teams.join('\n'))
    } else if (!this.npm.silent) {
      const plural = teams.length === 1 ? '' : 's'
      const more = teams.length === 0 ? '' : ':\n'
      output.standard(`\n@${entity} has ${teams.length} team${plural}${more}`)
      output.standard(columns(teams.map(t => `@${t}`), { padding: 1 }))
    }
  }
}

module.exports = Team
