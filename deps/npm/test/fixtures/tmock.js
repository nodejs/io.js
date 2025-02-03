const path = require('node:path')

const ROOT = path.resolve(__dirname, '../..')
const BIN = path.join(ROOT, 'bin')
const LIB = path.join(ROOT, 'lib')

// since mock npm changes directories it can be hard to figure out the
// correct path to mock something with tap since the directory will change
// before/after npm is loaded. This helper replaces {BIN} and {LIB} with
// the absolute path to those directories
const replace = (s) => {
  if (/^[./{]/.test(s)) {
    return s
      .replace(/^\{BIN\}/, BIN)
      .replace(/^\{LIB\}/, LIB)
      .replace(/^\{ROOT\}/, ROOT)
  } else {
    return require.resolve(s)
  }
}

const tmock = (t, p, mocks = {}) => {
  const entries = Object.entries(mocks).map(([k, v]) => [replace(k), v])
  return t.mock(replace(p), Object.fromEntries(entries))
}

module.exports = tmock
