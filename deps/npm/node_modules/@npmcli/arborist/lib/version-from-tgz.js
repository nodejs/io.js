const semver = require('semver')
const { basename } = require('node:path')
const { URL } = require('node:url')
module.exports = (name, tgz) => {
  const base = basename(tgz)
  if (!base.endsWith('.tgz')) {
    return null
  }

  if (tgz.startsWith('http:/') || tgz.startsWith('https:/')) {
    const u = new URL(tgz)
    // registry url?  check for most likely pattern.
    // either /@foo/bar/-/bar-1.2.3.tgz or
    // /foo/-/foo-1.2.3.tgz, and fall through to
    // basename checking.  Note that registries can
    // be mounted below the root url, so /a/b/-/x/y/foo/-/foo-1.2.3.tgz
    // is a potential option.
    const tfsplit = u.pathname.slice(1).split('/-/')
    if (tfsplit.length > 1) {
      const afterTF = tfsplit.pop()
      if (afterTF === base) {
        const pre = tfsplit.pop()
        const preSplit = pre.split(/\/|%2f/i)
        const project = preSplit.pop()
        const scope = preSplit.pop()
        return versionFromBaseScopeName(base, scope, project)
      }
    }
  }

  const split = name.split(/\/|%2f/i)
  const project = split.pop()
  const scope = split.pop()
  return versionFromBaseScopeName(base, scope, project)
}

const versionFromBaseScopeName = (base, scope, name) => {
  if (!base.startsWith(name + '-')) {
    return null
  }

  const parsed = semver.parse(base.substring(name.length + 1, base.length - 4))
  return parsed ? {
    name: scope && scope.charAt(0) === '@' ? `${scope}/${name}` : name,
    version: parsed.version,
  } : null
}
