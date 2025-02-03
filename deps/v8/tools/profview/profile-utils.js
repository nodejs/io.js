// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

"use strict";

let codeKinds = [
    "UNKNOWN",
    "CPP_PARSE",
    "CPP_COMP_BC",
    "CPP_COMP_BASELINE",
    "CPP_COMP",
    "CPP_GC",
    "CPP_EXT",
    "CPP_LOGGING",
    "CPP",
    "LIB",
    "IC",
    "BC",
    "STUB",
    "BUILTIN",
    "REGEXP",
    "JS_IGNITION",
    "JS_SPARKPLUG",
    "JS_MAGLEV",
    "JS_TURBOFAN",
];

function resolveCodeKind(code) {
  if (!code || !code.type) {
    return "UNKNOWN";
  }
  const type = code.type;
  if (type === "CPP") {
    return "CPP";
  } else if (type === "SHARED_LIB") {
    return "LIB";
  }
  const kind = code.kind;
  if (type === "CODE") {
    if (kind === "LoadIC" ||
        kind === "StoreIC" ||
        kind === "KeyedStoreIC" ||
        kind === "KeyedLoadIC" ||
        kind === "LoadGlobalIC" ||
        kind === "Handler") {
      return "IC";
    } else if (kind === "BytecodeHandler") {
      return "BC";
    } else if (kind === "Stub") {
      return "STUB";
    } else if (kind === "Builtin") {
      return "BUILTIN";
    } else if (kind === "RegExp") {
      return "REGEXP";
    }
    console.warn("Unknown CODE: '" + kind + "'.");
    return "CODE";
  } else if (type === "JS") {
    if (kind === "Builtin" || kind == "Ignition" || kind === "Unopt") {
      return "JS_IGNITION";
    } else if (kind === "Baseline" || kind === "Sparkplug") {
      return "JS_SPARKPLUG";
    } else if (kind === "Maglev") {
      return "JS_MAGLEV";
    } else if (kind === "Opt" || kind === "Turbofan") {
      return "JS_TURBOFAN";
    }
  }
  console.warn("Unknown code type '" + kind + "'.");
}

function resolveCodeKindAndVmState(code, vmState) {
  let kind = resolveCodeKind(code);
  if (kind === "CPP") {
    if (vmState === 1) {
      kind = "CPP_GC";
    } else if (vmState === 2) {
      kind = "CPP_PARSE";
    } else if (vmState === 3) {
      kind = "CPP_COMP_BC";
    } else if (vmState === 4) {
      kind = "CPP_COMP";
    } else if (vmState === 6) {
      kind = "CPP_EXT";
    } else if (vmState === 9) {
      kind = "CPP_LOGGING";
    }
    // TODO(cbruni): add CPP_COMP_BASELINE
  }
  return kind;
}

function codeEquals(code1, code2, allowDifferentKinds = false) {
  if (!code1 || !code2) return false;
  if (code1.name !== code2.name || code1.type !== code2.type) return false;

  if (code1.type === 'CODE') {
    if (!allowDifferentKinds && code1.kind !== code2.kind) return false;
  } else if (code1.type === 'JS') {
    if (!allowDifferentKinds && code1.kind !== code2.kind) return false;
    if (code1.func !== code2.func) return false;
  }
  return true;
}

function createNodeFromStackEntry(code, codeId, vmState) {
  let name = code ? code.name : "UNKNOWN";
  let node = createEmptyNode(name);
  node.codeId = codeId;
  node.type = resolveCodeKindAndVmState(code, vmState);
  return node;
}

function childIdFromCode(codeId, code) {
  // For JavaScript function, pretend there is one instance of optimized
  // function and one instance of unoptimized function per SFI.
  // Otherwise, just compute the id from code id.
  let type = resolveCodeKind(code);
  if (type === "JSOPT") {
    return code.func * 4 + 1;
  } else if (type === "JSUNOPT") {
    return code.func * 4 + 2;
  } else {
    return codeId * 4;
  }
}

// We store list of ticks and positions within the ticks stack by
// storing flattened triplets of { tickIndex, depth, count }.
// Triplet { 123, 2, 3 } encodes positions in ticks 123, 124, 125,
// all of them at depth 2. The flattened array is used to encode
// position within the call-tree.

// The following function helps to encode such triplets.
function addFrameToFrameList(paths, pathIndex, depth) {
  // Try to combine with the previous code run.
  if (paths.length > 0 &&
      paths[paths.length - 3] + 1 === pathIndex &&
      paths[paths.length - 2] === depth) {
    paths[paths.length - 1]++;
  } else {
    paths.push(pathIndex, depth, 1);
  }
}

function findNextFrame(file, stack, stackPos, step, filter) {
  let codeId = -1;
  let code = null;
  while (stackPos >= 0 && stackPos < stack.length) {
    codeId = stack[stackPos];
    code = codeId >= 0 ? file.code[codeId] : undefined;

    if (!filter || filter(code?.type, code?.kind)) return stackPos;
    stackPos += step;
  }
  return -1;
}

function addOrUpdateChildNode(parent, file, stackIndex, stackPos, ascending) {
  if (stackPos === -1) {
    // We reached the end without finding the next step.
    // If we are doing top-down call tree, update own ticks.
    if (!ascending) {
      parent.ownTicks++;
    }
    return;
  }

  let stack = file.ticks[stackIndex].s;
  console.assert(stackPos >= 0 && stackPos < stack.length);
  let codeId = stack[stackPos];
  let code = codeId >= 0 ? file.code[codeId] : undefined;
  // We found a child node.
  let childId = childIdFromCode(codeId, code);
  let child = parent.children[childId];
  if (!child) {
    let vmState = file.ticks[stackIndex].vm;
    child = createNodeFromStackEntry(code, codeId, vmState);
    child.delayedExpansion = { frameList : [], ascending };
    parent.children[childId] = child;
  }
  child.ticks++;
  addFrameToFrameList(child.delayedExpansion.frameList, stackIndex, stackPos);
}

// This expands a tree node (direct children only).
function expandTreeNode(file, node, filter) {
  let { frameList, ascending } = node.delayedExpansion;

  let step = ascending ? 2 : -2;

  for (let i = 0; i < frameList.length; i+= 3) {
    let firstStackIndex = frameList[i];
    let depth = frameList[i + 1];
    let count = frameList[i + 2];
    for (let j = 0; j < count; j++) {
      let stackIndex = firstStackIndex + j;
      let stack = file.ticks[stackIndex].s;

      // Get to the next frame that has not been filtered out.
      let stackPos = findNextFrame(file, stack, depth + step, step, filter);
      addOrUpdateChildNode(node, file, stackIndex, stackPos, ascending);
    }
  }
  node.delayedExpansion = null;
}

function createEmptyNode(name) {
  return {
      name : name,
      codeId: -1,
      type : "CAT",
      children : [],
      ownTicks : 0,
      ticks : 0
  };
}

class RuntimeCallTreeProcessor {
  constructor() {
    this.tree = createEmptyNode("root");
    this.tree.delayedExpansion = { frameList : [], ascending : false };
  }

  addStack(file, tickIndex) {
    this.tree.ticks++;

    let stack = file.ticks[tickIndex].s;
    let i;
    for (i = 0; i < stack.length; i += 2) {
      let codeId = stack[i];
      if (codeId < 0) return;
      let code = file.code[codeId];
      if (code.type !== "CPP" && code.type !== "SHARED_LIB") {
        i -= 2;
        break;
      }
    }
    if (i < 0 || i >= stack.length) return;
    addOrUpdateChildNode(this.tree, file, tickIndex, i, false);
  }
}

class PlainCallTreeProcessor {
  constructor(filter, isBottomUp) {
    this.filter = filter;
    this.tree = createEmptyNode("root");
    this.tree.delayedExpansion = { frameList : [], ascending : isBottomUp };
    this.isBottomUp = isBottomUp;
  }

  addStack(file, tickIndex) {
    let stack = file.ticks[tickIndex].s;
    let step = this.isBottomUp ? 2 : -2;
    let start = this.isBottomUp ? 0 : stack.length - 2;

    let stackPos = findNextFrame(file, stack, start, step, this.filter);
    addOrUpdateChildNode(this.tree, file, tickIndex, stackPos, this.isBottomUp);

    this.tree.ticks++;
  }
}

function buildCategoryTreeAndLookup() {
  let root = createEmptyNode("root");
  let categories = {};
  function addCategory(name, types) {
    let n = createEmptyNode(name);
    for (let i = 0; i < types.length; i++) {
      categories[types[i]] = n;
    }
    root.children.push(n);
  }
  addCategory("JS Ignition", [ "JS_IGNITION", "BC" ]);
  addCategory("JS Sparkplug", [ "JS_SPARKPLUG" ]);
  addCategory("JS Maglev", [ "JS_MAGLEV" ]);
  addCategory("JS Turbofan", [ "JS_TURBOFAN" ]);
  addCategory("IC", [ "IC" ]);
  addCategory("RegExp", [ "REGEXP" ]);
  addCategory("Other generated", [ "STUB", "BUILTIN" ]);
  addCategory("C++", [ "CPP", "LIB" ]);
  addCategory("C++/GC", [ "CPP_GC" ]);
  addCategory("C++/Parser", [ "CPP_PARSE" ]);
  addCategory("C++/Bytecode Compiler", [ "CPP_COMP_BC" ]);
  addCategory("C++/Baseline Compiler", [ "CPP_COMP_BASELINE" ]);
  addCategory("C++/Compiler", [ "CPP_COMP" ]);
  addCategory("C++/External", [ "CPP_EXT" ]);
  addCategory("C++/Logging", [ "CPP_LOGGING" ]);
  addCategory("Unknown", [ "UNKNOWN" ]);

  return { categories, root };
}

class CategorizedCallTreeProcessor {
  constructor(filter, isBottomUp) {
    this.filter = filter;
    let { categories, root } = buildCategoryTreeAndLookup();

    this.tree = root;
    this.categories = categories;
    this.isBottomUp = isBottomUp;
  }

  addStack(file, tickIndex) {
    let stack = file.ticks[tickIndex].s;
    let vmState = file.ticks[tickIndex].vm;
    if (stack.length === 0) return;
    let codeId = stack[0];
    let code = codeId >= 0 ? file.code[codeId] : undefined;
    let kind = resolveCodeKindAndVmState(code, vmState);
    let node = this.categories[kind];

    this.tree.ticks++;
    node.ticks++;

    let step = this.isBottomUp ? 2 : -2;
    let start = this.isBottomUp ? 0 : stack.length - 2;

    let stackPos = findNextFrame(file, stack, start, step, this.filter);
    addOrUpdateChildNode(node, file, tickIndex, stackPos, this.isBottomUp);
  }
}

class FunctionListTree {
  constructor(filter, withCategories) {
    if (withCategories) {
      let { categories, root } = buildCategoryTreeAndLookup();
      this.tree = root;
      this.categories = categories;
    } else {
      this.tree = createEmptyNode("root");
      this.categories = null;
    }

    this.codeVisited = [];
    this.filter = filter;
  }

  addStack(file, tickIndex) {
    let stack = file.ticks[tickIndex].s;
    let vmState = file.ticks[tickIndex].vm;

    this.tree.ticks++;
    let child = null;
    let tree = null;
    for (let i = stack.length - 2; i >= 0; i -= 2) {
      let codeId = stack[i];
      if (codeId < 0 || this.codeVisited[codeId]) continue;

      let code = file.code[codeId];
      if (this.filter) {
        let type = code ? code.type : undefined;
        let kind = code ? code.kind : undefined;
        if (!this.filter(type, kind)) continue;
      }
      let childId = childIdFromCode(codeId, code);
      if (this.categories) {
        let kind = resolveCodeKindAndVmState(code, vmState);
        tree = this.categories[kind];
      } else {
        tree = this.tree;
      }
      child = tree.children[childId];
      if (!child) {
        child = createNodeFromStackEntry(code, codeId, vmState);
        child.children[0] = createEmptyNode("Top-down tree");
        child.children[0].delayedExpansion =
          { frameList : [], ascending : false };
        child.children[1] = createEmptyNode("Bottom-up tree");
        child.children[1].delayedExpansion =
          { frameList : [], ascending : true };
        tree.children[childId] = child;
      }
      child.ticks++;
      child.children[0].ticks++;
      addFrameToFrameList(
          child.children[0].delayedExpansion.frameList, tickIndex, i);
      child.children[1].ticks++;
      addFrameToFrameList(
          child.children[1].delayedExpansion.frameList, tickIndex, i);
      this.codeVisited[codeId] = true;
    }
    if (child) {
      child.ownTicks++;
      console.assert(tree !== null);
      tree.ticks++;
      console.assert(tree.type === "CAT");
    }

    for (let i = 0; i < stack.length; i += 2) {
      let codeId = stack[i];
      if (codeId >= 0) this.codeVisited[codeId] = false;
    }
  }
}


class CategorySampler {
  constructor(file, bucketCount, filter) {
    this.bucketCount = bucketCount;
    this.filter = filter;

    this.firstTime = file.ticks[0].tm;
    let lastTime = file.ticks[file.ticks.length - 1].tm;
    this.step = (lastTime - this.firstTime) / bucketCount;

    this.buckets = [];
    let bucket = {};
    for (let i = 0; i < codeKinds.length; i++) {
      bucket[codeKinds[i]] = 0;
    }
    for (let i = 0; i < bucketCount; i++) {
      this.buckets.push(Object.assign({ total : 0 }, bucket));
    }
  }

  addStack(file, tickIndex) {
    let { tm : timestamp, vm : vmState, s : stack } = file.ticks[tickIndex];

    let i = Math.floor((timestamp - this.firstTime) / this.step);
    if (i === this.buckets.length) i--;
    console.assert(i >= 0 && i < this.buckets.length);

    let bucket = this.buckets[i];
    bucket.total++;

    let stackPos = findNextFrame(file, stack, 0, 2, this.filter);
    let codeId = stackPos >= 0 ? stack[stackPos] : -1;
    let code = codeId >= 0 ? file.code[codeId] : undefined;
    let kind = resolveCodeKindAndVmState(code, vmState);
    bucket[kind]++;
  }
}

class FunctionTimelineProcessor {
  constructor(functionCodeId, filter) {
    this.functionCodeId = functionCodeId;
    this.filter = filter;
    this.blocks = [];
    this.currentBlock = null;
  }

  addStack(file, tickIndex) {
    if (!this.functionCodeId) return;

    let { tm : timestamp, vm : vmState, s : stack } = file.ticks[tickIndex];
    let functionCode = file.code[this.functionCodeId];

    // Find if the function is on the stack, and its position on the stack,
    // ignoring any filtered entries.
    let stackCode = undefined;
    let functionPosInStack = -1;
    let filteredI = 0;
    for (let i = 0; i < stack.length - 1; i += 2) {
      let codeId = stack[i];
      let code = codeId >= 0 ? file.code[codeId] : undefined;
      let type = code ? code.type : undefined;
      let kind = code ? code.kind : undefined;
      if (!this.filter(type, kind)) continue;

      // Match other instances of the same function (e.g. unoptimised, various
      // different optimised versions).
      if (codeEquals(code, functionCode, true)) {
        functionPosInStack = filteredI;
        stackCode = code;
        break;
      }
      filteredI++;
    }

    if (functionPosInStack >= 0) {
      let stackKind = resolveCodeKindAndVmState(stackCode, vmState);

      let codeIsTopOfStack = (functionPosInStack === 0);

      if (this.currentBlock !== null) {
        this.currentBlock.end = timestamp;

        if (codeIsTopOfStack === this.currentBlock.topOfStack
          && stackKind === this.currentBlock.kind) {
          // If we haven't changed the stack top or the function kind, then
          // we're happy just extending the current block and not starting
          // a new one.
          return;
        }
      }

      // Start a new block at the current timestamp.
      this.currentBlock = {
        start: timestamp,
        end: timestamp,
        code: stackCode,
        kind: stackKind,
        topOfStack: codeIsTopOfStack
      };
      this.blocks.push(this.currentBlock);
    } else {
      this.currentBlock = null;
    }
  }
}

// Generates a tree out of a ticks sequence.
// {file} is the JSON files with the ticks and code objects.
// {startTime}, {endTime} is the interval.
// {tree} is the processor of stacks.
function generateTree(
    file, startTime, endTime, tree) {
  let ticks = file.ticks;
  let i = 0;
  while (i < ticks.length && ticks[i].tm < startTime) {
    i++;
  }

  let tickCount = 0;
  while (i < ticks.length && ticks[i].tm < endTime) {
    tree.addStack(file, i);
    i++;
    tickCount++;
  }

  return tickCount;
}

function computeOptimizationStats(file,
    timeStart = -Infinity, timeEnd = Infinity) {
  function newCollection() {
    return { count : 0, functions : [], functionTable : [] };
  }
  function addToCollection(collection, code) {
    collection.count++;
    let funcData = collection.functionTable[code.func];
    if (!funcData) {
      funcData = { f : file.functions[code.func], instances : [] };
      collection.functionTable[code.func] = funcData;
      collection.functions.push(funcData);
    }
    funcData.instances.push(code);
  }

  let functionCount = 0;
  let baselineFunctionCount = 0;
  let optimizedFunctionCount = 0;
  let maglevOptimizedFunctionCount = 0;
  let deoptimizedFunctionCount = 0;
  let baselineCompilations = newCollection();
  let optimizations = newCollection();
  let maglevOptimizations = newCollection();
  let eagerDeoptimizations = newCollection();
  let lazyDeoptimizations = newCollection();
  let dependencyChangeDeoptimizations = newCollection();

  for (let i = 0; i < file.functions.length; i++) {
    let f = file.functions[i];

    // Skip special SFIs that do not correspond to JS functions.
    if (f.codes.length === 0) continue;
    if (file.code[f.codes[0]].type !== "JS") continue;

    functionCount++;
    let baselineCompiled = false;
    let optimized = false;
    let maglev_optimized = false;
    let deoptimized = false;

    for (let j = 0; j < f.codes.length; j++) {
      let code = file.code[f.codes[j]];
      console.assert(code.type === "JS");
      if (code.kind === "Sparkplug") {
        baselineCompiled = true;
        if (code.tm >= timeStart && code.tm <= timeEnd) {
          addToCollection(baselineCompilations, code);
        }
      }
      if (code.kind === "Opt") {
        optimized = true;
        if (code.tm >= timeStart && code.tm <= timeEnd) {
          addToCollection(optimizations, code);
        }
      }
      if (code.kind === "Maglev") {
        maglev_optimized = true;
        if (code.tm >= timeStart && code.tm <= timeEnd) {
          addToCollection(maglevOptimizations, code);
        }
      }
      if (code.deopt) {
        deoptimized = true;
        if (code.deopt.tm >= timeStart && code.deopt.tm <= timeEnd) {
          switch (code.deopt.bailoutType) {
            case "deopt-lazy":
              addToCollection(lazyDeoptimizations, code);
              break;
            case "deopt-eager":
              addToCollection(eagerDeoptimizations, code);
              break;
            case "dependency-change":
              addToCollection(dependencyChangeDeoptimizations, code);
              break;
          }
        }
      }
    }
    if (baselineCompiled) {
      baselineFunctionCount++;
    }
    if (optimized) {
      optimizedFunctionCount++;
    }
    if (maglev_optimized) {
      maglevOptimizedFunctionCount++;
    }
    if (deoptimized) {
      deoptimizedFunctionCount++;
    }
  }

  function sortCollection(collection) {
    collection.functions.sort(
        (a, b) => a.instances.length - b.instances.length);
  }

  sortCollection(eagerDeoptimizations);
  sortCollection(lazyDeoptimizations);
  sortCollection(dependencyChangeDeoptimizations);
  sortCollection(baselineCompilations);
  sortCollection(optimizations);
  sortCollection(maglevOptimizations);

  return {
    functionCount,
    baselineFunctionCount,
    optimizedFunctionCount,
    maglevOptimizedFunctionCount,
    deoptimizedFunctionCount,
    baselineCompilations,
    optimizations,
    maglevOptimizations,
    eagerDeoptimizations,
    lazyDeoptimizations,
    dependencyChangeDeoptimizations,
  };
}

function normalizeLeadingWhitespace(lines) {
  let regex = /^\s*/;
  let minimumLeadingWhitespaceChars = Infinity;
  for (let line of lines) {
    minimumLeadingWhitespaceChars =
        Math.min(minimumLeadingWhitespaceChars, regex.exec(line)[0].length);
  }
  for (let i = 0; i < lines.length; i++) {
    lines[i] = lines[i].substring(minimumLeadingWhitespaceChars);
  }
}
