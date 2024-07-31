'use strict';

const {
  constants: {
    NODE_PERFORMANCE_MILESTONE_TIME_ORIGIN,
    NODE_PERFORMANCE_MILESTONE_TIME_ORIGIN_TIMESTAMP,
  },
  milestones,
  now,
} = internalBinding('performance');

function getTimeOrigin() {
  // Do not cache this to prevent it from being serialized into the
  // snapshot.
  return milestones[NODE_PERFORMANCE_MILESTONE_TIME_ORIGIN] / 1e6;
}

// Returns the milestone relative to the process start time in milliseconds.
function getMilestoneTimestamp(milestoneIdx) {
  const ns = milestones[milestoneIdx];
  if (ns === -1)
    return ns;
  return ns / 1e6 - getTimeOrigin();
}

function getTimeOriginTimestamp() {
  return milestones[NODE_PERFORMANCE_MILESTONE_TIME_ORIGIN_TIMESTAMP] / 1e3;
}

module.exports = {
  now,
  getMilestoneTimestamp,
  getTimeOriginTimestamp,
};
