const npa = require('npm-package-arg')
const npmFetch = require('npm-registry-fetch')
const semver = require('semver')
const { log, output } = require('proc-log')
const { otplease } = require('../utils/auth.js')
const pkgJson = require('@npmcli/package-json')
const BaseCommand = require('../base-cmd.js')

class DistTag extends BaseCommand {
  static description = 'Modify package distribution tags'
  static params = ['workspace', 'workspaces', 'include-workspace-root']
  static name = 'dist-tag'
  static usage = [
    'add <package-spec (with version)> [<tag>]',
    'rm <package-spec> <tag>',
    'ls [<package-spec>]',
  ]

  static workspaces = true
  static ignoreImplicitWorkspace = false

  static async completion (opts) {
    const argv = opts.conf.argv.remain
    if (argv.length === 2) {
      return ['add', 'rm', 'ls']
    }

    switch (argv[2]) {
      default:
        return []
    }
  }

  async exec ([cmdName, pkg, tag]) {
    const opts = {
      ...this.npm.flatOptions,
    }

    if (['add', 'a', 'set', 's'].includes(cmdName)) {
      return this.add(pkg, tag, opts)
    }

    if (['rm', 'r', 'del', 'd', 'remove'].includes(cmdName)) {
      return this.remove(pkg, tag, opts)
    }

    if (['ls', 'l', 'sl', 'list'].includes(cmdName)) {
      return this.list(pkg, opts)
    }

    if (!pkg) {
      // when only using the pkg name the default behavior
      // should be listing the existing tags
      return this.list(cmdName, opts)
    } else {
      throw this.usageError()
    }
  }

  async execWorkspaces ([cmdName, pkg, tag]) {
    // cmdName is some form of list
    // pkg is one of:
    // - unset
    // - .
    // - .@version
    if (['ls', 'l', 'sl', 'list'].includes(cmdName) && (!pkg || pkg === '.' || /^\.@/.test(pkg))) {
      return this.listWorkspaces()
    }

    // pkg is unset
    // cmdName is one of:
    // - unset
    // - .
    // - .@version
    if (!pkg && (!cmdName || cmdName === '.' || /^\.@/.test(cmdName))) {
      return this.listWorkspaces()
    }

    // anything else is just a regular dist-tag command
    // so we fallback to the non-workspaces implementation
    log.warn('dist-tag', 'Ignoring workspaces for specified package')
    return this.exec([cmdName, pkg, tag])
  }

  async add (spec, tag, opts) {
    spec = npa(spec || '')
    const version = spec.rawSpec
    const defaultTag = tag || this.npm.config.get('tag')

    log.verbose('dist-tag add', defaultTag, 'to', spec.name + '@' + version)

    // make sure new spec with tag is valid, this will throw if invalid
    npa(`${spec.name}@${defaultTag}`)

    if (!spec.name || !version || !defaultTag) {
      throw this.usageError('must provide a spec with a name and version, and a tag to add')
    }

    const t = defaultTag.trim()

    if (semver.validRange(t)) {
      throw new Error('Tag name must not be a valid SemVer range: ' + t)
    }

    const tags = await this.fetchTags(spec, opts)
    if (tags[t] === version) {
      log.warn('dist-tag add', t, 'is already set to version', version)
      return
    }
    tags[t] = version
    const url =
      `/-/package/${spec.escapedName}/dist-tags/${encodeURIComponent(t)}`
    const reqOpts = {
      ...opts,
      method: 'PUT',
      body: JSON.stringify(version),
      headers: {
        'content-type': 'application/json',
      },
      spec,
    }
    await otplease(this.npm, reqOpts, o => npmFetch(url, o))
    output.standard(`+${t}: ${spec.name}@${version}`)
  }

  async remove (spec, tag, opts) {
    spec = npa(spec || '')
    log.verbose('dist-tag del', tag, 'from', spec.name)

    if (!spec.name) {
      throw this.usageError()
    }

    const tags = await this.fetchTags(spec, opts)
    if (!tags[tag]) {
      log.info('dist-tag del', tag, 'is not a dist-tag on', spec.name)
      throw new Error(tag + ' is not a dist-tag on ' + spec.name)
    }
    const version = tags[tag]
    delete tags[tag]
    const url =
      `/-/package/${spec.escapedName}/dist-tags/${encodeURIComponent(tag)}`
    const reqOpts = {
      ...opts,
      method: 'DELETE',
      spec,
    }
    await otplease(this.npm, reqOpts, o => npmFetch(url, o))
    output.standard(`-${tag}: ${spec.name}@${version}`)
  }

  async list (spec, opts) {
    if (!spec) {
      if (this.npm.global) {
        throw this.usageError()
      }
      const { content: { name } } = await pkgJson.normalize(this.npm.prefix)
      if (!name) {
        throw this.usageError()
      }

      return this.list(name, opts)
    }
    spec = npa(spec)

    try {
      const tags = await this.fetchTags(spec, opts)
      const msg =
        Object.keys(tags).map(k => `${k}: ${tags[k]}`).sort().join('\n')
      output.standard(msg)
      return tags
    } catch (err) {
      log.error('dist-tag ls', "Couldn't get dist-tag data for", spec)
      throw err
    }
  }

  async listWorkspaces () {
    await this.setWorkspaces()

    for (const name of this.workspaceNames) {
      try {
        output.standard(`${name}:`)
        await this.list(npa(name), this.npm.flatOptions)
      } catch {
        // set the exitCode directly, but ignore the error
        // since it will have already been logged by this.list()
        process.exitCode = 1
      }
    }
  }

  async fetchTags (spec, opts) {
    const data = await npmFetch.json(
      `/-/package/${spec.escapedName}/dist-tags`,
      { ...opts, 'prefer-online': true, spec }
    )
    if (data && typeof data === 'object') {
      delete data._etag
    }
    if (!data || !Object.keys(data).length) {
      throw new Error('No dist-tags found for ' + spec.name)
    }

    return data
  }
}

module.exports = DistTag
