// ping the npm registry
// used by the ping and doctor commands
const npmFetch = require('npm-registry-fetch')
module.exports = async (flatOptions) => {
  const res = await npmFetch('/-/ping', { ...flatOptions, cache: false })
  return res.json().catch(() => ({}))
}
