const fs = require('node:fs')
const os = require('node:os')
const path = require('node:path')
const ini = require('ini')

const gitConfigPath = path.join(os.homedir(), '.gitconfig')

let cachedConfig = null

// Function to load and cache the git config
const loadGitConfig = () => {
  if (cachedConfig === null) {
    try {
      cachedConfig = {}
      if (fs.existsSync(gitConfigPath)) {
        const configContent = fs.readFileSync(gitConfigPath, 'utf-8')
        cachedConfig = ini.parse(configContent)
      }
    } catch (error) {
      cachedConfig = {}
    }
  }
  return cachedConfig
}

const checkGitConfigs = () => {
  const config = loadGitConfig()
  return {
    sshCommandSetInConfig: config?.core?.sshCommand !== undefined,
    askPassSetInConfig: config?.core?.askpass !== undefined,
  }
}

const sshCommandSetInEnv = process.env.GIT_SSH_COMMAND !== undefined
const askPassSetInEnv = process.env.GIT_ASKPASS !== undefined
const { sshCommandSetInConfig, askPassSetInConfig } = checkGitConfigs()

// Values we want to set if they're not already defined by the end user
// This defaults to accepting new ssh host key fingerprints
const finalGitEnv = {
  ...(askPassSetInEnv || askPassSetInConfig ? {} : {
    GIT_ASKPASS: 'echo',
  }),
  ...(sshCommandSetInEnv || sshCommandSetInConfig ? {} : {
    GIT_SSH_COMMAND: 'ssh -oStrictHostKeyChecking=accept-new',
  }),
}

module.exports = (opts = {}) => ({
  stdioString: true,
  ...opts,
  shell: false,
  env: opts.env || { ...finalGitEnv, ...process.env },
})

// Export the loadGitConfig function for testing
module.exports.loadGitConfig = loadGitConfig
