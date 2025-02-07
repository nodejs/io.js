const liborg = require('libnpmorg')
const { otplease } = require('../utils/auth.js')
const BaseCommand = require('../base-cmd.js')
const { output } = require('proc-log')

class Org extends BaseCommand {
  static description = 'Manage orgs'
  static name = 'org'
  static usage = [
    'set orgname username [developer | admin | owner]',
    'rm orgname username',
    'ls orgname [<username>]',
  ]

  static params = ['registry', 'otp', 'json', 'parseable']

  static async completion (opts) {
    const argv = opts.conf.argv.remain
    if (argv.length === 2) {
      return ['set', 'rm', 'ls']
    }

    switch (argv[2]) {
      case 'ls':
      case 'add':
      case 'rm':
      case 'set':
        return []
      default:
        throw new Error(argv[2] + ' not recognized')
    }
  }

  async exec ([cmd, orgname, username, role]) {
    return otplease(this.npm, {
      ...this.npm.flatOptions,
    }, opts => {
      switch (cmd) {
        case 'add':
        case 'set':
          return this.set(orgname, username, role, opts)
        case 'rm':
          return this.rm(orgname, username, opts)
        case 'ls':
          return this.ls(orgname, username, opts)
        default:
          throw this.usageError()
      }
    })
  }

  async set (org, user, role, opts) {
    role = role || 'developer'
    if (!org) {
      throw new Error('First argument `orgname` is required.')
    }

    if (!user) {
      throw new Error('Second argument `username` is required.')
    }

    if (!['owner', 'admin', 'developer'].find(x => x === role)) {
      throw new Error(
        'Third argument `role` must be one of `owner`, `admin`, or `developer`, with `developer` being the default value if omitted.'
      )
    }

    const memDeets = await liborg.set(org, user, role, opts)
    if (opts.json) {
      output.standard(JSON.stringify(memDeets, null, 2))
    } else if (opts.parseable) {
      output.standard(['org', 'orgsize', 'user', 'role'].join('\t'))
      output.standard(
        [memDeets.org.name, memDeets.org.size, memDeets.user, memDeets.role].join('\t')
      )
    } else if (!this.npm.silent) {
      output.standard(
        `Added ${memDeets.user} as ${memDeets.role} to ${memDeets.org.name}. You now have ${
            memDeets.org.size
          } member${memDeets.org.size === 1 ? '' : 's'} in this org.`
      )
    }

    return memDeets
  }

  async rm (org, user, opts) {
    if (!org) {
      throw new Error('First argument `orgname` is required.')
    }

    if (!user) {
      throw new Error('Second argument `username` is required.')
    }

    await liborg.rm(org, user, opts)
    const roster = await liborg.ls(org, opts)
    user = user.replace(/^[~@]?/, '')
    org = org.replace(/^[~@]?/, '')
    const userCount = Object.keys(roster).length
    if (opts.json) {
      output.buffer({
        user,
        org,
        userCount,
        deleted: true,
      })
    } else if (opts.parseable) {
      output.standard(['user', 'org', 'userCount', 'deleted'].join('\t'))
      output.standard([user, org, userCount, true].join('\t'))
    } else if (!this.npm.silent) {
      output.standard(
        `Successfully removed ${user} from ${org}. You now have ${userCount} member${
          userCount === 1 ? '' : 's'
        } in this org.`
      )
    }
  }

  async ls (org, user, opts) {
    if (!org) {
      throw new Error('First argument `orgname` is required.')
    }

    let roster = await liborg.ls(org, opts)
    if (user) {
      const newRoster = {}
      if (roster[user]) {
        newRoster[user] = roster[user]
      }

      roster = newRoster
    }
    if (opts.json) {
      output.buffer(roster)
    } else if (opts.parseable) {
      output.standard(['user', 'role'].join('\t'))
      Object.keys(roster).forEach(u => {
        output.standard([u, roster[u]].join('\t'))
      })
    } else if (!this.npm.silent) {
      const chalk = this.npm.chalk
      for (const u of Object.keys(roster).sort()) {
        output.standard(`${u} - ${chalk.cyan(roster[u])}`)
      }
    }
  }
}

module.exports = Org
