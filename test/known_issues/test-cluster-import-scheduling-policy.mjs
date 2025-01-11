// Refs: https://github.com/nodejs/node/issues/49240
// When importing cluster in ESM, cluster.schedulingPolicy cannot be set;
// and the env variable doesn't work since imports are hoisted to the top.
// Therefore the scheduling policy is still cluster.SCHED_RR.
import '../common/index.mjs';
import assert from 'node:assert';
import * as cluster from 'cluster';


assert.strictEqual(cluster.schedulingPolicy, cluster.SCHED_RR);
cluster.setupPrimary({ schedulingPolicy: cluster.SCHED_NONE });
const settings = cluster.getSettings();
assert.strictEqual(settings.schedulingPolicy, cluster.SCHED_RR);
