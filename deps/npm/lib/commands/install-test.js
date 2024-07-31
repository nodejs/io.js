const Install = require('./install.js')

// npm install-test
// Runs `npm install` and then runs `npm test`
class InstallTest extends Install {
  static description = 'Install package(s) and run tests'
  static name = 'install-test'

  async exec (args) {
    await this.npm.exec('install', args)
    return this.npm.exec('test', [])
  }
}

module.exports = InstallTest
