// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { TurboshaftGraphOperation } from "../phases/turboshaft-graph-phase/turboshaft-graph-operation";
import { GraphNode } from "../phases/graph-phase/graph-node";
import { TurboshaftGraphBlock } from "../phases/turboshaft-graph-phase/turboshaft-graph-block";
import { GenericPosition } from "../source-resolver";
import { BytecodePosition } from "../position";

export interface ClearableHandler {
  brokeredClear(): void;
}

export interface HistoryHandler {
  showNodeHistory(node: GraphNode | TurboshaftGraphOperation, phaseName: string): void;
}

export interface NodeSelectionHandler {
  select(nodes: Iterable<TurboshaftGraphOperation | GraphNode | string | number>, selected: boolean,
         scrollIntoView: boolean): void;
  clear(): void;
  brokeredNodeSelect(nodeIds: Set<string>, selected: boolean): void;
  brokeredClear(): void;
}

export interface BlockSelectionHandler {
  select(blocks: Iterable<TurboshaftGraphBlock | number>, selected: boolean,
         scrollIntoView: boolean): void;
  clear(): void;
  brokeredBlockSelect(blockIds: Array<number>, selected: boolean): void;
}

export interface InstructionSelectionHandler {
  select(instructionIds: Array<string>, selected: boolean): void;
  clear(): void;
  brokeredInstructionSelect(instructionsOffsets: Array<[number, number]> | Array<Array<number>>,
                            selected: boolean): void;
}

export interface SourcePositionSelectionHandler {
  select(sourcePositions: Array<GenericPosition>, selected: boolean): void;
  clear(): void;
  brokeredSourcePositionSelect(sourcePositions: Array<GenericPosition>, selected: boolean): void;
}

export interface BytecodeOffsetSelectionHandler {
  select(offsets: Array<number>, selected: boolean): void;
  clear(): void;
  brokeredBytecodeOffsetSelect(positions: Array<BytecodePosition>, selected: boolean): void;
}

export interface RegisterAllocationSelectionHandler {
  // These are called instructionIds since the class of the divs is "instruction-id"
  select(instructionIds: Array<number>, selected: boolean, scrollIntoView: boolean): void;
  clear(): void;
  brokeredRegisterAllocationSelect(instructionsOffsets: Array<[number, number]>, selected: boolean):
    void;
}
