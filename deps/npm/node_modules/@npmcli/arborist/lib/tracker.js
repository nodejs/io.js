const proggy = require('proggy')

module.exports = cls => class Tracker extends cls {
  #progress = new Map()

  #createTracker (key, name) {
    const tracker = new proggy.Tracker(name ?? key)
    tracker.on('done', () => this.#progress.delete(key))
    this.#progress.set(key, tracker)
  }

  addTracker (section, subsection = null, key = null) {
    if (section === null || section === undefined) {
      this.#onError(`Tracker can't be null or undefined`)
    }

    if (key === null) {
      key = subsection
    }

    const hasTracker = this.#progress.has(section)
    const hasSubtracker = this.#progress.has(`${section}:${key}`)

    if (hasTracker && subsection === null) {
      // 0. existing tracker, no subsection
      this.#onError(`Tracker "${section}" already exists`)
    } else if (!hasTracker && subsection === null) {
      // 1. no existing tracker, no subsection
      // Create a new progress tracker
      this.#createTracker(section)
    } else if (!hasTracker && subsection !== null) {
      // 2. no parent tracker and subsection
      this.#onError(`Parent tracker "${section}" does not exist`)
    } else if (!hasTracker || !hasSubtracker) {
      // 3. existing parent tracker, no subsection tracker
      // Create a new subtracker and update parents
      const parentTracker = this.#progress.get(section)
      parentTracker.update(parentTracker.value, parentTracker.total + 1)
      this.#createTracker(`${section}:${key}`, `${section}:${subsection}`)
    }
    // 4. existing parent tracker, existing subsection tracker
    // skip it
  }

  finishTracker (section, subsection = null, key = null) {
    if (section === null || section === undefined) {
      this.#onError(`Tracker can't be null or undefined`)
    }

    if (key === null) {
      key = subsection
    }

    const hasTracker = this.#progress.has(section)
    const hasSubtracker = this.#progress.has(`${section}:${key}`)

    // 0. parent tracker exists, no subsection
    // Finish parent tracker and remove from this.#progress
    if (hasTracker && subsection === null) {
      // check if parent tracker does
      // not have any remaining children
      const keys = this.#progress.keys()
      for (const key of keys) {
        if (key.match(new RegExp(section + ':'))) {
          this.finishTracker(section, key)
        }
      }
      // remove parent tracker
      this.#progress.get(section).finish()
    } else if (!hasTracker && subsection === null) {
      // 1. no existing parent tracker, no subsection
      this.#onError(`Tracker "${section}" does not exist`)
    } else if (!hasTracker || hasSubtracker) {
      // 2. subtracker exists
      // Finish subtracker and remove from this.#progress
      const parentTracker = this.#progress.get(section)
      parentTracker.update(parentTracker.value + 1)
      this.#progress.get(`${section}:${key}`).finish()
    }
    // 3. existing parent tracker, no subsection
  }

  #onError (msg) {
    throw new Error(msg)
  }
}
