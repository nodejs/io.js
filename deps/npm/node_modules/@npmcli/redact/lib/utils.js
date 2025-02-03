const {
  URL_MATCHER,
  TYPE_URL,
  TYPE_REGEX,
  TYPE_PATH,
} = require('./matchers')

/**
 * creates a string of asterisks,
 * this forces a minimum asterisk for security purposes
 */
const asterisk = (length = 0) => {
  length = typeof length === 'string' ? length.length : length
  if (length < 8) {
    return '*'.repeat(8)
  }
  return '*'.repeat(length)
}

/**
 * escapes all special regex chars
 * @see https://stackoverflow.com/a/9310752
 * @see https://github.com/tc39/proposal-regex-escaping
 */
const escapeRegExp = (text) => {
  return text.replace(/[-[\]{}()*+?.,\\^$|#\s]/g, `\\$&`)
}

/**
 * provieds a regex "or" of the url versions of a string
 */
const urlEncodeRegexGroup = (value) => {
  const decoded = decodeURIComponent(value)
  const encoded = encodeURIComponent(value)
  const union = [...new Set([encoded, decoded, value])].map(escapeRegExp).join('|')
  return union
}

/**
 * a tagged template literal that returns a regex ensures all variables are excaped
 */
const urlEncodeRegexTag = (strings, ...values) => {
  let pattern = ''
  for (let i = 0; i < values.length; i++) {
    pattern += strings[i] + `(${urlEncodeRegexGroup(values[i])})`
  }
  pattern += strings[strings.length - 1]
  return new RegExp(pattern)
}

/**
 * creates a matcher for redacting url hostname
 */
const redactUrlHostnameMatcher = ({ hostname, replacement } = {}) => ({
  type: TYPE_URL,
  predicate: ({ url }) => url.hostname === hostname,
  pattern: ({ url }) => {
    return urlEncodeRegexTag`(^${url.protocol}//${url.username}:.+@)?${url.hostname}`
  },
  replacement: `$1${replacement || asterisk()}`,
})

/**
 * creates a matcher for redacting url search / query parameter values
 */
const redactUrlSearchParamsMatcher = ({ param, replacement } = {}) => ({
  type: TYPE_URL,
  predicate: ({ url }) => url.searchParams.has(param),
  pattern: ({ url }) => urlEncodeRegexTag`(${param}=)${url.searchParams.get(param)}`,
  replacement: `$1${replacement || asterisk()}`,
})

/** creates a matcher for redacting the url password */
const redactUrlPasswordMatcher = ({ replacement } = {}) => ({
  type: TYPE_URL,
  predicate: ({ url }) => url.password,
  pattern: ({ url }) => urlEncodeRegexTag`(^${url.protocol}//${url.username}:)${url.password}`,
  replacement: `$1${replacement || asterisk()}`,
})

const redactUrlReplacement = (...matchers) => (subValue) => {
  try {
    const url = new URL(subValue)
    return redactMatchers(...matchers)(subValue, { url })
  } catch (err) {
    return subValue
  }
}

/**
 * creates a matcher / submatcher for urls, this function allows you to first
 * collect all urls within a larger string and then pass those urls to a
 * submatcher
 *
 * @example
 * console.log("this will first match all urls, then pass those urls to the password patcher")
 * redactMatchers(redactUrlMatcher(redactUrlPasswordMatcher()))
 *
 * @example
 * console.log(
 *   "this will assume you are passing in a string that is a url, and will redact the password"
 * )
 * redactMatchers(redactUrlPasswordMatcher())
 *
 */
const redactUrlMatcher = (...matchers) => {
  return {
    ...URL_MATCHER,
    replacement: redactUrlReplacement(...matchers),
  }
}

const matcherFunctions = {
  [TYPE_REGEX]: (matcher) => (value) => {
    if (typeof value === 'string') {
      value = value.replace(matcher.pattern, matcher.replacement)
    }
    return value
  },
  [TYPE_URL]: (matcher) => (value, ctx) => {
    if (typeof value === 'string') {
      try {
        const url = ctx?.url || new URL(value)
        const { predicate, pattern } = matcher
        const predicateValue = predicate({ url })
        if (predicateValue) {
          value = value.replace(pattern({ url }), matcher.replacement)
        }
      } catch (_e) {
        return value
      }
    }
    return value
  },
  [TYPE_PATH]: (matcher) => (value, ctx) => {
    const rawPath = ctx?.path
    const path = rawPath.join('.').toLowerCase()
    const { predicate, replacement } = matcher
    const replace = typeof replacement === 'function' ? replacement : () => replacement
    const shouldRun = predicate({ rawPath, path })
    if (shouldRun) {
      value = replace(value, { rawPath, path })
    }
    return value
  },
}

/** converts a matcher to a function */
const redactMatcher = (matcher) => {
  return matcherFunctions[matcher.type](matcher)
}

/** converts a series of matchers to a function */
const redactMatchers = (...matchers) => (value, ctx) => {
  const flatMatchers = matchers.flat()
  return flatMatchers.reduce((result, matcher) => {
    const fn = (typeof matcher === 'function') ? matcher : redactMatcher(matcher)
    return fn(result, ctx)
  }, value)
}

/**
 * replacement handler, keeping $1 (if it exists) and replacing the
 * rest of the string with asterisks, maintaining string length
 */
const redactDynamicReplacement = () => (value, start) => {
  if (typeof start === 'number') {
    return asterisk(value)
  }
  return start + asterisk(value.substring(start.length).length)
}

/**
 * replacement handler, keeping $1 (if it exists) and replacing the
 * rest of the string with a fixed number of asterisks
 */
const redactFixedReplacement = (length) => (_value, start) => {
  if (typeof start === 'number') {
    return asterisk(length)
  }
  return start + asterisk(length)
}

const redactUrlPassword = (value, replacement) => {
  return redactMatchers(redactUrlPasswordMatcher({ replacement }))(value)
}

module.exports = {
  asterisk,
  escapeRegExp,
  urlEncodeRegexGroup,
  urlEncodeRegexTag,
  redactUrlHostnameMatcher,
  redactUrlSearchParamsMatcher,
  redactUrlPasswordMatcher,
  redactUrlMatcher,
  redactUrlReplacement,
  redactDynamicReplacement,
  redactFixedReplacement,
  redactMatchers,
  redactUrlPassword,
}
