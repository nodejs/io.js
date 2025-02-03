const { log } = require('proc-log')
const BaseCommand = require('./base-cmd.js')

// This is the base for all commands whose execWorkspaces just gets
// a list of workspace names and passes it on to new Arborist() to
// be able to run a filtered Arborist.reify() at some point.
class ArboristCmd extends BaseCommand {
  get isArboristCmd () {
    return true
  }

  static params = [
    'workspace',
    'workspaces',
    'include-workspace-root',
    'install-links',
  ]

  static workspaces = true
  static ignoreImplicitWorkspace = false
  static checkDevEngines = true

  constructor (npm) {
    super(npm)

    const { config } = this.npm

    // when location isn't set and global isn't true check for a package.json at
    // the localPrefix and set the location to project if found
    const locationProject = config.get('location') === 'project' || (
      config.isDefault('location')
      // this is different then `npm.global` which falls back to checking
      // location which we do not want to use here
      && !config.get('global')
      && npm.localPackage
    )

    // if audit is not set and we are in global mode and location is not project
    // and we assume its not a project related context, then we set audit=false
    if (config.isDefault('audit') && (this.npm.global || !locationProject)) {
      config.set('audit', false)
    } else if (this.npm.global && config.get('audit')) {
      log.warn('config', 'includes both --global and --audit, which is currently unsupported.')
    }
  }

  async execWorkspaces (args) {
    await this.setWorkspaces()
    return this.exec(args)
  }
}

module.exports = ArboristCmd
