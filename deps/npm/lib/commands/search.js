const Pipeline = require('minipass-pipeline')
const libSearch = require('libnpmsearch')
const { log, output } = require('proc-log')
const formatSearchStream = require('../utils/format-search-stream.js')
const BaseCommand = require('../base-cmd.js')

class Search extends BaseCommand {
  static description = 'Search for packages'
  static name = 'search'
  static params = [
    'json',
    'color',
    'parseable',
    'description',
    'searchlimit',
    'searchopts',
    'searchexclude',
    'registry',
    'prefer-online',
    'prefer-offline',
    'offline',
  ]

  static usage = ['<search term> [<search term> ...]']

  async exec (args) {
    const opts = {
      ...this.npm.flatOptions,
      ...this.npm.flatOptions.search,
      include: args.map(s => s.toLowerCase()).filter(Boolean),
      exclude: this.npm.flatOptions.search.exclude.split(/\s+/),
    }

    if (opts.include.length === 0) {
      throw new Error('search must be called with arguments')
    }

    // Used later to figure out whether we had any packages go out
    let anyOutput = false

    // Grab a configured output stream that will spit out packages in the desired format.
    const outputStream = formatSearchStream({
      args, // --searchinclude options are not highlighted
      ...opts,
      npm: this.npm,
    })

    log.silly('search', 'searching packages')
    const p = new Pipeline(
      libSearch.stream(opts.include, opts),
      outputStream
    )

    p.on('data', chunk => {
      if (!anyOutput) {
        anyOutput = true
      }
      output.standard(chunk.toString('utf8'))
    })

    await p.promise()
    if (!anyOutput && !this.npm.config.get('json') && !this.npm.config.get('parseable')) {
      output.standard('No matches found for ' + (args.map(JSON.stringify).join(' ')))
    }

    log.silly('search', 'search completed')
  }
}

module.exports = Search
