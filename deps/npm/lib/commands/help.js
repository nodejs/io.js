const spawn = require('@npmcli/promise-spawn')
const path = require('node:path')
const { openUrl } = require('../utils/open-url.js')
const { glob } = require('glob')
const { output, input } = require('proc-log')
const localeCompare = require('@isaacs/string-locale-compare')('en')
const { deref } = require('../utils/cmd-list.js')
const BaseCommand = require('../base-cmd.js')

const globify = pattern => pattern.split('\\').join('/')

// Strips out the number from foo.7 or foo.7. or foo.7.tgz
// We don't currently compress our man pages but if we ever did this would
// seamlessly continue supporting it
const manNumberRegex = /\.(\d+)(\.[^/\\]*)?$/
// hardcoded names for mansections
// XXX: these are used in the docs workspace and should be exported
// from npm so section names can changed more easily
const manSectionNames = {
  1: 'commands',
  5: 'configuring-npm',
  7: 'using-npm',
}

class Help extends BaseCommand {
  static description = 'Get help on npm'
  static name = 'help'
  static usage = ['<term> [<terms..>]']
  static params = ['viewer']

  static async completion (opts, npm) {
    if (opts.conf.argv.remain.length > 2) {
      return []
    }
    const g = path.resolve(npm.npmRoot, 'man/man[0-9]/*.[0-9]')
    let files = await glob(globify(g))
    // preserve glob@8 behavior
    files = files.sort((a, b) => a.localeCompare(b, 'en'))

    return Object.keys(files.reduce(function (acc, file) {
      file = path.basename(file).replace(/\.[0-9]+$/, '')
      file = file.replace(/^npm-/, '')
      acc[file] = true
      return acc
    }, { help: true }))
  }

  async exec (args) {
    // By default we search all of our man subdirectories, but if the user has
    // asked for a specific one we limit the search to just there
    const manSearch = /^\d+$/.test(args[0]) ? `man${args.shift()}` : 'man*'

    if (!args.length) {
      return output.standard(this.npm.usage)
    }

    // npm help foo bar baz: search topics
    if (args.length > 1) {
      return this.helpSearch(args)
    }

    // `npm help package.json`
    const arg = (deref(args[0]) || args[0]).replace('.json', '-json')

    // find either section.n or npm-section.n
    const f = globify(path.resolve(this.npm.npmRoot, `man/${manSearch}/?(npm-)${arg}.[0-9]*`))

    const [man] = await glob(f).then(r => r.sort((a, b) => {
      // Because the glob is (subtly) different from manNumberRegex,
      // we can't rely on it passing.
      const aManNumberMatch = a.match(manNumberRegex)?.[1] || 999
      const bManNumberMatch = b.match(manNumberRegex)?.[1] || 999
      if (aManNumberMatch !== bManNumberMatch) {
        return aManNumberMatch - bManNumberMatch
      }
      return localeCompare(a, b)
    }))

    return man ? this.viewMan(man) : this.helpSearch(args)
  }

  helpSearch (args) {
    return this.npm.exec('help-search', args)
  }

  async viewMan (man) {
    const viewer = this.npm.config.get('viewer')

    if (viewer === 'browser') {
      return openUrl(this.npm, this.htmlMan(man), 'help available at the following URL', true)
    }

    let args = ['man', [man]]
    if (viewer === 'woman') {
      args = ['emacsclient', ['-e', `(woman-find-file '${man}')`]]
    }

    try {
      await input.start(() => spawn(...args, { stdio: 'inherit' }))
    } catch (err) {
      if (err.code) {
        throw new Error(`help process exited with code: ${err.code}`)
      } else {
        throw err
      }
    }
  }

  // Returns the path to the html version of the man page
  htmlMan (man) {
    const sect = manSectionNames[man.match(manNumberRegex)[1]]
    const f = path.basename(man).replace(manNumberRegex, '')
    return 'file:///' + path.resolve(this.npm.npmRoot, `docs/output/${sect}/${f}.html`)
  }
}

module.exports = Help
