const { inspect } = require('node:util')
const { URL } = require('node:url')
const { log, output } = require('proc-log')
const { get, set, createToken } = require('npm-profile')
const qrcodeTerminal = require('qrcode-terminal')
const { otplease } = require('../utils/auth.js')
const readUserInfo = require('../utils/read-user-info.js')
const BaseCommand = require('../base-cmd.js')

const qrcode = url =>
  new Promise((resolve) => qrcodeTerminal.generate(url, resolve))

const knownProfileKeys = [
  'name',
  'email',
  'two-factor auth',
  'fullname',
  'homepage',
  'freenode',
  'twitter',
  'github',
  'created',
  'updated',
]

const writableProfileKeys = [
  'email',
  'password',
  'fullname',
  'homepage',
  'freenode',
  'twitter',
  'github',
]

class Profile extends BaseCommand {
  static description = 'Change settings on your registry profile'
  static name = 'profile'
  static usage = [
    'enable-2fa [auth-only|auth-and-writes]',
    'disable-2fa',
    'get [<key>]',
    'set <key> <value>',
  ]

  static params = [
    'registry',
    'json',
    'parseable',
    'otp',
  ]

  static async completion (opts) {
    var argv = opts.conf.argv.remain

    if (!argv[2]) {
      return ['enable-2fa', 'disable-2fa', 'get', 'set']
    }

    switch (argv[2]) {
      case 'enable-2fa':
      case 'enable-tfa':
        return ['auth-and-writes', 'auth-only']

      case 'disable-2fa':
      case 'disable-tfa':
      case 'get':
      case 'set':
        return []
      default:
        throw new Error(argv[2] + ' not recognized')
    }
  }

  async exec (args) {
    if (args.length === 0) {
      throw this.usageError()
    }

    const [subcmd, ...opts] = args

    switch (subcmd) {
      case 'enable-2fa':
      case 'enable-tfa':
      case 'enable2fa':
      case 'enabletfa':
        return this.enable2fa(opts)
      case 'disable-2fa':
      case 'disable-tfa':
      case 'disable2fa':
      case 'disabletfa':
        return this.disable2fa()
      case 'get':
        return this.get(opts)
      case 'set':
        return this.set(opts)
      default:
        throw new Error('Unknown profile command: ' + subcmd)
    }
  }

  async get (args) {
    const tfa = 'two-factor auth'
    const info = await get({ ...this.npm.flatOptions })

    if (!info.cidr_whitelist) {
      delete info.cidr_whitelist
    }

    if (this.npm.config.get('json')) {
      output.buffer(info)
      return
    }

    // clean up and format key/values for output
    const cleaned = {}
    for (const key of knownProfileKeys) {
      cleaned[key] = info[key] || ''
    }

    const unknownProfileKeys = Object.keys(info).filter((k) => !(k in cleaned))
    for (const key of unknownProfileKeys) {
      cleaned[key] = info[key] || ''
    }

    delete cleaned.tfa
    delete cleaned.email_verified
    cleaned.email += info.email_verified ? ' (verified)' : '(unverified)'

    if (info.tfa && !info.tfa.pending) {
      cleaned[tfa] = info.tfa.mode
    } else {
      cleaned[tfa] = 'disabled'
    }

    if (args.length) {
      const values = args // comma or space separated
        .join(',')
        .split(/,/)
        .filter((arg) => arg.trim() !== '')
        .map((arg) => cleaned[arg])
        .join('\t')
      output.standard(values)
    } else {
      if (this.npm.config.get('parseable')) {
        for (const key of Object.keys(info)) {
          if (key === 'tfa') {
            output.standard(`${key}\t${cleaned[tfa]}`)
          } else {
            output.standard(`${key}\t${info[key]}`)
          }
        }
      } else {
        for (const [key, value] of Object.entries(cleaned)) {
          output.standard(`${key}: ${value}`)
        }
      }
    }
  }

  async set (args) {
    const conf = { ...this.npm.flatOptions }
    const prop = (args[0] || '').toLowerCase().trim()

    let value = args.length > 1 ? args.slice(1).join(' ') : null

    const readPasswords = async () => {
      const newpassword = await readUserInfo.password('New password: ')
      const confirmedpassword = await readUserInfo.password('       Again:     ')

      if (newpassword !== confirmedpassword) {
        log.warn('profile', 'Passwords do not match, please try again.')
        return readPasswords()
      }

      return newpassword
    }

    if (prop !== 'password' && value === null) {
      throw new Error('npm profile set <prop> <value>')
    }

    if (prop === 'password' && value !== null) {
      throw new Error(
        'npm profile set password\n' +
        'Do not include your current or new passwords on the command line.')
    }

    if (writableProfileKeys.indexOf(prop) === -1) {
      throw new Error(`"${prop}" is not a property we can set. ` +
        `Valid properties are: ` + writableProfileKeys.join(', '))
    }

    if (prop === 'password') {
      const current = await readUserInfo.password('Current password: ')
      const newpassword = await readPasswords()

      value = { old: current, new: newpassword }
    }

    // FIXME: Work around to not clear everything other than what we're setting
    const user = await get(conf)
    const newUser = {}

    for (const key of writableProfileKeys) {
      newUser[key] = user[key]
    }

    newUser[prop] = value

    const result = await otplease(this.npm, conf, c => set(newUser, c))

    if (this.npm.config.get('json')) {
      output.buffer({ [prop]: result[prop] })
    } else if (this.npm.config.get('parseable')) {
      output.standard(prop + '\t' + result[prop])
    } else if (result[prop] != null) {
      output.standard('Set', prop, 'to', result[prop])
    } else {
      output.standard('Set', prop)
    }
  }

  async enable2fa (args) {
    if (args.length > 1) {
      throw new Error('npm profile enable-2fa [auth-and-writes|auth-only]')
    }

    const mode = args[0] || 'auth-and-writes'
    if (mode !== 'auth-only' && mode !== 'auth-and-writes') {
      throw new Error(
        `Invalid two-factor authentication mode "${mode}".\n` +
        'Valid modes are:\n' +
        '  auth-only - Require two-factor authentication only when logging in\n' +
        '  auth-and-writes - Require two-factor authentication when logging in ' +
        'AND when publishing'
      )
    }

    if (this.npm.config.get('json') || this.npm.config.get('parseable')) {
      throw new Error(
        'Enabling two-factor authentication is an interactive operation and ' +
        (this.npm.config.get('json') ? 'JSON' : 'parseable') + ' output mode is not available'
      )
    }

    const info = {
      tfa: {
        mode: mode,
      },
    }

    // if they're using legacy auth currently then we have to
    // update them to a bearer token before continuing.
    const creds = this.npm.config.getCredentialsByURI(this.npm.config.get('registry'))
    const auth = {}

    if (creds.token) {
      auth.token = creds.token
    } else if (creds.username) {
      auth.basic = { username: creds.username, password: creds.password }
    } else if (creds.auth) {
      const basic = Buffer.from(creds.auth, 'base64').toString().split(':', 2)
      auth.basic = { username: basic[0], password: basic[1] }
    }

    if (!auth.basic && !auth.token) {
      throw new Error(
        'You need to be logged in to registry ' +
        `${this.npm.config.get('registry')} in order to enable 2fa`
      )
    }

    if (auth.basic) {
      log.info('profile', 'Updating authentication to bearer token')
      const result = await createToken(
        auth.basic.password, false, [], { ...this.npm.flatOptions }
      )

      if (!result.token) {
        throw new Error(
          `Your registry ${this.npm.config.get('registry')} does not seem to ` +
          'support bearer tokens. Bearer tokens are required for ' +
          'two-factor authentication'
        )
      }

      this.npm.config.setCredentialsByURI(
        this.npm.config.get('registry'),
        { token: result.token }
      )
      await this.npm.config.save('user')
    }

    log.notice('profile', 'Enabling two factor authentication for ' + mode)
    const password = await readUserInfo.password()
    info.tfa.password = password

    log.info('profile', 'Determine if tfa is pending')
    const userInfo = await get({ ...this.npm.flatOptions })

    const conf = { ...this.npm.flatOptions }
    if (userInfo && userInfo.tfa && userInfo.tfa.pending) {
      log.info('profile', 'Resetting two-factor authentication')
      await set({ tfa: { password, mode: 'disable' } }, conf)
    } else if (userInfo && userInfo.tfa) {
      if (!conf.otp) {
        conf.otp = await readUserInfo.otp(
          'Enter one-time password: '
        )
      }
    }

    log.info('profile', 'Setting two-factor authentication to ' + mode)
    const challenge = await set(info, conf)

    if (challenge.tfa === null) {
      output.standard('Two factor authentication mode changed to: ' + mode)
      return
    }

    const badResponse = typeof challenge.tfa !== 'string'
      || !/^otpauth:[/][/]/.test(challenge.tfa)
    if (badResponse) {
      throw new Error(
        'Unknown error enabling two-factor authentication. Expected otpauth URL' +
        ', got: ' + inspect(challenge.tfa)
      )
    }

    const otpauth = new URL(challenge.tfa)
    const secret = otpauth.searchParams.get('secret')
    const code = await qrcode(challenge.tfa)

    output.standard(
      'Scan into your authenticator app:\n' + code + '\n Or enter code:', secret
    )

    const interactiveOTP =
      await readUserInfo.otp('And an OTP code from your authenticator: ')

    log.info('profile', 'Finalizing two-factor authentication')

    const result = await set({ tfa: [interactiveOTP] }, conf)

    output.standard(
      '2FA successfully enabled. Below are your recovery codes, ' +
      'please print these out.'
    )
    output.standard(
      'You will need these to recover access to your account ' +
      'if you lose your authentication device.'
    )

    for (const tfaCode of result.tfa) {
      output.standard('\t' + tfaCode)
    }
  }

  async disable2fa () {
    const conf = { ...this.npm.flatOptions }
    const info = await get(conf)

    if (!info.tfa || info.tfa.pending) {
      output.standard('Two factor authentication not enabled.')
      return
    }

    const password = await readUserInfo.password()

    if (!conf.otp) {
      const msg = 'Enter one-time password: '
      conf.otp = await readUserInfo.otp(msg)
    }

    log.info('profile', 'disabling tfa')

    await set({ tfa: { password: password, mode: 'disable' } }, conf)

    if (this.npm.config.get('json')) {
      output.buffer({ tfa: false })
    } else if (this.npm.config.get('parseable')) {
      output.standard('tfa\tfalse')
    } else {
      output.standard('Two factor authentication disabled.')
    }
  }
}

module.exports = Profile
