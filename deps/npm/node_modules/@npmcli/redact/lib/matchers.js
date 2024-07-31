const TYPE_REGEX = 'regex'
const TYPE_URL = 'url'
const TYPE_PATH = 'path'

const NPM_SECRET = {
  type: TYPE_REGEX,
  pattern: /\b(npms?_)[a-zA-Z0-9]{36,48}\b/gi,
  replacement: `[REDACTED_NPM_SECRET]`,
}

const AUTH_HEADER = {
  type: TYPE_REGEX,
  pattern: /\b(Basic\s+|Bearer\s+)[\w+=\-.]+\b/gi,
  replacement: `[REDACTED_AUTH_HEADER]`,
}

const JSON_WEB_TOKEN = {
  type: TYPE_REGEX,
  pattern: /\b[A-Za-z0-9-_]{10,}(?!\.\d+\.)\.[A-Za-z0-9-_]{3,}\.[A-Za-z0-9-_]{20,}\b/gi,
  replacement: `[REDACTED_JSON_WEB_TOKEN]`,
}

const UUID = {
  type: TYPE_REGEX,
  pattern: /\b[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\b/gi,
  replacement: `[REDACTED_UUID]`,
}

const URL_MATCHER = {
  type: TYPE_REGEX,
  pattern: /(?:https?|ftp):\/\/[^\s/"$.?#].[^\s"]*/gi,
  replacement: '[REDACTED_URL]',
}

const DEEP_HEADER_AUTHORIZATION = {
  type: TYPE_PATH,
  predicate: ({ path }) => path.endsWith('.headers.authorization'),
  replacement: '[REDACTED_HEADER_AUTHORIZATION]',
}

const DEEP_HEADER_SET_COOKIE = {
  type: TYPE_PATH,
  predicate: ({ path }) => path.endsWith('.headers.set-cookie'),
  replacement: '[REDACTED_HEADER_SET_COOKIE]',
}

const REWRITE_REQUEST = {
  type: TYPE_PATH,
  predicate: ({ path }) => path.endsWith('.request'),
  replacement: (input) => ({
    method: input?.method,
    path: input?.path,
    headers: input?.headers,
    url: input?.url,
  }),
}

const REWRITE_RESPONSE = {
  type: TYPE_PATH,
  predicate: ({ path }) => path.endsWith('.response'),
  replacement: (input) => ({
    data: input?.data,
    status: input?.status,
    headers: input?.headers,
  }),
}

module.exports = {
  TYPE_REGEX,
  TYPE_URL,
  TYPE_PATH,
  NPM_SECRET,
  AUTH_HEADER,
  JSON_WEB_TOKEN,
  UUID,
  URL_MATCHER,
  DEEP_HEADER_AUTHORIZATION,
  DEEP_HEADER_SET_COOKIE,
  REWRITE_REQUEST,
  REWRITE_RESPONSE,
}
