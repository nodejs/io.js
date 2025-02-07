const Deprecate = require('./deprecate.js')

class Undeprecate extends Deprecate {
  static description = 'Undeprecate a version of a package'
  static name = 'undeprecate'
  static usage = ['<package-spec>']

  async exec ([pkg]) {
    return super.exec([pkg, ''])
  }
}

module.exports = Undeprecate
