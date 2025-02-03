'use strict'

const eu = encodeURIComponent
const npmFetch = require('npm-registry-fetch')
const validate = require('aproba')

// From https://github.com/npm/registry/blob/master/docs/orgs/memberships.md
const cmd = module.exports

class MembershipDetail {}
cmd.set = (org, user, role, opts = {}) => {
  if (
    typeof role === 'object' &&
    Object.keys(opts).length === 0
  ) {
    opts = role
    role = undefined
  }
  validate('SSSO|SSZO', [org, user, role, opts])
  user = user.replace(/^@?/, '')
  org = org.replace(/^@?/, '')
  return npmFetch.json(`/-/org/${eu(org)}/user`, {
    ...opts,
    method: 'PUT',
    body: { user, role },
  }).then(ret => Object.assign(new MembershipDetail(), ret))
}

cmd.rm = (org, user, opts = {}) => {
  validate('SSO', [org, user, opts])
  user = user.replace(/^@?/, '')
  org = org.replace(/^@?/, '')
  return npmFetch(`/-/org/${eu(org)}/user`, {
    ...opts,
    method: 'DELETE',
    body: { user },
    ignoreBody: true,
  }).then(() => null)
}

class Roster {}
cmd.ls = (org, opts = {}) => {
  return cmd.ls.stream(org, opts)
    .collect()
    .then(data => data.reduce((acc, [key, val]) => {
      if (!acc) {
        acc = {}
      }
      acc[key] = val
      return acc
    }, null))
    .then(ret => Object.assign(new Roster(), ret))
}

cmd.ls.stream = (org, opts = {}) => {
  validate('SO', [org, opts])
  org = org.replace(/^@?/, '')
  return npmFetch.json.stream(`/-/org/${eu(org)}/user`, '*', {
    ...opts,
    mapJSON: (value, [key]) => {
      return [key, value]
    },
  })
}
