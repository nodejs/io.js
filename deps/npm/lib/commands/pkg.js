const { output } = require('proc-log')
const PackageJson = require('@npmcli/package-json')
const BaseCommand = require('../base-cmd.js')
const Queryable = require('../utils/queryable.js')

class Pkg extends BaseCommand {
  static description = 'Manages your package.json'
  static name = 'pkg'
  static usage = [
    'set <key>=<value> [<key>=<value> ...]',
    'get [<key> [<key> ...]]',
    'delete <key> [<key> ...]',
    'set [<array>[<index>].<key>=<value> ...]',
    'set [<array>[].<key>=<value> ...]',
    'fix',
  ]

  static params = [
    'force',
    'json',
    'workspace',
    'workspaces',
  ]

  static workspaces = true
  static ignoreImplicitWorkspace = false

  async exec (args, { path = this.npm.localPrefix, workspace } = {}) {
    if (this.npm.global) {
      throw Object.assign(
        new Error(`There's no package.json file to manage on global mode`),
        { code: 'EPKGGLOBAL' }
      )
    }

    const [cmd, ..._args] = args
    switch (cmd) {
      case 'get':
        return this.get(_args, { path, workspace })
      case 'set':
        return this.set(_args, { path, workspace }).then(p => p.save())
      case 'delete':
        return this.delete(_args, { path, workspace }).then(p => p.save())
      case 'fix':
        return PackageJson.fix(path).then(p => p.save())
      default:
        throw this.usageError()
    }
  }

  async execWorkspaces (args) {
    await this.setWorkspaces()
    for (const [workspace, path] of this.workspaces.entries()) {
      await this.exec(args, { path, workspace })
    }
  }

  async get (args, { path, workspace }) {
    this.npm.config.set('json', true)
    const pkgJson = await PackageJson.load(path)

    let result = pkgJson.content

    if (args.length) {
      result = new Queryable(result).query(args)
      // in case there's only a single argument and a single result from the query
      // just prints that one element to stdout.
      // TODO(BREAKING_CHANGE): much like other places where we unwrap single
      // item arrays this should go away. it makes the behavior unknown for users
      // who don't already know the shape of the data.
      if (Object.keys(result).length === 1 && args.length === 1) {
        result = result[args]
      }
    }

    // The display layer is responsible for calling JSON.stringify on the result
    // TODO: https://github.com/npm/cli/issues/5508 a raw mode has been requested similar
    // to jq -r. If that was added then this method should no longer set `json:true` all the time
    output.buffer(workspace ? { [workspace]: result } : result)
  }

  async set (args, { path }) {
    const setError = () =>
      this.usageError('npm pkg set expects a key=value pair of args.')

    if (!args.length) {
      throw setError()
    }

    const force = this.npm.config.get('force')
    const json = this.npm.config.get('json')
    const pkgJson = await PackageJson.load(path)
    const q = new Queryable(pkgJson.content)
    for (const arg of args) {
      const [key, ...rest] = arg.split('=')
      const value = rest.join('=')
      if (!key || !value) {
        throw setError()
      }

      q.set(key, json ? JSON.parse(value) : value, { force })
    }

    return pkgJson.update(q.toJSON())
  }

  async delete (args, { path }) {
    const setError = () =>
      this.usageError('npm pkg delete expects key args.')

    if (!args.length) {
      throw setError()
    }

    const pkgJson = await PackageJson.load(path)
    const q = new Queryable(pkgJson.content)
    for (const key of args) {
      if (!key) {
        throw setError()
      }

      q.delete(key)
    }

    return pkgJson.update(q.toJSON())
  }
}

module.exports = Pkg
