const { mkdir, readFile, writeFile } = require('node:fs/promises')
const { dirname, resolve } = require('node:path')
const { spawn } = require('node:child_process')
const { EOL } = require('node:os')
const localeCompare = require('@isaacs/string-locale-compare')('en')
const pkgJson = require('@npmcli/package-json')
const { defaults, definitions } = require('@npmcli/config/lib/definitions')
const { log, output } = require('proc-log')
const BaseCommand = require('../base-cmd.js')
const { redact } = require('@npmcli/redact')

// These are the configs that we can nerf-dart. Not all of them currently even
// *have* config definitions so we have to explicitly validate them here.
// This is used to validate during "npm config set"
const nerfDarts = [
  '_auth',
  '_authToken',
  '_password',
  'certfile',
  'email',
  'keyfile',
  'username',
]
// These are the config values to swap with "protected".  It does not catch
// every single sensitive thing a user may put in the npmrc file but it gets
// the common ones.  This is distinct from nerfDarts because that is used to
// validate valid configs during "npm config set", and folks may have old
// invalid entries lying around in a config file that we still want to protect
// when running "npm config list"
// This is a more general list of values to consider protected.  You can not
// "npm config get" them, and they will not display during "npm config list"
const protected = [
  'auth',
  'authToken',
  'certfile',
  'email',
  'keyfile',
  'password',
  'username',
]

// take an array of `[key, value, k2=v2, k3, v3, ...]` and turn into
// { key: value, k2: v2, k3: v3 }
const keyValues = args => {
  const kv = {}
  for (let i = 0; i < args.length; i++) {
    const arg = args[i].split('=')
    const key = arg.shift()
    const val = arg.length ? arg.join('=')
      : i < args.length - 1 ? args[++i]
      : ''
    kv[key.trim()] = val.trim()
  }
  return kv
}

const isProtected = (k) => {
  // _password
  if (k.startsWith('_')) {
    return true
  }
  if (protected.includes(k)) {
    return true
  }
  // //localhost:8080/:_password
  if (k.startsWith('//')) {
    if (k.includes(':_')) {
      return true
    }
    // //registry:_authToken or //registry:authToken
    for (const p of protected) {
      if (k.endsWith(`:${p}`) || k.endsWith(`:_${p}`)) {
        return true
      }
    }
  }
  return false
}

// Private fields are either protected or they can redacted info
const isPrivate = (k, v) => isProtected(k) || redact(v) !== v

const displayVar = (k, v) =>
  `${k} = ${isProtected(k, v) ? '(protected)' : JSON.stringify(redact(v))}`

class Config extends BaseCommand {
  static description = 'Manage the npm configuration files'
  static name = 'config'
  static usage = [
    'set <key>=<value> [<key>=<value> ...]',
    'get [<key> [<key> ...]]',
    'delete <key> [<key> ...]',
    'list [--json]',
    'edit',
    'fix',
  ]

  static params = [
    'json',
    'global',
    'editor',
    'location',
    'long',
  ]

  static ignoreImplicitWorkspace = false

  static skipConfigValidation = true

  static async completion (opts) {
    const argv = opts.conf.argv.remain
    if (argv[1] !== 'config') {
      argv.unshift('config')
    }

    if (argv.length === 2) {
      const cmds = ['get', 'set', 'delete', 'ls', 'rm', 'edit', 'fix']
      if (opts.partialWord !== 'l') {
        cmds.push('list')
      }

      return cmds
    }

    const action = argv[2]
    switch (action) {
      case 'set':
        // todo: complete with valid values, if possible.
        if (argv.length > 3) {
          return []
        }

        // fallthrough
        /* eslint no-fallthrough:0 */
      case 'get':
      case 'delete':
      case 'rm':
        return Object.keys(definitions)
      case 'edit':
      case 'list':
      case 'ls':
      case 'fix':
      default:
        return []
    }
  }

  async exec ([action, ...args]) {
    switch (action) {
      case 'set':
        await this.set(args)
        break
      case 'get':
        await this.get(args)
        break
      case 'delete':
      case 'rm':
      case 'del':
        await this.del(args)
        break
      case 'list':
      case 'ls':
        await (this.npm.flatOptions.json ? this.listJson() : this.list())
        break
      case 'edit':
        await this.edit()
        break
      case 'fix':
        await this.fix()
        break
      default:
        throw this.usageError()
    }
  }

  async set (args) {
    if (!args.length) {
      throw this.usageError()
    }

    const where = this.npm.flatOptions.location
    for (const [key, val] of Object.entries(keyValues(args))) {
      log.info('config', 'set %j %j', key, val)
      const baseKey = key.split(':').pop()
      if (!this.npm.config.definitions[baseKey] && !nerfDarts.includes(baseKey)) {
        throw new Error(`\`${baseKey}\` is not a valid npm option`)
      }
      const deprecated = this.npm.config.definitions[baseKey]?.deprecated
      if (deprecated) {
        throw new Error(
          `The \`${baseKey}\` option is deprecated, and can not be set in this way${deprecated}`
        )
      }

      if (val === '') {
        this.npm.config.delete(key, where)
      } else {
        this.npm.config.set(key, val, where)
      }

      if (!this.npm.config.validate(where)) {
        log.warn('config', 'omitting invalid config values')
      }
    }

    await this.npm.config.save(where)
  }

  async get (keys) {
    if (!keys.length) {
      return this.list()
    }

    const out = []
    for (const key of keys) {
      const val = this.npm.config.get(key)
      if (isPrivate(key, val)) {
        throw new Error(`The ${key} option is protected, and can not be retrieved in this way`)
      }

      const pref = keys.length > 1 ? `${key}=` : ''
      out.push(pref + val)
    }
    output.standard(out.join('\n'))
  }

  async del (keys) {
    if (!keys.length) {
      throw this.usageError()
    }

    const where = this.npm.flatOptions.location
    for (const key of keys) {
      this.npm.config.delete(key, where)
    }
    await this.npm.config.save(where)
  }

  async edit () {
    const ini = require('ini')
    const e = this.npm.flatOptions.editor
    const where = this.npm.flatOptions.location
    const file = this.npm.config.data.get(where).source

    // save first, just to make sure it's synced up
    // this also removes all the comments from the last time we edited it.
    await this.npm.config.save(where)

    const data = (
      await readFile(file, 'utf8').catch(() => '')
    ).replace(/\r\n/g, '\n')
    const entries = Object.entries(defaults)
    const defData = entries.reduce((str, [key, val]) => {
      const obj = { [key]: val }
      const i = ini.stringify(obj)
        .replace(/\r\n/g, '\n') // normalizes output from ini.stringify
        .replace(/\n$/m, '')
        .replace(/^/g, '; ')
        .replace(/\n/g, '\n; ')
        .split('\n')
      return str + '\n' + i
    }, '')

    const tmpData = `;;;;
; npm ${where}config file: ${file}
; this is a simple ini-formatted file
; lines that start with semi-colons are comments
; run \`npm help 7 config\` for documentation of the various options
;
; Configs like \`@scope:registry\` map a scope to a given registry url.
;
; Configs like \`//<hostname>/:_authToken\` are auth that is restricted
; to the registry host specified.

${data.split('\n').sort(localeCompare).join('\n').trim()}

;;;;
; all available options shown below with default values
;;;;

${defData}
`.split('\n').join(EOL)
    await mkdir(dirname(file), { recursive: true })
    await writeFile(file, tmpData, 'utf8')
    await new Promise((res, rej) => {
      const [bin, ...args] = e.split(/\s+/)
      const editor = spawn(bin, [...args, file], { stdio: 'inherit' })
      editor.on('exit', (code) => {
        if (code) {
          return rej(new Error(`editor process exited with code: ${code}`))
        }
        return res()
      })
    })
  }

  async fix () {
    let problems

    try {
      this.npm.config.validate()
      return // if validate doesn't throw we have nothing to do
    } catch (err) {
      // coverage skipped because we don't need to test rethrowing errors
      // istanbul ignore next
      if (err.code !== 'ERR_INVALID_AUTH') {
        throw err
      }

      problems = err.problems
    }

    if (!this.npm.config.isDefault('location')) {
      problems = problems.filter((problem) => {
        return problem.where === this.npm.config.get('location')
      })
    }

    this.npm.config.repair(problems)
    const locations = []

    output.standard('The following configuration problems have been repaired:\n')
    const summary = problems.map(({ action, from, to, key, where }) => {
      // coverage disabled for else branch because it is intentionally omitted
      // istanbul ignore else
      if (action === 'rename') {
        // we keep track of which configs were modified here so we know what to save later
        locations.push(where)
        return `~ \`${from}\` renamed to \`${to}\` in ${where} config`
      } else if (action === 'delete') {
        locations.push(where)
        return `- \`${key}\` deleted from ${where} config`
      }
    }).join('\n')
    output.standard(summary)

    return await Promise.all(locations.map((location) => this.npm.config.save(location)))
  }

  async list () {
    const msg = []
    // long does not have a flattener
    const long = this.npm.config.get('long')
    for (const [where, { data, source }] of this.npm.config.data.entries()) {
      if (where === 'default' && !long) {
        continue
      }

      const entries = Object.entries(data).sort(([a], [b]) => localeCompare(a, b))
      if (!entries.length) {
        continue
      }

      msg.push(`; "${where}" config from ${source}`, '')
      for (const [k, v] of entries) {
        const display = displayVar(k, v)
        const src = this.npm.config.find(k)
        msg.push(src === where ? display : `; ${display} ; overridden by ${src}`)
        msg.push()
      }
      msg.push('')
    }

    if (!long) {
      msg.push(
        `; node bin location = ${process.execPath}`,
        `; node version = ${process.version}`,
        `; npm local prefix = ${this.npm.localPrefix}`,
        `; npm version = ${this.npm.version}`,
        `; cwd = ${process.cwd()}`,
        `; HOME = ${process.env.HOME}`,
        '; Run `npm config ls -l` to show all defaults.'
      )
      msg.push('')
    }

    if (!this.npm.global) {
      const { content } = await pkgJson.normalize(this.npm.prefix).catch(() => ({ content: {} }))

      if (content.publishConfig) {
        const pkgPath = resolve(this.npm.prefix, 'package.json')
        msg.push(`; "publishConfig" from ${pkgPath}`)
        msg.push('; This set of config values will be used at publish-time.', '')
        const entries = Object.entries(content.publishConfig)
          .sort(([a], [b]) => localeCompare(a, b))
        for (const [k, value] of entries) {
          msg.push(displayVar(k, value))
        }
        msg.push('')
      }
    }

    output.standard(msg.join('\n').trim())
  }

  async listJson () {
    const publicConf = {}
    for (const key in this.npm.config.list[0]) {
      const value = this.npm.config.get(key)
      if (isPrivate(key, value)) {
        continue
      }

      publicConf[key] = value
    }
    output.buffer(publicConf)
  }
}

module.exports = Config
