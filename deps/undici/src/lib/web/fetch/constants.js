'use strict'

const corsSafeListedMethods = ['GET', 'HEAD', 'POST']
const corsSafeListedMethodsSet = new Set(corsSafeListedMethods)

const nullBodyStatus = [101, 204, 205, 304]

const redirectStatus = [301, 302, 303, 307, 308]
const redirectStatusSet = new Set(redirectStatus)

// https://fetch.spec.whatwg.org/#block-bad-port
const badPorts = [
  '1', '7', '9', '11', '13', '15', '17', '19', '20', '21', '22', '23', '25', '37', '42', '43', '53', '69', '77', '79',
  '87', '95', '101', '102', '103', '104', '109', '110', '111', '113', '115', '117', '119', '123', '135', '137',
  '139', '143', '161', '179', '389', '427', '465', '512', '513', '514', '515', '526', '530', '531', '532',
  '540', '548', '554', '556', '563', '587', '601', '636', '989', '990', '993', '995', '1719', '1720', '1723',
  '2049', '3659', '4045', '4190', '5060', '5061', '6000', '6566', '6665', '6666', '6667', '6668', '6669', '6679',
  '6697', '10080'
]

const badPortsSet = new Set(badPorts)

// https://w3c.github.io/webappsec-referrer-policy/#referrer-policies
const referrerPolicy = [
  '',
  'no-referrer',
  'no-referrer-when-downgrade',
  'same-origin',
  'origin',
  'strict-origin',
  'origin-when-cross-origin',
  'strict-origin-when-cross-origin',
  'unsafe-url'
]
const referrerPolicySet = new Set(referrerPolicy)

const requestRedirect = ['follow', 'manual', 'error']

const safeMethods = ['GET', 'HEAD', 'OPTIONS', 'TRACE']
const safeMethodsSet = new Set(safeMethods)

const requestMode = ['navigate', 'same-origin', 'no-cors', 'cors']

const requestCredentials = ['omit', 'same-origin', 'include']

const requestCache = [
  'default',
  'no-store',
  'reload',
  'no-cache',
  'force-cache',
  'only-if-cached'
]

// https://fetch.spec.whatwg.org/#request-body-header-name
const requestBodyHeader = [
  'content-encoding',
  'content-language',
  'content-location',
  'content-type',
  // See https://github.com/nodejs/undici/issues/2021
  // 'Content-Length' is a forbidden header name, which is typically
  // removed in the Headers implementation. However, undici doesn't
  // filter out headers, so we add it here.
  'content-length'
]

// https://fetch.spec.whatwg.org/#enumdef-requestduplex
const requestDuplex = [
  'half'
]

// http://fetch.spec.whatwg.org/#forbidden-method
const forbiddenMethods = ['CONNECT', 'TRACE', 'TRACK']
const forbiddenMethodsSet = new Set(forbiddenMethods)

const subresource = [
  'audio',
  'audioworklet',
  'font',
  'image',
  'manifest',
  'paintworklet',
  'script',
  'style',
  'track',
  'video',
  'xslt',
  ''
]
const subresourceSet = new Set(subresource)

module.exports = {
  subresource,
  forbiddenMethods,
  requestBodyHeader,
  referrerPolicy,
  requestRedirect,
  requestMode,
  requestCredentials,
  requestCache,
  redirectStatus,
  corsSafeListedMethods,
  nullBodyStatus,
  safeMethods,
  badPorts,
  requestDuplex,
  subresourceSet,
  badPortsSet,
  redirectStatusSet,
  corsSafeListedMethodsSet,
  safeMethodsSet,
  forbiddenMethodsSet,
  referrerPolicySet
}
