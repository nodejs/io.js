const CI = require('./ci.js')

// npm install-ci-test
// Runs `npm ci` and then runs `npm test`
class InstallCITest extends CI {
  static description = 'Install a project with a clean slate and run tests'
  static name = 'install-ci-test'

  async exec (args) {
    await this.npm.exec('ci', args)
    return this.npm.exec('test', [])
  }
}

module.exports = InstallCITest
