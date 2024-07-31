/* eslint-disable max-len */
const { stripVTControlCharacters: strip } = require('node:util')
const { Minipass } = require('minipass')

// This module consumes package data in the following format:
//
// {
//   name: String,
//   description: String,
//   maintainers: [{ username: String, email: String }],
//   keywords: String | [String],
//   version: String,
//   date: Date // can be null,
// }
//
// The returned stream will format this package data
// into a byte stream of formatted, displayable output.

function filter (data, exclude) {
  const words = [data.name]
    .concat(data.maintainers.map(m => m.username))
    .concat(data.keywords || [])
    .map(f => f?.trim?.())
    .filter(Boolean)
    .join(' ')
    .toLowerCase()

  if (exclude.find(pattern => {
    // Treats both /foo and /foo/ as regex searches
    if (pattern.startsWith('/')) {
      if (pattern.endsWith('/')) {
        pattern = pattern.slice(0, -1)
      }
      return words.match(new RegExp(pattern.slice(1)))
    }
    return words.includes(pattern)
  })) {
    return false
  }

  return true
}

module.exports = (opts) => {
  return opts.json ? new JSONOutputStream(opts) : new TextOutputStream(opts)
}

class JSONOutputStream extends Minipass {
  #didFirst = false
  #exclude

  constructor (opts) {
    super()
    this.#exclude = opts.exclude
  }

  write (obj) {
    if (!filter(obj, this.#exclude)) {
      return
    }
    if (!this.#didFirst) {
      super.write('[\n')
      this.#didFirst = true
    } else {
      super.write('\n,\n')
    }

    return super.write(JSON.stringify(obj))
  }

  end () {
    super.write(this.#didFirst ? ']\n' : '\n[]\n')
    super.end()
  }
}

class TextOutputStream extends Minipass {
  #args
  #chalk
  #exclude
  #parseable

  constructor (opts) {
    super()
    this.#args = opts.args.map(s => s.toLowerCase()).filter(Boolean)
    this.#chalk = opts.npm.chalk
    this.#exclude = opts.exclude
    this.#parseable = opts.parseable
  }

  write (data) {
    if (!filter(data, this.#exclude)) {
      return
    }
    // Normalize
    const pkg = {
      authors: data.maintainers.map((m) => `${strip(m.username)}`).join(' '),
      publisher: strip(data.publisher?.username || ''),
      date: data.date ? data.date.toISOString().slice(0, 10) : 'prehistoric',
      description: strip(data.description ?? ''),
      keywords: [],
      name: strip(data.name),
      version: data.version,
    }
    if (Array.isArray(data.keywords)) {
      pkg.keywords = data.keywords.map(strip)
    } else if (typeof data.keywords === 'string') {
      pkg.keywords = strip(data.keywords.replace(/[,\s]+/, ' ')).split(' ')
    }

    let output
    if (this.#parseable) {
      output = [pkg.name, pkg.description, pkg.author, pkg.date, pkg.version, pkg.keywords]
        .filter(Boolean)
        .map(col => ('' + col).replace(/\t/g, ' ')).join('\t')
      return super.write(output)
    }

    const keywords = pkg.keywords.map(k => {
      if (this.#args.includes(k)) {
        return this.#chalk.cyan(k)
      } else {
        return k
      }
    }).join(' ')

    let description = []
    for (const arg of this.#args) {
      const finder = pkg.description.toLowerCase().split(arg.toLowerCase())
      let p = 0
      for (const f of finder) {
        description.push(pkg.description.slice(p, p + f.length))
        const word = pkg.description.slice(p + f.length, p + f.length + arg.length)
        description.push(this.#chalk.cyan(word))
        p += f.length + arg.length
      }
    }
    description = description.filter(Boolean)
    let name = pkg.name
    if (this.#args.includes(pkg.name)) {
      name = this.#chalk.cyan(pkg.name)
    } else {
      name = []
      for (const arg of this.#args) {
        const finder = pkg.name.toLowerCase().split(arg.toLowerCase())
        let p = 0
        for (const f of finder) {
          name.push(pkg.name.slice(p, p + f.length))
          const word = pkg.name.slice(p + f.length, p + f.length + arg.length)
          name.push(this.#chalk.cyan(word))
          p += f.length + arg.length
        }
      }
      name = this.#chalk.blue(name.join(''))
    }

    if (description.length) {
      output = `${name}\n${description.join('')}\n`
    } else {
      output = `${name}\n`
    }
    if (pkg.publisher) {
      output += `Version ${this.#chalk.blue(pkg.version)} published ${this.#chalk.blue(pkg.date)} by ${this.#chalk.blue(pkg.publisher)}\n`
    } else {
      output += `Version ${this.#chalk.blue(pkg.version)} published ${this.#chalk.blue(pkg.date)} by ${this.#chalk.yellow('???')}\n`
    }
    output += `Maintainers: ${pkg.authors}\n`
    if (keywords) {
      output += `Keywords: ${keywords}\n`
    }
    output += `${this.#chalk.blue(`https://npm.im/${pkg.name}`)}\n`
    return super.write(output)
  }
}
