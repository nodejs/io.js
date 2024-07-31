const { log, output } = require('proc-log')
const { redactLog: replaceInfo } = require('@npmcli/redact')
const auth = require('../utils/auth.js')
const BaseCommand = require('../base-cmd.js')

class Login extends BaseCommand {
  static description = 'Login to a registry user account'
  static name = 'login'
  static params = [
    'registry',
    'scope',
    'auth-type',
  ]

  async exec () {
    const scope = this.npm.config.get('scope')
    let registry = this.npm.config.get('registry')

    if (scope) {
      const scopedRegistry = this.npm.config.get(`${scope}:registry`)
      const cliRegistry = this.npm.config.get('registry', 'cli')
      if (scopedRegistry && !cliRegistry) {
        registry = scopedRegistry
      }
    }

    const creds = this.npm.config.getCredentialsByURI(registry)

    log.notice('', `Log in on ${replaceInfo(registry)}`)

    const { message, newCreds } = await auth.login(this.npm, {
      ...this.npm.flatOptions,
      creds,
      registry,
    })

    this.npm.config.delete('_token', 'user') // prevent legacy pollution
    this.npm.config.setCredentialsByURI(registry, newCreds)

    if (scope) {
      this.npm.config.set(scope + ':registry', registry, 'user')
    }

    await this.npm.config.save('user')

    output.standard(message)
  }
}

module.exports = Login
