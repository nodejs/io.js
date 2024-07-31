const {
  AUTH_HEADER,
  JSON_WEB_TOKEN,
  NPM_SECRET,
  DEEP_HEADER_AUTHORIZATION,
  DEEP_HEADER_SET_COOKIE,
  REWRITE_REQUEST,
  REWRITE_RESPONSE,
} = require('./matchers')

const {
  redactUrlMatcher,
  redactUrlPasswordMatcher,
  redactMatchers,
} = require('./utils')

const { deepMap } = require('./deep-map')

const _redact = redactMatchers(
  NPM_SECRET,
  AUTH_HEADER,
  JSON_WEB_TOKEN,
  DEEP_HEADER_AUTHORIZATION,
  DEEP_HEADER_SET_COOKIE,
  REWRITE_REQUEST,
  REWRITE_RESPONSE,
  redactUrlMatcher(
    redactUrlPasswordMatcher()
  )
)

const redact = (input) => deepMap(input, (value, path) => _redact(value, { path }))

module.exports = { redact }
