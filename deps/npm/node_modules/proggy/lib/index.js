exports.Client = require('./client.js')
exports.Tracker = require('./tracker.js')

const trackers = new Map()
exports.createTracker = (name, key, total) => {
  const tracker = new exports.Tracker(name, key, total)
  if (trackers.has(tracker.key)) {
    const msg = `proggy: duplicate progress id ${JSON.stringify(tracker.key)}`
    throw new Error(msg)
  }
  trackers.set(tracker.key, tracker)
  tracker.on('done', () => trackers.delete(tracker.key))
  return tracker
}
exports.createClient = (options = {}) => new exports.Client(options)
