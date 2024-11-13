// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { GenericPosition, SourceResolver } from "../source-resolver";
import { GraphNode } from "../phases/graph-phase/graph-node";
import { BytecodePosition } from "../position";
import { TurboshaftGraphOperation } from "../phases/turboshaft-graph-phase/turboshaft-graph-operation";
import {
  ClearableHandler,
  SourcePositionSelectionHandler,
  NodeSelectionHandler,
  BlockSelectionHandler,
  InstructionSelectionHandler,
  RegisterAllocationSelectionHandler,
  HistoryHandler,
  BytecodeOffsetSelectionHandler
} from "./selection-handler";

export class SelectionBroker {
  sourceResolver: SourceResolver;
  allHandlers: Array<ClearableHandler>;
  historyHandlers: Array<HistoryHandler>;
  nodeHandlers: Array<NodeSelectionHandler>;
  blockHandlers: Array<BlockSelectionHandler>;
  instructionHandlers: Array<InstructionSelectionHandler>;
  sourcePositionHandlers: Array<SourcePositionSelectionHandler>;
  bytecodeOffsetHandlers: Array<BytecodeOffsetSelectionHandler>;
  registerAllocationHandlers: Array<RegisterAllocationSelectionHandler>;

  constructor(sourceResolver: SourceResolver) {
    this.sourceResolver = sourceResolver;
    this.allHandlers = new Array<ClearableHandler>();
    this.historyHandlers = new Array<HistoryHandler>();
    this.nodeHandlers = new Array<NodeSelectionHandler>();
    this.blockHandlers = new Array<BlockSelectionHandler>();
    this.instructionHandlers = new Array<InstructionSelectionHandler>();
    this.sourcePositionHandlers = new Array<SourcePositionSelectionHandler>();
    this.bytecodeOffsetHandlers = new Array<BytecodeOffsetSelectionHandler>();
    this.registerAllocationHandlers = new Array<RegisterAllocationSelectionHandler>();
  }

  public addHistoryHandler(handler: HistoryHandler): void {
    this.historyHandlers.push(handler);
  }

  public deleteHistoryHandler(handler: HistoryHandler): void {
    this.historyHandlers = this.historyHandlers.filter(h => h != handler);
  }

  public addNodeHandler(handler: NodeSelectionHandler & ClearableHandler): void {
    this.allHandlers.push(handler);
    this.nodeHandlers.push(handler);
  }

  public deleteNodeHandler(handler: NodeSelectionHandler & ClearableHandler): void {
    this.allHandlers = this.allHandlers.filter(h => h != handler);
    this.nodeHandlers = this.nodeHandlers.filter(h => h != handler);
  }

  public addBlockHandler(handler: BlockSelectionHandler & ClearableHandler): void {
    this.allHandlers.push(handler);
    this.blockHandlers.push(handler);
  }

  public deleteBlockHandler(handler: BlockSelectionHandler & ClearableHandler): void {
    this.allHandlers = this.allHandlers.filter(h => h != handler);
    this.blockHandlers = this.blockHandlers.filter(h => h != handler);
  }

  public addInstructionHandler(handler: InstructionSelectionHandler & ClearableHandler): void {
    this.allHandlers.push(handler);
    this.instructionHandlers.push(handler);
  }

  public addSourcePositionHandler(handler: SourcePositionSelectionHandler & ClearableHandler):
    void {
    this.allHandlers.push(handler);
    this.sourcePositionHandlers.push(handler);
  }

  public addBytecodeOffsetHandler(handler: BytecodeOffsetSelectionHandler & ClearableHandler):
    void {
    this.allHandlers.push(handler);
    this.bytecodeOffsetHandlers.push(handler);
  }

  public addRegisterAllocatorHandler(handler: RegisterAllocationSelectionHandler
    & ClearableHandler): void {
    this.allHandlers.push(handler);
    this.registerAllocationHandlers.push(handler);
  }

  public broadcastHistoryShow(from, node: GraphNode | TurboshaftGraphOperation, phaseName: string):
    void {
    for (const handler of this.historyHandlers) {
      if (handler != from) handler.showNodeHistory(node, phaseName);
    }
  }

  public broadcastInstructionSelect(from, instructionOffsets: Array<number>, selected: boolean):
    void {
    // Select the lines from the disassembly (right panel)
    for (const handler of this.instructionHandlers) {
      if (handler != from) handler.brokeredInstructionSelect([instructionOffsets], selected);
    }

    // Select the lines from the source and bytecode panels (left panels)
    const nodes = this.sourceResolver.instructionsPhase.nodesForInstructions(instructionOffsets);
    const sourcePositions = this.sourceResolver.nodeIdsToSourcePositions(nodes);
    for (const handler of this.sourcePositionHandlers) {
      if (handler != from) handler.brokeredSourcePositionSelect(sourcePositions, selected);
    }
    const bytecodePositions = this.sourceResolver.nodeIdsToBytecodePositions(nodes);
    for (const handler of this.bytecodeOffsetHandlers) {
      if (handler != from) handler.brokeredBytecodeOffsetSelect(bytecodePositions, selected);
    }

    // Select the lines from the middle panel for the register allocation phase.
    for (const b of this.registerAllocationHandlers) {
      if (b != from) {
        b.brokeredRegisterAllocationSelect(instructionOffsets.map(instr => [instr, instr]),
                                           selected);
      }
    }
  }

  public broadcastSourcePositionSelect(from, sourcePositions: Array<GenericPosition>,
                                       selected: boolean, selectedNodes?: Set<string>): void {
    sourcePositions = sourcePositions.filter(sourcePosition => {
      if (!sourcePosition.isValid()) {
        console.warn("Invalid source position");
        return false;
      }
      return true;
    });

    // Select the lines from the source panel (left panel)
    for (const handler of this.sourcePositionHandlers) {
      if (handler != from) handler.brokeredSourcePositionSelect(sourcePositions, selected);
    }

    // Select the nodes (middle panel)
    const nodes = this.sourceResolver.sourcePositionsToNodeIds(sourcePositions);
    for (const handler of this.nodeHandlers) {
      if (handler != from) handler.brokeredNodeSelect(nodes, selected);
    }

    // Select bytecode source panel (left panel)
    const bytecodePositions = selectedNodes
      ? this.sourceResolver.nodeIdsToBytecodePositions(selectedNodes)
      : this.sourceResolver.nodeIdsToBytecodePositions(nodes);
    for (const handler of this.bytecodeOffsetHandlers) {
      if (handler != from) handler.brokeredBytecodeOffsetSelect(bytecodePositions, selected);
    }

    this.selectInstructionsAndRegisterAllocations(from, nodes, selected);
  }

  public broadcastBytecodePositionsSelect(from, bytecodePositions: Array<BytecodePosition>,
                                       selected: boolean): void {
    bytecodePositions = bytecodePositions.filter(bytecodePosition => {
      if (!bytecodePosition.isValid()) {
        console.warn("Invalid bytecode position");
        return false;
      }
      return true;
    });

    // Select the lines from the bytecode panel (left panel)
    for (const handler of this.bytecodeOffsetHandlers) {
      if (handler != from) handler.brokeredBytecodeOffsetSelect(bytecodePositions, selected);
    }

    // Select the nodes (middle panel)
    const nodes = this.sourceResolver.bytecodePositionsToNodeIds(bytecodePositions);
    for (const handler of this.nodeHandlers) {
      if (handler != from) handler.brokeredNodeSelect(nodes, selected);
    }

    // Select the lines from the source panel (left panel)
    const sourcePositions = this.sourceResolver.nodeIdsToSourcePositions(nodes);
    for (const handler of this.sourcePositionHandlers) {
      if (handler != from) handler.brokeredSourcePositionSelect(sourcePositions, selected);
    }

    this.selectInstructionsAndRegisterAllocations(from, nodes, selected);
  }

  public broadcastNodeSelect(from, nodes: Set<string>, selected: boolean): void {
    // Select the nodes (middle panel)
    for (const handler of this.nodeHandlers) {
      if (handler != from) handler.brokeredNodeSelect(nodes, selected);
    }

    // Select the lines from the source panel (left panel)
    const sourcePositions = this.sourceResolver.nodeIdsToSourcePositions(nodes);
    for (const handler of this.sourcePositionHandlers) {
      if (handler != from) handler.brokeredSourcePositionSelect(sourcePositions, selected);
    }

    // Select bytecode source panel (left panel)
    const bytecodePositions = this.sourceResolver.nodeIdsToBytecodePositions(nodes);
    for (const handler of this.bytecodeOffsetHandlers) {
      if (handler != from) handler.brokeredBytecodeOffsetSelect(bytecodePositions, selected);
    }

    this.selectInstructionsAndRegisterAllocations(from, nodes, selected);
  }

  public broadcastBlockSelect(from, blocksIds: Array<number>, selected: boolean): void {
    for (const handler of this.blockHandlers) {
      if (handler != from) handler.brokeredBlockSelect(blocksIds, selected);
    }
  }

  public broadcastClear(from): void {
    for (const handler of this.allHandlers) {
      if (handler != from) handler.brokeredClear();
    }
  }

  private selectInstructionsAndRegisterAllocations(from, nodes: Set<string>, selected: boolean):
    void {
    const instructionsOffsets = new Array<[number, number]>();
    for (const node of nodes) {
      const instructionRange = this.sourceResolver.instructionsPhase.nodeIdToInstructionRange[node];
      if (instructionRange) instructionsOffsets.push(instructionRange);
    }

    if (instructionsOffsets.length > 0) {
      // Select the lines from the disassembly (right panel)
      for (const handler of this.instructionHandlers) {
        if (handler != from) handler.brokeredInstructionSelect(instructionsOffsets, selected);
      }

      // Select the lines from the middle panel for the register allocation phase.
      for (const handler of this.registerAllocationHandlers) {
        if (handler != from) {
          handler.brokeredRegisterAllocationSelect(instructionsOffsets, selected);
        }
      }
    }
  }
}
