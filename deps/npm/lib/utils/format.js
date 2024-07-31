const { formatWithOptions: baseFormatWithOptions } = require('node:util')

// These are most assuredly not a mistake
// https://eslint.org/docs/latest/rules/no-control-regex
// \x00 through \x1f, \x7f through \x9f, not including \x09 \x0a \x0b \x0d
/* eslint-disable-next-line no-control-regex */
const HAS_C01 = /[\x00-\x08\x0c\x0e-\x1f\x7f-\x9f]/

// Allows everything up to '[38;5;255m' in 8 bit notation
const ALLOWED_SGR = /^\[[0-9;]{0,8}m/

// '[38;5;255m'.length
const SGR_MAX_LEN = 10

// Strips all ANSI C0 and C1 control characters (except for SGR up to 8 bit)
function STRIP_C01 (str) {
  if (!HAS_C01.test(str)) {
    return str
  }
  let result = ''
  for (let i = 0; i < str.length; i++) {
    const char = str[i]
    const code = char.charCodeAt(0)
    if (!HAS_C01.test(char)) {
      // Most characters are in this set so continue early if we can
      result = `${result}${char}`
    } else if (code === 27 && ALLOWED_SGR.test(str.slice(i + 1, i + SGR_MAX_LEN + 1))) {
      // \x1b with allowed SGR
      result = `${result}\x1b`
    } else if (code <= 31) {
      // escape all other C0 control characters besides \x7f
      result = `${result}^${String.fromCharCode(code + 64)}`
    } else {
      // hasC01 ensures this is now a C1 control character or \x7f
      result = `${result}^${String.fromCharCode(code - 64)}`
    }
  }
  return result
}

const formatWithOptions = ({ prefix: prefixes = [], eol = '\n', ...options }, ...args) => {
  const prefix = prefixes.filter(p => p != null).join(' ')
  const formatted = STRIP_C01(baseFormatWithOptions(options, ...args))
  // Splitting could be changed to only `\n` once we are sure we only emit unix newlines.
  // The eol param to this function will put the correct newlines in place for the returned string.
  const lines = formatted.split(/\r?\n/)
  return lines.reduce((acc, l) => `${acc}${prefix}${prefix && l ? ' ' : ''}${l}${eol}`, '')
}

module.exports = { formatWithOptions }
