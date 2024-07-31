'use strict'
const fs = require('fs')
const npa = require('npm-package-arg')
const { URL } = require('url')

// Find the longest registry key that is used for some kind of auth
// in the options.  Returns the registry key and the auth config.
const regFromURI = (uri, opts) => {
  const parsed = new URL(uri)
  // try to find a config key indicating we have auth for this registry
  // can be one of :_authToken, :_auth, :_password and :username, or
  // :certfile and :keyfile
  // We walk up the "path" until we're left with just //<host>[:<port>],
  // stopping when we reach '//'.
  let regKey = `//${parsed.host}${parsed.pathname}`
  while (regKey.length > '//'.length) {
    const authKey = hasAuth(regKey, opts)
    // got some auth for this URI
    if (authKey) {
      return { regKey, authKey }
    }

    // can be either //host/some/path/:_auth or //host/some/path:_auth
    // walk up by removing EITHER what's after the slash OR the slash itself
    regKey = regKey.replace(/([^/]+|\/)$/, '')
  }
  return { regKey: false, authKey: null }
}

// Not only do we want to know if there is auth, but if we are calling `npm
// logout` we want to know what config value specifically provided it.  This is
// so we can look up where the config came from to delete it (i.e. user vs
// project)
const hasAuth = (regKey, opts) => {
  if (opts[`${regKey}:_authToken`]) {
    return '_authToken'
  }
  if (opts[`${regKey}:_auth`]) {
    return '_auth'
  }
  if (opts[`${regKey}:username`] && opts[`${regKey}:_password`]) {
    // 'password' can be inferred to also be present
    return 'username'
  }
  if (opts[`${regKey}:certfile`] && opts[`${regKey}:keyfile`]) {
    // 'keyfile' can be inferred to also be present
    return 'certfile'
  }
  return false
}

const sameHost = (a, b) => {
  const parsedA = new URL(a)
  const parsedB = new URL(b)
  return parsedA.host === parsedB.host
}

const getRegistry = opts => {
  const { spec } = opts
  const { scope: specScope, subSpec } = spec ? npa(spec) : {}
  const subSpecScope = subSpec && subSpec.scope
  const scope = subSpec ? subSpecScope : specScope
  const scopeReg = scope && opts[`${scope}:registry`]
  return scopeReg || opts.registry
}

const maybeReadFile = file => {
  try {
    return fs.readFileSync(file, 'utf8')
  } catch (er) {
    if (er.code !== 'ENOENT') {
      throw er
    }
    return null
  }
}

const getAuth = (uri, opts = {}) => {
  const { forceAuth } = opts
  if (!uri) {
    throw new Error('URI is required')
  }
  const { regKey, authKey } = regFromURI(uri, forceAuth || opts)

  // we are only allowed to use what's in forceAuth if specified
  if (forceAuth && !regKey) {
    return new Auth({
      // if we force auth we don't want to refer back to anything in config
      regKey: false,
      authKey: null,
      scopeAuthKey: null,
      token: forceAuth._authToken || forceAuth.token,
      username: forceAuth.username,
      password: forceAuth._password || forceAuth.password,
      auth: forceAuth._auth || forceAuth.auth,
      certfile: forceAuth.certfile,
      keyfile: forceAuth.keyfile,
    })
  }

  // no auth for this URI, but might have it for the registry
  if (!regKey) {
    const registry = getRegistry(opts)
    if (registry && uri !== registry && sameHost(uri, registry)) {
      return getAuth(registry, opts)
    } else if (registry !== opts.registry) {
      // If making a tarball request to a different base URI than the
      // registry where we logged in, but the same auth SHOULD be sent
      // to that artifact host, then we track where it was coming in from,
      // and warn the user if we get a 4xx error on it.
      const { regKey: scopeAuthKey, authKey: _authKey } = regFromURI(registry, opts)
      return new Auth({ scopeAuthKey, regKey: scopeAuthKey, authKey: _authKey })
    }
  }

  const {
    [`${regKey}:_authToken`]: token,
    [`${regKey}:username`]: username,
    [`${regKey}:_password`]: password,
    [`${regKey}:_auth`]: auth,
    [`${regKey}:certfile`]: certfile,
    [`${regKey}:keyfile`]: keyfile,
  } = opts

  return new Auth({
    scopeAuthKey: null,
    regKey,
    authKey,
    token,
    auth,
    username,
    password,
    certfile,
    keyfile,
  })
}

class Auth {
  constructor ({
    token,
    auth,
    username,
    password,
    scopeAuthKey,
    certfile,
    keyfile,
    regKey,
    authKey,
  }) {
    // same as regKey but only present for scoped auth. Should have been named scopeRegKey
    this.scopeAuthKey = scopeAuthKey
    // `${regKey}:${authKey}` will get you back to the auth config that gave us auth
    this.regKey = regKey
    this.authKey = authKey
    this.token = null
    this.auth = null
    this.isBasicAuth = false
    this.cert = null
    this.key = null
    if (token) {
      this.token = token
    } else if (auth) {
      this.auth = auth
    } else if (username && password) {
      const p = Buffer.from(password, 'base64').toString('utf8')
      this.auth = Buffer.from(`${username}:${p}`, 'utf8').toString('base64')
      this.isBasicAuth = true
    }
    // mTLS may be used in conjunction with another auth method above
    if (certfile && keyfile) {
      const cert = maybeReadFile(certfile, 'utf-8')
      const key = maybeReadFile(keyfile, 'utf-8')
      if (cert && key) {
        this.cert = cert
        this.key = key
      }
    }
  }
}

module.exports = getAuth
