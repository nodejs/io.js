// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This test might time out if the search space for a sequential
// interleaving becomes to large. However, it should never fail.
// Note that results of this test are flaky by design. While the test is
// deterministic with a fixed seed, bugs may introduce non-determinism.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const kDebug = false;

const kSequenceLength = 256;
const kNumberOfWorker = 4;
const kNumberOfSteps = 10000000;

const kFirstOpcodeWithInput = 3;
const kFirstOpcodeWithoutOutput = 3;
const kLastOpcodeWithoutOutput = 5;

const opCodes = [
  kExprI32AtomicLoad,     kExprI32AtomicLoad8U,     kExprI32AtomicLoad16U,
  kExprI32AtomicStore,    kExprI32AtomicStore8U,    kExprI32AtomicStore16U,
  kExprI32AtomicAdd,      kExprI32AtomicAdd8U,      kExprI32AtomicAdd16U,
  kExprI32AtomicSub,      kExprI32AtomicSub8U,      kExprI32AtomicSub16U,
  kExprI32AtomicAnd,      kExprI32AtomicAnd8U,      kExprI32AtomicAnd16U,
  kExprI32AtomicOr,       kExprI32AtomicOr8U,       kExprI32AtomicOr16U,
  kExprI32AtomicXor,      kExprI32AtomicXor8U,      kExprI32AtomicXor16U,
  kExprI32AtomicExchange, kExprI32AtomicExchange8U, kExprI32AtomicExchange16U
];

const opCodeNames = [
  'kExprI32AtomicLoad',       'kExprI32AtomicLoad8U',
  'kExprI32AtomicLoad16U',    'kExprI32AtomicStore',
  'kExprI32AtomicStore8U',    'kExprI32AtomicStore16U',
  'kExprI32AtomicAdd',        'kExprI32AtomicAdd8U',
  'kExprI32AtomicAdd16U',     'kExprI32AtomicSub',
  'kExprI32AtomicSub8U',      'kExprI32AtomicSub16U',
  'kExprI32AtomicAnd',        'kExprI32AtomicAnd8U',
  'kExprI32AtomicAnd16U',     'kExprI32AtomicOr',
  'kExprI32AtomicOr8U',       'kExprI32AtomicOr16U',
  'kExprI32AtomicXor',        'kExprI32AtomicXor8U',
  'kExprI32AtomicXor16U',     'kExprI32AtomicExchange',
  'kExprI32AtomicExchange8U', 'kExprI32AtomicExchange16U'
];

let kMaxMemPages = 10;
let gSharedMemory =
    new WebAssembly.Memory({initial: 1, maximum: kMaxMemPages, shared: true});
let gSharedMemoryView = new Int32Array(gSharedMemory.buffer);

let gPrivateMemory =
    new WebAssembly.Memory({initial: 1, maximum: kMaxMemPages, shared: true});
let gPrivateMemoryView = new Int32Array(gPrivateMemory.buffer);

class Operation {
  constructor(opcode, input, offset) {
    this.opcode = opcode != undefined ? opcode : Operation.nextOpcode();
    this.size = Operation.opcodeToSize(this.opcode);
    this.input = input != undefined ? input : Operation.inputForSize(this.size);
    this.offset =
        offset != undefined ? offset : Operation.offsetForSize(this.size);
  }

  static nextOpcode() {
    let random = Math.random();
    return Math.floor(random * opCodes.length);
  }

  static opcodeToSize(opcode) {
    // Instructions are ordered in 32, 8, 16 bits size
    return [32, 8, 16][opcode % 3];
  }

  static opcodeToAlignment(opcode) {
    // Instructions are ordered in 32, 8, 16 bits size
    return [2, 0, 1][opcode % 3];
  }

  static inputForSize(size) {
    let random = Math.random();
    // Avoid 32 bit overflow for integer here :(
    return Math.floor(random * (1 << (size - 1)) * 2);
  }

  static offsetForSize(size) {
    // Pick an offset in bytes between 0 and 7.
    let offset = Math.floor(Math.random() * 8);
    // Make sure the offset matches the required alignment by masking out the
    // lower bits.
    let size_in_bytes = size / 8;
    let mask = ~(size_in_bytes - 1);
    return offset & mask;
  }

  get wasmOpcode() {
    // [opcode, alignment, offset]
    return [
      opCodes[this.opcode], Operation.opcodeToAlignment(this.opcode),
      this.offset
    ];
  }

  get hasInput() {
    return this.opcode >= kFirstOpcodeWithInput;
  }

  get hasOutput() {
    return this.opcode < kFirstOpcodeWithoutOutput ||
        this.opcode > kLastOpcodeWithoutOutput;
  }

  truncateResultBits(low, high) {
    // Shift the lower part. For offsets greater four it drops out of the
    // visible window.
    let shiftedL = this.offset >= 4 ? 0 : low >>> (this.offset * 8);
    // The higher part is zero for offset 0, left shifted for [1..3] and right
    // shifted for [4..7].
    let shiftedH = this.offset == 0 ?
        0 :
        this.offset >= 4 ? high >>> (this.offset - 4) * 8 :
                           high << ((4 - this.offset) * 8);
    let value = shiftedL | shiftedH;

    switch (this.size) {
      case 8:
        return value & 0xFF;
      case 16:
        return value & 0xFFFF;
      case 32:
        return value;
      default:
        throw 'Unexpected size: ' + this.size;
    }
  }

  static get builder() {
    if (!Operation.__builder) {
      let builder = new WasmModuleBuilder();
      builder.addImportedMemory('m', 'imported_mem', 0, kMaxMemPages, 'shared');
      Operation.__builder = builder;
    }
    return Operation.__builder;
  }

  static get exports() {
    if (!Operation.__instance) {
      return {};
    }
    return Operation.__instance.exports;
  }

  static set instance(instance) {
    Operation.__instance = instance;
  }

  compute(state) {
    let evalFun = Operation.exports[this.key];
    if (!evalFun) {
      let builder = Operation.builder;
      let body = [
        // Load address of low 32 bits.
        kExprI32Const, 0,
        // Load expected value.
        kExprLocalGet, 0, kExprI32StoreMem, 2, 0,
        // Load address of high 32 bits.
        kExprI32Const, 4,
        // Load expected value.
        kExprLocalGet, 1, kExprI32StoreMem, 2, 0,
        // Load address of where our window starts.
        kExprI32Const, 0,
        // Load input if there is one.
        ...(this.hasInput ? [kExprLocalGet, 2] : []),
        // Perform operation.
        kAtomicPrefix, ...this.wasmOpcode,
        // Drop output if it had any.
        ...(this.hasOutput ? [kExprDrop] : []),
        // Load resulting value.
        kExprI32Const, 0, kExprI32LoadMem, 2, 0,
        // Return.
        kExprReturn
      ]
      builder.addFunction(this.key, kSig_i_iii)
          .addBody(body)
          .exportAs(this.key);
      // Instantiate module, get function exports.
      let module = new WebAssembly.Module(builder.toBuffer());
      Operation.instance =
          new WebAssembly.Instance(module, {m: {imported_mem: gPrivateMemory}});
      evalFun = Operation.exports[this.key];
    }
    let result = evalFun(state.low, state.high, this.input);
    let ta = gPrivateMemoryView;
    if (kDebug) {
      print(
          state.high + ':' + state.low + ' ' + this.toString() + ' -> ' +
          ta[1] + ':' + ta[0]);
    }
    if (result != ta[0]) throw '!';
    return {low: ta[0], high: ta[1]};
  }

  toString() {
    return opCodeNames[this.opcode] + '[+' + this.offset + '] ' + this.input;
  }

  get key() {
    return this.opcode + '-' + this.offset;
  }
}

class State {
  constructor(low, high, indices, count) {
    this.low = low;
    this.high = high;
    this.indices = indices;
    this.count = count;
  }

  isFinal() {
    return (this.count == kNumberOfWorker * kSequenceLength);
  }

  toString() {
    return this.high + ':' + this.low + ' @ ' + this.indices;
  }
}

function makeSequenceOfOperations(size) {
  let result = new Array(size);
  for (let i = 0; i < size; i++) {
    result[i] = new Operation();
  }
  return result;
}

function toSLeb128(val) {
  let result = [];
  while (true) {
    let v = val & 0x7f;
    val = val >> 7;
    let msbIsSet = (v & 0x40) || false;
    if (((val == 0) && !msbIsSet) || ((val == -1) && msbIsSet)) {
      result.push(v);
      break;
    }
    result.push(v | 0x80);
  }
  return result;
}

function generateFunctionBodyForSequence(sequence) {
  // We expect the int32* to perform ops on as arg 0 and
  // the int32* for our value log as arg1. Argument 2 gives
  // an int32* we use to count down spinning workers.
  let body = [];
  // Initially, we spin until all workers start running.
  if (!kDebug) {
    body.push(
        // Decrement the wait count.
        kExprLocalGet, 2, kExprI32Const, 1, kAtomicPrefix, kExprI32AtomicSub, 2,
        0,
        // Spin until zero.
        kExprLoop, kWasmVoid, kExprLocalGet, 2, kAtomicPrefix,
        kExprI32AtomicLoad, 2, 0, kExprI32Const, 0, kExprI32GtU, kExprBrIf, 0,
        kExprEnd);
  }
  for (let operation of sequence) {
    body.push(
        // Pre-load address of results sequence pointer for later.
        kExprLocalGet, 1,
        // Load address where atomic pointers are stored.
        kExprLocalGet, 0,
        // Load the second argument if it had any.
        ...(operation.hasInput ?
                [kExprI32Const, ...toSLeb128(operation.input)] :
                []),
        // Perform operation
        kAtomicPrefix, ...operation.wasmOpcode,
        // Generate fake output in needed.
        ...(operation.hasOutput ? [] : [kExprI32Const, 0]),
        // Store read intermediate to sequence.
        kExprI32StoreMem, 2, 0,
        // Increment result sequence pointer.
        kExprLocalGet, 1, kExprI32Const, 4, kExprI32Add, kExprLocalSet, 1);
  }
  // Return end of sequence index.
  body.push(kExprLocalGet, 1, kExprReturn);
  return body;
}

function getSequence(start, end) {
  return new Int32Array(
      gSharedMemory.buffer, start,
      (end - start) / Int32Array.BYTES_PER_ELEMENT);
}

function spawnWorkers() {
  function workerCode() {
    onmessage = function({data:msg}) {
      if (msg.module) {
        let module = msg.module;
        let mem = msg.mem;
        this.instance = new WebAssembly.Instance(module, {m: {imported_mem: mem}});
        postMessage({instantiated: true});
      } else {
        let address = msg.address;
        let sequence = msg.sequence;
        let index = msg.index;
        let spin = msg.spin;
        let result = instance.exports["worker" + index](address, sequence, spin);
        postMessage({index: index, sequence: sequence, result: result});
      }
    }
  }

  let workers = [];
  for (let i = 0; i < kNumberOfWorker; i++) {
    let worker = new Worker(workerCode, {type: 'function'});
    workers.push(worker);
  }
  return workers;
}

function instantiateModuleInWorkers(workers) {
  for (let worker of workers) {
    worker.postMessage({module: module, mem: gSharedMemory});
    let msg = worker.getMessage();
    if (!msg.instantiated) throw 'Worker failed to instantiate';
  }
}

function executeSequenceInWorkers(workers) {
  for (i = 0; i < workers.length; i++) {
    let worker = workers[i];
    worker.postMessage({
      index: i,
      address: 0,
      spin: 16,
      sequence: 32 + ((kSequenceLength * 4) + 32) * i
    });
    // In debug mode, keep execution sequential.
    if (kDebug) {
      let msg = worker.getMessage();
      results[msg.index] = getSequence(msg.sequence, msg.result);
    }
  }
}

function selectMatchingWorkers(state) {
  let matching = [];
  let indices = state.indices;
  for (let i = 0; i < indices.length; i++) {
    let index = indices[i];
    if (index >= kSequenceLength) continue;
    // We need to project the expected value to the number of bits this
    // operation will read at runtime.
    let expected =
        sequences[i][index].truncateResultBits(state.low, state.high);
    let hasOutput = sequences[i][index].hasOutput;
    if (!hasOutput || (results[i][index] == expected)) {
      matching.push(i);
    }
  }
  return matching;
}

function computeNextState(state, advanceIdx) {
  let newIndices = state.indices.slice();
  let sequence = sequences[advanceIdx];
  let operation = sequence[state.indices[advanceIdx]];
  newIndices[advanceIdx]++;
  let {low, high} = operation.compute(state);
  return new State(low, high, newIndices, state.count + 1);
}

function findSequentialOrdering() {
  let startIndices = new Array(results.length);
  let steps = 0;
  startIndices.fill(0);
  let matchingStates = [new State(0, 0, startIndices, 0)];
  while (matchingStates.length > 0) {
    let current = matchingStates.pop();
    if (kDebug) {
      print(current);
    }
    let matchingResults = selectMatchingWorkers(current);
    if (matchingResults.length == 0) {
      continue;
    }
    for (let match of matchingResults) {
      let newState = computeNextState(current, match);
      if (newState.isFinal()) {
        return true;
      }
      matchingStates.push(newState);
    }
    if (steps++ > kNumberOfSteps) {
      print('Search timed out, aborting...');
      return true;
    }
  }
  // We have no options left.
  return false;
}

// Helpful for debugging failed tests.
function loadSequencesFromStrings(inputs) {
  let reverseOpcodes = {};
  for (let i = 0; i < opCodeNames.length; i++) {
    reverseOpcodes[opCodeNames[i]] = i;
  }
  let sequences = [];
  let parseRE = /([a-zA-Z0-9]*)\[\+([0-9])\] ([\-0-9]*)/;
  for (let input of inputs) {
    let parts = input.split(',');
    let sequence = [];
    for (let part of parts) {
      let parsed = parseRE.exec(part);
      sequence.push(
          new Operation(reverseOpcodes[parsed[1]], parsed[3], parsed[2] | 0));
    }
    sequences.push(sequence);
  }
  return sequences;
}

// Helpful for debugging failed tests.
function loadResultsFromStrings(inputs) {
  let results = [];
  for (let input of inputs) {
    let parts = input.split(',');
    let result = [];
    for (let number of parts) {
      result.push(number | 0);
    }
    results.push(result);
  }
  return results;
}

let sequences = [];
let results = [];

let builder = new WasmModuleBuilder();
builder.addImportedMemory('m', 'imported_mem', 0, kMaxMemPages, 'shared');

for (let i = 0; i < kNumberOfWorker; i++) {
  sequences[i] = makeSequenceOfOperations(kSequenceLength);
  builder.addFunction('worker' + i, kSig_i_iii)
      .addBody(generateFunctionBodyForSequence(sequences[i]))
      .exportAs('worker' + i);
}

// Instantiate module, get function exports.
let module = new WebAssembly.Module(builder.toBuffer());
let instance =
    new WebAssembly.Instance(module, {m: {imported_mem: gSharedMemory}});

// Spawn off the workers and run the sequences.
let workers = spawnWorkers();
// Set spin count.
gSharedMemoryView[4] = kNumberOfWorker;
instantiateModuleInWorkers(workers);
executeSequenceInWorkers(workers);

if (!kDebug) {
  // Collect results, d8 style.
  for (let worker of workers) {
    let msg = worker.getMessage();
    results[msg.index] = getSequence(msg.sequence, msg.result);
  }
}

// Terminate all workers.
for (let worker of workers) {
  worker.terminate();
}

// In debug mode, print sequences and results.
if (kDebug) {
  for (let result of results) {
    print(result);
  }

  for (let sequence of sequences) {
    print(sequence);
  }
}

// Try to reconstruct a sequential ordering.
let passed = findSequentialOrdering();

if (passed) {
  print('PASS');
} else {
  for (let i = 0; i < kNumberOfWorker; i++) {
    print('Worker ' + i);
    print(sequences[i]);
    print(results[i]);
  }
  print('FAIL');
  quit(-1);
}
