// a class to manage an inventory and set of indexes of a set of objects based
// on specific fields.
const { hasOwnProperty } = Object.prototype
const debug = require('./debug.js')

const keys = ['name', 'license', 'funding', 'realpath', 'packageName']
class Inventory extends Map {
  #index

  constructor () {
    super()
    this.#index = new Map()
    for (const key of keys) {
      this.#index.set(key, new Map())
    }
  }

  // XXX where is this used?
  get primaryKey () {
    return 'location'
  }

  // XXX where is this used?
  get indexes () {
    return [...keys]
  }

  * filter (fn) {
    for (const node of this.values()) {
      if (fn(node)) {
        yield node
      }
    }
  }

  add (node) {
    const root = super.get('')
    if (root && node.root !== root && node.root !== root.root) {
      debug(() => {
        throw Object.assign(new Error('adding external node to inventory'), {
          root: root.path,
          node: node.path,
          nodeRoot: node.root.path,
        })
      })
      return
    }

    const current = super.get(node.location)
    if (current) {
      if (current === node) {
        return
      }
      this.delete(current)
    }
    super.set(node.location, node)
    for (const [key, map] of this.#index.entries()) {
      let val
      if (hasOwnProperty.call(node, key)) {
        // if the node has the value, use it even if it's false
        val = node[key]
      } else if (key === 'license' && node.package) {
        // handling for the outdated "licenses" array, just pick the first one
        // also support the alternative spelling "licence"
        if (node.package.license) {
          val = node.package.license
        } else if (node.package.licence) {
          val = node.package.licence
        } else if (Array.isArray(node.package.licenses)) {
          val = node.package.licenses[0]
        } else if (Array.isArray(node.package.licences)) {
          val = node.package.licences[0]
        }
      } else if (node[key]) {
        val = node[key]
      } else {
        val = node.package?.[key]
      }
      if (val && typeof val === 'object') {
        // We currently only use license and funding
        /* istanbul ignore next - not used */
        if (key === 'license') {
          val = val.type
        } else if (key === 'funding') {
          val = val.url
        }
      }
      if (!map.has(val)) {
        map.set(val, new Set())
      }
      map.get(val).add(node)
    }
  }

  delete (node) {
    if (!this.has(node)) {
      return
    }

    super.delete(node.location)
    for (const [key, map] of this.#index.entries()) {
      let val
      if (node[key] !== undefined) {
        val = node[key]
      } else {
        val = node.package?.[key]
      }
      const set = map.get(val)
      if (set) {
        set.delete(node)
        if (set.size === 0) {
          map.delete(node[key])
        }
      }
    }
  }

  query (key, val) {
    const map = this.#index.get(key)
    if (arguments.length === 2) {
      if (map.has(val)) {
        return map.get(val)
      }
      return new Set()
    }
    return map.keys()
  }

  has (node) {
    return super.get(node.location) === node
  }

  set () {
    throw new Error('direct set() not supported, use inventory.add(node)')
  }
}

module.exports = Inventory
