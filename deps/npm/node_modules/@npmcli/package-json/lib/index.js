const { readFile, writeFile } = require('node:fs/promises')
const { resolve } = require('node:path')
const parseJSON = require('json-parse-even-better-errors')

const updateDeps = require('./update-dependencies.js')
const updateScripts = require('./update-scripts.js')
const updateWorkspaces = require('./update-workspaces.js')
const normalize = require('./normalize.js')
const { read, parse } = require('./read-package.js')
const { packageSort } = require('./sort.js')

// a list of handy specialized helper functions that take
// care of special cases that are handled by the npm cli
const knownSteps = new Set([
  updateDeps,
  updateScripts,
  updateWorkspaces,
])

// list of all keys that are handled by "knownSteps" helpers
const knownKeys = new Set([
  ...updateDeps.knownKeys,
  'scripts',
  'workspaces',
])

class PackageJson {
  static normalizeSteps = Object.freeze([
    '_id',
    '_attributes',
    'bundledDependencies',
    'bundleDependencies',
    'optionalDedupe',
    'scripts',
    'funding',
    'bin',
  ])

  // npm pkg fix
  static fixSteps = Object.freeze([
    'binRefs',
    'bundleDependencies',
    'bundleDependenciesFalse',
    'fixNameField',
    'fixVersionField',
    'fixRepositoryField',
    'fixDependencies',
    'devDependencies',
    'scriptpath',
  ])

  static prepareSteps = Object.freeze([
    '_id',
    '_attributes',
    'bundledDependencies',
    'bundleDependencies',
    'bundleDependenciesDeleteFalse',
    'gypfile',
    'serverjs',
    'scriptpath',
    'authors',
    'readme',
    'mans',
    'binDir',
    'gitHead',
    'fillTypes',
    'normalizeData',
    'binRefs',
  ])

  // create a new empty package.json, so we can save at the given path even
  // though we didn't start from a parsed file
  static async create (path, opts = {}) {
    const p = new PackageJson()
    await p.create(path)
    if (opts.data) {
      return p.update(opts.data)
    }
    return p
  }

  // Loads a package.json at given path and JSON parses
  static async load (path, opts = {}) {
    const p = new PackageJson()
    // Avoid try/catch if we aren't going to create
    if (!opts.create) {
      return p.load(path)
    }

    try {
      return await p.load(path)
    } catch (err) {
      if (!err.message.startsWith('Could not read package.json')) {
        throw err
      }
      return await p.create(path)
    }
  }

  // npm pkg fix
  static async fix (path, opts) {
    const p = new PackageJson()
    await p.load(path, true)
    return p.fix(opts)
  }

  // read-package-json compatible behavior
  static async prepare (path, opts) {
    const p = new PackageJson()
    await p.load(path, true)
    return p.prepare(opts)
  }

  // read-package-json-fast compatible behavior
  static async normalize (path, opts) {
    const p = new PackageJson()
    await p.load(path)
    return p.normalize(opts)
  }

  #path
  #manifest
  #readFileContent = ''
  #canSave = true

  // Load content from given path
  async load (path, parseIndex) {
    this.#path = path
    let parseErr
    try {
      this.#readFileContent = await read(this.filename)
    } catch (err) {
      if (!parseIndex) {
        throw err
      }
      parseErr = err
    }

    if (parseErr) {
      const indexFile = resolve(this.path, 'index.js')
      let indexFileContent
      try {
        indexFileContent = await readFile(indexFile, 'utf8')
      } catch (err) {
        throw parseErr
      }
      try {
        this.fromComment(indexFileContent)
      } catch (err) {
        throw parseErr
      }
      // This wasn't a package.json so prevent saving
      this.#canSave = false
      return this
    }

    return this.fromJSON(this.#readFileContent)
  }

  // Load data from a JSON string/buffer
  fromJSON (data) {
    this.#manifest = parse(data)
    return this
  }

  fromContent (data) {
    this.#manifest = data
    this.#canSave = false
    return this
  }

  // Load data from a comment
  // /**package { "name": "foo", "version": "1.2.3", ... } **/
  fromComment (data) {
    data = data.split(/^\/\*\*package(?:\s|$)/m)

    if (data.length < 2) {
      throw new Error('File has no package in comments')
    }
    data = data[1]
    data = data.split(/\*\*\/$/m)

    if (data.length < 2) {
      throw new Error('File has no package in comments')
    }
    data = data[0]
    data = data.replace(/^\s*\*/mg, '')

    this.#manifest = parseJSON(data)
    return this
  }

  get content () {
    return this.#manifest
  }

  get path () {
    return this.#path
  }

  get filename () {
    if (this.path) {
      return resolve(this.path, 'package.json')
    }
    return undefined
  }

  create (path) {
    this.#path = path
    this.#manifest = {}
    return this
  }

  // This should be the ONLY way to set content in the manifest
  update (content) {
    if (!this.content) {
      throw new Error('Can not update without content.  Please `load` or `create`')
    }

    for (const step of knownSteps) {
      this.#manifest = step({ content, originalContent: this.content })
    }

    // unknown properties will just be overwitten
    for (const [key, value] of Object.entries(content)) {
      if (!knownKeys.has(key)) {
        this.content[key] = value
      }
    }

    return this
  }

  async save ({ sort } = {}) {
    if (!this.#canSave) {
      throw new Error('No package.json to save to')
    }
    const {
      [Symbol.for('indent')]: indent,
      [Symbol.for('newline')]: newline,
      ...rest
    } = this.content

    const format = indent === undefined ? '  ' : indent
    const eol = newline === undefined ? '\n' : newline

    const content = sort ? packageSort(rest) : rest

    const fileContent = `${
      JSON.stringify(content, null, format)
    }\n`
      .replace(/\n/g, eol)

    if (fileContent.trim() !== this.#readFileContent.trim()) {
      const written = await writeFile(this.filename, fileContent)
      this.#readFileContent = fileContent
      return written
    }
  }

  async normalize (opts = {}) {
    if (!opts.steps) {
      opts.steps = this.constructor.normalizeSteps
    }
    await normalize(this, opts)
    return this
  }

  async prepare (opts = {}) {
    if (!opts.steps) {
      opts.steps = this.constructor.prepareSteps
    }
    await normalize(this, opts)
    return this
  }

  async fix (opts = {}) {
    // This one is not overridable
    opts.steps = this.constructor.fixSteps
    await normalize(this, opts)
    return this
  }
}

module.exports = PackageJson
