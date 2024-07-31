// Parse a field, coercing it to the best type available.
const typeDefs = require('./type-defs.js')
const envReplace = require('./env-replace.js')
const { resolve } = require('node:path')

const { parse: umaskParse } = require('./umask.js')

const parseField = (f, key, opts, listElement = false) => {
  if (typeof f !== 'string' && !Array.isArray(f)) {
    return f
  }

  const { platform, types, home, env } = opts

  // type can be array or a single thing.  coerce to array.
  const typeList = new Set([].concat(types[key]))
  const isPath = typeList.has(typeDefs.path.type)
  const isBool = typeList.has(typeDefs.Boolean.type)
  const isString = isPath || typeList.has(typeDefs.String.type)
  const isUmask = typeList.has(typeDefs.Umask.type)
  const isNumber = typeList.has(typeDefs.Number.type)
  const isList = !listElement && typeList.has(Array)
  const isDate = typeList.has(typeDefs.Date.type)

  if (Array.isArray(f)) {
    return !isList ? f : f.map(field => parseField(field, key, opts, true))
  }

  // now we know it's a string
  f = f.trim()

  // list types get put in the environment separated by double-\n
  // usually a single \n would suffice, but ca/cert configs can contain
  // line breaks and multiple entries.
  if (isList) {
    return parseField(f.split('\n\n'), key, opts)
  }

  // --foo is like --foo=true for boolean types
  if (isBool && !isString && f === '') {
    return true
  }

  // string types can be the string 'true', 'false', etc.
  // otherwise, parse these values out
  if (!isString && !isPath && !isNumber) {
    switch (f) {
      case 'true': return true
      case 'false': return false
      case 'null': return null
      case 'undefined': return undefined
    }
  }

  f = envReplace(f, env)

  if (isDate) {
    return new Date(f)
  }

  if (isPath) {
    const homePattern = platform === 'win32' ? /^~(\/|\\)/ : /^~\//
    if (homePattern.test(f) && home) {
      f = resolve(home, f.slice(2))
    } else {
      f = resolve(f)
    }
  }

  if (isUmask) {
    try {
      return umaskParse(f)
    } catch (er) {
      // let it warn later when we validate
      return f
    }
  }

  if (isNumber && !isNaN(f)) {
    f = +f
  }

  return f
}

module.exports = parseField
