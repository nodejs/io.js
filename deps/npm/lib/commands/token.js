const { log, output } = require('proc-log')
const { listTokens, createToken, removeToken } = require('npm-profile')
const { otplease } = require('../utils/auth.js')
const readUserInfo = require('../utils/read-user-info.js')
const BaseCommand = require('../base-cmd.js')

class Token extends BaseCommand {
  static description = 'Manage your authentication tokens'
  static name = 'token'
  static usage = ['list', 'revoke <id|token>', 'create [--read-only] [--cidr=list]']
  static params = ['read-only', 'cidr', 'registry', 'otp']

  static async completion (opts) {
    const argv = opts.conf.argv.remain
    const subcommands = ['list', 'revoke', 'create']
    if (argv.length === 2) {
      return subcommands
    }

    if (subcommands.includes(argv[2])) {
      return []
    }

    throw new Error(argv[2] + ' not recognized')
  }

  async exec (args) {
    if (args.length === 0) {
      return this.list()
    }
    switch (args[0]) {
      case 'list':
      case 'ls':
        return this.list()
      case 'rm':
      case 'delete':
      case 'revoke':
      case 'remove':
        return this.rm(args.slice(1))
      case 'create':
        return this.create(args.slice(1))
      default:
        throw this.usageError(`${args[0]} is not a recognized subcommand.`)
    }
  }

  async list () {
    const json = this.npm.config.get('json')
    const parseable = this.npm.config.get('parseable')
    log.info('token', 'getting list')
    const tokens = await listTokens(this.npm.flatOptions)
    if (json) {
      output.buffer(tokens)
      return
    }
    if (parseable) {
      output.standard(['key', 'token', 'created', 'readonly', 'CIDR whitelist'].join('\t'))
      tokens.forEach(token => {
        output.standard(
          [
            token.key,
            token.token,
            token.created,
            token.readonly ? 'true' : 'false',
            token.cidr_whitelist ? token.cidr_whitelist.join(',') : '',
          ].join('\t')
        )
      })
      return
    }
    this.generateTokenIds(tokens, 6)
    const chalk = this.npm.chalk
    for (const token of tokens) {
      const level = token.readonly ? 'Read only token' : 'Publish token'
      const created = String(token.created).slice(0, 10)
      /* eslint-disable-next-line max-len */
      output.standard(`${chalk.blue(level)} ${token.token}… with id ${chalk.cyan(token.id)} created ${created}`)
      if (token.cidr_whitelist) {
        output.standard(`with IP whitelist: ${chalk.green(token.cidr_whitelist.join(','))}`)
      }
      output.standard()
    }
  }

  async rm (args) {
    if (args.length === 0) {
      throw this.usageError('`<tokenKey>` argument is required.')
    }

    const json = this.npm.config.get('json')
    const parseable = this.npm.config.get('parseable')
    const toRemove = []
    const opts = { ...this.npm.flatOptions }
    log.info('token', `removing ${toRemove.length} tokens`)
    const tokens = await listTokens(opts)
    args.forEach(id => {
      const matches = tokens.filter(token => token.key.indexOf(id) === 0)
      if (matches.length === 1) {
        toRemove.push(matches[0].key)
      } else if (matches.length > 1) {
        throw new Error(
          /* eslint-disable-next-line max-len */
          `Token ID "${id}" was ambiguous, a new token may have been created since you last ran \`npm token list\`.`
        )
      } else {
        const tokenMatches = tokens.some(t => id.indexOf(t.token) === 0)
        if (!tokenMatches) {
          throw new Error(`Unknown token id or value "${id}".`)
        }

        toRemove.push(id)
      }
    })
    await Promise.all(
      toRemove.map(key => {
        return otplease(this.npm, opts, c => removeToken(key, c))
      })
    )
    if (json) {
      output.buffer(toRemove)
    } else if (parseable) {
      output.standard(toRemove.join('\t'))
    } else {
      output.standard('Removed ' + toRemove.length + ' token' + (toRemove.length !== 1 ? 's' : ''))
    }
  }

  async create () {
    const json = this.npm.config.get('json')
    const parseable = this.npm.config.get('parseable')
    const cidr = this.npm.config.get('cidr')
    const readonly = this.npm.config.get('read-only')

    const validCIDR = await this.validateCIDRList(cidr)
    const password = await readUserInfo.password()
    log.info('token', 'creating')
    const result = await otplease(
      this.npm,
      { ...this.npm.flatOptions },
      c => createToken(password, readonly, validCIDR, c)
    )
    delete result.key
    delete result.updated
    if (json) {
      output.buffer(result)
    } else if (parseable) {
      Object.keys(result).forEach(k => output.standard(k + '\t' + result[k]))
    } else {
      const chalk = this.npm.chalk
      // Identical to list
      const level = result.readonly ? 'read only' : 'publish'
      output.standard(`Created ${chalk.blue(level)} token ${result.token}`)
      if (result.cidr_whitelist?.length) {
        output.standard(`with IP whitelist: ${chalk.green(result.cidr_whitelist.join(','))}`)
      }
    }
  }

  invalidCIDRError (msg) {
    return Object.assign(new Error(msg), { code: 'EINVALIDCIDR' })
  }

  generateTokenIds (tokens, minLength) {
    for (const token of tokens) {
      token.id = token.key
      for (let ii = minLength; ii < token.key.length; ++ii) {
        const match = tokens.some(
          ot => ot !== token && ot.key.slice(0, ii) === token.key.slice(0, ii)
        )
        if (!match) {
          token.id = token.key.slice(0, ii)
          break
        }
      }
    }
  }

  async validateCIDRList (cidrs) {
    const { v4: isCidrV4, v6: isCidrV6 } = await import('is-cidr')
    const maybeList = [].concat(cidrs).filter(Boolean)
    const list = maybeList.length === 1 ? maybeList[0].split(/,\s*/) : maybeList
    for (const cidr of list) {
      if (isCidrV6(cidr)) {
        throw this.invalidCIDRError(
          `CIDR whitelist can only contain IPv4 addresses${cidr} is IPv6`
        )
      }

      if (!isCidrV4(cidr)) {
        throw this.invalidCIDRError(`CIDR whitelist contains invalid CIDR entry: ${cidr}`)
      }
    }
    return list
  }
}

module.exports = Token
