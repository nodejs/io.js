const nopt = require('nopt')

const { validate: validateUmask } = require('./umask.js')

class Umask {}
class Semver {}
const semverValid = require('semver/functions/valid')
const validateSemver = (data, k, val) => {
  const valid = semverValid(val)
  if (!valid) {
    return false
  }
  data[k] = valid
}

const noptValidatePath = nopt.typeDefs.path.validate
const validatePath = (data, k, val) => {
  if (typeof val !== 'string') {
    return false
  }
  return noptValidatePath(data, k, val)
}

// add descriptions so we can validate more usefully
module.exports = {
  ...nopt.typeDefs,
  semver: {
    type: Semver,
    validate: validateSemver,
    description: 'full valid SemVer string',
  },
  Umask: {
    type: Umask,
    validate: validateUmask,
    description: 'octal number in range 0o000..0o777 (0..511)',
  },
  url: {
    ...nopt.typeDefs.url,
    description: 'full url with "http://"',
  },
  path: {
    ...nopt.typeDefs.path,
    validate: validatePath,
    description: 'valid filesystem path',
  },
  Number: {
    ...nopt.typeDefs.Number,
    description: 'numeric value',
  },
  Boolean: {
    ...nopt.typeDefs.Boolean,
    description: 'boolean value (true or false)',
  },
  Date: {
    ...nopt.typeDefs.Date,
    description: 'valid Date string',
  },
}

// TODO: make nopt less of a global beast so this kludge isn't necessary
nopt.typeDefs = module.exports
