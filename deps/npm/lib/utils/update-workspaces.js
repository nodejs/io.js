'use strict'

const reifyFinish = require('../utils/reify-finish.js')

async function updateWorkspaces ({
  config,
  flatOptions,
  localPrefix,
  npm,
  workspaces,
}) {
  if (!flatOptions.workspacesUpdate || !workspaces.length) {
    return
  }

  // default behavior is to not save by default in order to avoid
  // race condition problems when publishing multiple workspaces
  // that have dependencies on one another, it might still be useful
  // in some cases, which then need to set --save
  const save = config.isDefault('save')
    ? false
    : config.get('save')

  // runs a minimalistic reify update, targeting only the workspaces
  // that had version updates and skipping fund/audit/save
  const opts = {
    ...flatOptions,
    audit: false,
    fund: false,
    path: localPrefix,
    save,
  }
  const Arborist = require('@npmcli/arborist')
  const arb = new Arborist(opts)

  await arb.reify({ ...opts, update: workspaces })
  await reifyFinish(npm, arb)
}

module.exports = updateWorkspaces
