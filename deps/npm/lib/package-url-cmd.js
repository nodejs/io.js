const pacote = require('pacote')
const { openUrl } = require('./utils/open-url.js')
const { log } = require('proc-log')
const BaseCommand = require('./base-cmd.js')

// Base command for opening urls from a package manifest (bugs, docs, repo)
class PackageUrlCommand extends BaseCommand {
  static params = [
    'browser',
    'registry',
    'workspace',
    'workspaces',
    'include-workspace-root',
  ]

  static workspaces = true
  static ignoreImplicitWorkspace = false
  static usage = ['[<pkgname> [<pkgname> ...]]']

  async exec (args) {
    if (!args || !args.length) {
      args = ['.']
    }

    for (const arg of args) {
      // XXX It is very odd that `where` is how pacote knows to look anywhere
      // other than the cwd.
      const opts = {
        ...this.npm.flatOptions,
        where: this.npm.localPrefix,
        fullMetadata: true,
      }
      const mani = await pacote.manifest(arg, opts)
      const url = this.getUrl(arg, mani)
      log.silly(this.name, 'url', url)
      await openUrl(this.npm, url, `${mani.name} ${this.name} available at the following URL`)
    }
  }

  async execWorkspaces (args) {
    if (args && args.length) {
      return this.exec(args)
    }
    await this.setWorkspaces()
    return this.exec(this.workspacePaths)
  }

  // given a manifest, try to get the hosted git info from it based on
  // repository (if a string) or repository.url (if an object) returns null
  // if it's not a valid repo, or not a known hosted repo
  hostedFromMani (mani) {
    const hostedGitInfo = require('hosted-git-info')
    const r = mani.repository
    const rurl = !r ? null
      : typeof r === 'string' ? r
      : typeof r === 'object' && typeof r.url === 'string' ? r.url
      : null

    // hgi returns undefined sometimes, but let's always return null here
    return (rurl && hostedGitInfo.fromUrl(rurl.replace(/^git\+/, ''))) || null
  }
}

module.exports = PackageUrlCommand
