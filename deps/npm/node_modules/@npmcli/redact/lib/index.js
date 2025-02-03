const matchers = require('./matchers')
const { redactUrlPassword } = require('./utils')

const REPLACE = '***'

const redact = (value) => {
  if (typeof value !== 'string' || !value) {
    return value
  }
  return redactUrlPassword(value, REPLACE)
    .replace(matchers.NPM_SECRET.pattern, `npm_${REPLACE}`)
    .replace(matchers.UUID.pattern, REPLACE)
}

// split on \s|= similar to how nopt parses options
const splitAndRedact = (str) => {
  // stateful regex, don't move out of this scope
  const splitChars = /[\s=]/g

  let match = null
  let result = ''
  let index = 0
  while (match = splitChars.exec(str)) {
    result += redact(str.slice(index, match.index)) + match[0]
    index = splitChars.lastIndex
  }

  return result + redact(str.slice(index))
}

// replaces auth info in an array of arguments or in a strings
const redactLog = (arg) => {
  if (typeof arg === 'string') {
    return splitAndRedact(arg)
  } else if (Array.isArray(arg)) {
    return arg.map((a) => typeof a === 'string' ? splitAndRedact(a) : a)
  }
  return arg
}

module.exports = {
  redact,
  redactLog,
}
