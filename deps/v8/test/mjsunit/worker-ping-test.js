// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A test utility for pinging objects back and forth among a pool of workers.
// Use by calling {RunWorkerPingTest} with a {config} object.
{
// Reference config object for demonstrating the interface.
let config = {
  numThings: 4,       // size of circular buffer
  numWorkers: 4,      // number of workers
  numMessages: 100,   // number of messages sent to each worker
  allocInterval: 11,  // interval for allocating new things per worker
  traceScript: false, // print the script
  traceAlloc: false,  // print each allocation attempt
  traceIteration: 10, // print diagnostics every so many iterations
  abortOnFail: false, // kill worker if allocation fails

  // Note that because the functions are appended to a worker script
  // *as source*, they need to be named properly.

  // The function that allocates things. Required.
  AllocThing: function AllocThing(id) {
    return new Array(2);
  },
  // Before message send behavior. Optional.
  BeforeSend: function BeforeSend(msg) { },
  // Before message reception behavior. Optional.
  BeforeReceive: function BeforeReceive(msg) { },
}
}

function RunWorkerPingTest(config) {
  let workers = [];
  let beforeSend = (typeof config.BeforeSend == "function") ?
      config.BeforeSend :
      function BeforeSend(msg) { };
  let beforeReceive = (typeof config.BeforeReceive == "function") ?
      config.BeforeReceive :
      function BeforeReceive(msg) { };

  // Each worker has a circular buffer of size {config.numThings}, recording
  // received things into the buffer and responding with a previous thing.
  // Every {config.allocInterval}, a worker creates a new thing by
  // {config.AllocThing}.

  function workerCode(config, AllocThing, BeforeReceive) {
    eval(AllocThing);
    eval(BeforeReceive);
    const kNumThings = config.numThings;
    const kAllocInterval = config.allocInterval;
    let index = 0;
    let total = 0;
    let id = 0;
    let things = new Array(kNumThings);
    for (let i = 0; i < kNumThings; i++) {
      things[i] = TryAllocThing();
    }

    function TryAllocThing() {
      try {
        let thing = AllocThing(id++);
        if (config.traceAlloc) {
          print("alloc success");
        }
       return thing;
     } catch(e) {
       if (config.abortOnFail) {
         postMessage({error: e.toString()}); throw e;
       }
       if (config.traceAlloc) {
         print("alloc fail: " + e);
       }
     }
    }

    onmessage = function({data:msg}) {
      BeforeReceive(msg);
      if (msg.thing !== undefined) {
        let reply = things[index];
        if ((total % kAllocInterval) == 0) {
          reply = TryAllocThing();
        }
        things[index] = msg.thing;
        postMessage({thing : reply});
        index = (index + 1) % kNumThings;
        total++;
      }
    }
  }

  if (config.traceScript) {
    print("========== Worker script ==========");
    print(workerCode.toString());
    print("===================================");
  }

  let arguments = [config,
                   config.AllocThing.toString(),
                   beforeReceive.toString()];
  for (let i = 0; i < config.numWorkers; i++) {
    let worker = new Worker(workerCode, {type: 'function',
                                         arguments: arguments});
    workers.push(worker);
  }

  let time = performance.now();

  // The main thread posts {config.numMessages} messages to {config.numWorkers}
  // workers, with each message containing a "thing" created by {config.AllocThing}.
  let thing = config.AllocThing(-1);
  for (let i = 0; i < config.numMessages; i++) {
    if ((i % config.traceIteration) == 0) {
      let now = performance.now();
      print(`iteration ${i}, Δ = ${(now - time).toFixed(3)} ms`);
      time = now;
    }

    for (let worker of workers) {
      let msg = {thing: thing};
      beforeSend(msg);
      worker.postMessage(msg);
      msg = worker.getMessage();
      if (msg.thing) {
        thing = msg.thing;
      } else if (msg.error) {
        print('Error in worker:', msg.error);
        worker.terminate();
        throw msg.error;
      }
    }
  }
  print('Terminating workers.');
  for (let worker of workers) {
    worker.terminate();
  }
  print('Workers terminated.');
}
