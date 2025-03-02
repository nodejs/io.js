// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { camelize, sortUnique } from "./common/util";
import { PhaseType } from "./phases/phase";
import { GraphPhase } from "./phases/graph-phase/graph-phase";
import { DisassemblyPhase } from "./phases/disassembly-phase";
import { InstructionsPhase } from "./phases/instructions-phase";
import { SchedulePhase } from "./phases/schedule-phase";
import { SequencePhase } from "./phases/sequence-phase";
import { BytecodeSource, BytecodeSourceData, Source } from "./source";
import { TurboshaftCustomDataPhase } from "./phases/turboshaft-custom-data-phase";
import { TurboshaftGraphPhase } from "./phases/turboshaft-graph-phase/turboshaft-graph-phase";
import { GraphNode } from "./phases/graph-phase/graph-node";
import { TurboshaftGraphOperation } from "./phases/turboshaft-graph-phase/turboshaft-graph-operation";
import { BytecodePosition, InliningPosition, PositionsContainer, SourcePosition } from "./position";
import { NodeOrigin } from "./origin";

export type GenericPosition = SourcePosition | BytecodePosition;
export type DynamicPhase = GraphPhase | TurboshaftGraphPhase | SchedulePhase | SequencePhase;
export type GenericPhase = GraphPhase | TurboshaftGraphPhase | TurboshaftCustomDataPhase
  | DisassemblyPhase | InstructionsPhase | SchedulePhase | SequencePhase;

export class SourceResolver {
  sources: Array<Source>;
  bytecodeSources: Map<number, BytecodeSource>;
  inlinings: Array<InliningPosition>;
  inliningsMap: Map<string, InliningPosition>;
  phases: Array<GenericPhase>;
  phaseNames: Map<string, number>;
  disassemblyPhase: DisassemblyPhase;
  linePositionMap: Map<string, Array<GenericPosition>>;
  finalNodeOrigins: Array<NodeOrigin>;
  instructionsPhase: InstructionsPhase;
  positions: PositionsContainer;

  constructor() {
    // Maps source ids to source objects.
    this.sources = new Array<Source>();
    // Maps bytecode source ids to bytecode source objects.
    this.bytecodeSources = new Map<number, BytecodeSource>();
    // Maps inlining ids to inlining objects.
    this.inlinings = new Array<InliningPosition>();
    // Maps source position keys to inlinings.
    this.inliningsMap = new Map<string, InliningPosition>();
    // Maps phase ids to phases.
    this.phases = new Array<GenericPhase>();
    // Maps phase names to phaseIds.
    this.phaseNames = new Map<string, number>();
    // Maps line numbers to source positions
    this.linePositionMap = new Map<string, Array<GenericPosition>>();
    // Maps node ids to node origin
    this.finalNodeOrigins = new Array<NodeOrigin>();
  }

  public getMainFunction(jsonObj): Source {
    const fncJson = jsonObj.function;
    // Backwards compatibility.
    if (typeof fncJson === "string") {
      return new Source(null, null, jsonObj.source, -1, true,
        jsonObj.sourcePosition, jsonObj.sourcePosition + jsonObj.source.length);
    }
    return new Source(fncJson.sourceName, fncJson.functionName, fncJson.sourceText,
      fncJson.sourceId, false, fncJson.startPosition, fncJson.endPosition);
  }

  public setInlinings(inliningsJson): void {
    if (inliningsJson) {
      for (const [inliningIdStr, inlining] of Object.entries<InliningPosition>(inliningsJson)) {
        const scriptOffset = inlining.inliningPosition.scriptOffset;
        const inliningId = inlining.inliningPosition.inliningId;
        const inl = new InliningPosition(inlining.sourceId,
          new SourcePosition(scriptOffset, inliningId));
        this.inlinings[Number(inliningIdStr)] = inl;
        this.inliningsMap.set(inl.inliningPosition.toString(), inl);
      }
    }
    // This is a default entry for the script itself that helps
    // keep other code more uniform.
    this.inlinings[-1] = new InliningPosition(-1, null);
  }

  public setSources(sourcesJson, mainFunc: Source): void {
    if (sourcesJson) {
      for (const [sourceId, source] of Object.entries<Source>(sourcesJson)) {
        const src = new Source(source.sourceName, source.functionName, source.sourceText,
          source.sourceId, source.backwardsCompatibility, source.startPosition, source.endPosition);
        this.sources[sourceId] = src;
      }
    }
    // This is a fallback if the JSON is incomplete (e.g. due to compiler crash).
    if (!this.sources[-1]) {
      this.sources[-1] = mainFunc;
    }
  }

  public setBytecodeSources(bytecodeSourcesJson): void {
    if (!bytecodeSourcesJson) return;
    for (const [sourceId, source] of Object.entries<any>(bytecodeSourcesJson)) {
      const bytecodeSource = source.bytecodeSource;
      const data = new Array<BytecodeSourceData>();

      for (const bytecode of Object.values<BytecodeSourceData>(bytecodeSource.data)) {
        data.push(new BytecodeSourceData(bytecode.offset, bytecode.disassembly));
      }

      const numSourceId = Number(sourceId);
      const inliningIds = [];
      for (let index = -1; index < this.inlinings.length; index += 1) {
        const inlining = this.inlinings[index];
        if (inlining.sourceId == source.sourceId) inliningIds.push(index);
      }
      this.bytecodeSources.set(numSourceId,
                               new BytecodeSource(source.sourceId, inliningIds, source.functionName,
                                                  data, bytecodeSource.constantPool,
                                                  source.feedbackVector));
    }
  }

  public setFinalNodeOrigins(nodeOriginsJson): void {
    if (!nodeOriginsJson) return;
    for (const [nodeId, nodeOrigin] of Object.entries<NodeOrigin>(nodeOriginsJson)) {
      this.finalNodeOrigins[nodeId] = new NodeOrigin(nodeOrigin.nodeId, null, nodeOrigin.phase,
        nodeOrigin.reducer);
    }
  }

  public parsePhases(phasesJson): void {
    const instructionsPhase = new InstructionsPhase();
    const selectedDynamicPhases = new Array<DynamicPhase>();
    const nodeMap = new Array<GraphNode | TurboshaftGraphOperation>();
    let lastTurboshaftGraphPhase: TurboshaftGraphPhase = null;
    let lastGraphPhase: GraphPhase | TurboshaftGraphPhase = null;
    for (const [, genericPhase] of Object.entries<GenericPhase>(phasesJson)) {
      switch (genericPhase.type) {
        case PhaseType.Disassembly:
          const castedDisassembly = genericPhase as DisassemblyPhase;
          const disassemblyPhase = new DisassemblyPhase(castedDisassembly.name,
            castedDisassembly.data, castedDisassembly?.blockIdToOffset);
          this.disassemblyPhase = disassemblyPhase;
          break;
        case PhaseType.Schedule:
          const castedSchedule = genericPhase as SchedulePhase;
          const schedulePhase = new SchedulePhase(castedSchedule.name, castedSchedule.data);
          this.phaseNames.set(schedulePhase.name, this.phases.length);
          this.phases.push(schedulePhase);
          selectedDynamicPhases.push(schedulePhase);
          if (lastGraphPhase instanceof GraphPhase) {
            schedulePhase.positions = lastGraphPhase.positions;
          } else {
            const oldIdToNewIdMap = this.getOldIdToNewIdMap(this.phases.length - 1);
            schedulePhase.positions.merge(lastGraphPhase.data.nodes, oldIdToNewIdMap);
          }
          schedulePhase.instructionsPhase = instructionsPhase;
          break;
        case PhaseType.Sequence:
          const castedSequence = camelize(genericPhase) as SequencePhase;
          const sequencePhase = new SequencePhase(castedSequence.name, castedSequence.blocks,
            castedSequence.registerAllocation);
          const prevPhase = this.getDynamicPhase(this.phases.length - 1);
          sequencePhase.positions = prevPhase.positions;
          sequencePhase.instructionsPhase = prevPhase.instructionsPhase;
          this.phaseNames.set(sequencePhase.name, this.phases.length);
          this.phases.push(sequencePhase);
          break;
        case PhaseType.Instructions:
          const castedInstructions = genericPhase as InstructionsPhase;
          if (instructionsPhase.name === "") {
            instructionsPhase.name = castedInstructions.name;
          } else {
            instructionsPhase.name += `, ${castedInstructions.name}`;
          }
          instructionsPhase.parseNodeIdToInstructionRangeFromJSON(castedInstructions
            ?.nodeIdToInstructionRange);
          instructionsPhase.parseBlockIdToInstructionRangeFromJSON(castedInstructions
            ?.blockIdToInstructionRange);
          instructionsPhase.parseInstructionOffsetToPCOffsetFromJSON(castedInstructions
            ?.instructionOffsetToPCOffset);
          instructionsPhase.parseCodeOffsetsInfoFromJSON(castedInstructions?.codeOffsetsInfo);
          break;
        case PhaseType.Graph:
          const castedGraph = genericPhase as GraphPhase;
          const graphPhase = new GraphPhase(castedGraph.name, castedGraph.data,
            nodeMap as Array<GraphNode>, this.sources, this.inlinings);
          this.phaseNames.set(graphPhase.name, this.phases.length);
          this.phases.push(graphPhase);
          selectedDynamicPhases.push(graphPhase);
          lastGraphPhase = graphPhase;
          break;
        case PhaseType.TurboshaftGraph:
          const castedTurboshaftGraph = genericPhase as TurboshaftGraphPhase;
          const turboshaftGraphPhase = new TurboshaftGraphPhase(castedTurboshaftGraph.name,
            castedTurboshaftGraph.data, nodeMap, this.sources, this.inlinings);
          this.phaseNames.set(turboshaftGraphPhase.name, this.phases.length);
          this.phases.push(turboshaftGraphPhase);
          selectedDynamicPhases.push(turboshaftGraphPhase);
          lastTurboshaftGraphPhase = turboshaftGraphPhase;
          lastGraphPhase = turboshaftGraphPhase;
          break;
        case PhaseType.TurboshaftCustomData:
          const castedCustomData = camelize(genericPhase) as TurboshaftCustomDataPhase;
          const customDataPhase = new TurboshaftCustomDataPhase(castedCustomData.name,
            castedCustomData.dataTarget, castedCustomData.data);
          lastTurboshaftGraphPhase?.addCustomData(customDataPhase);
          break;
        default:
          throw "Unsupported phase type";
      }
    }
    this.sortSourcePositions();
    this.instructionsPhase = instructionsPhase;
    if (!lastTurboshaftGraphPhase) {
      for (const phase of selectedDynamicPhases) {
        if (!phase.isDynamic()) continue;
        phase.instructionsPhase = instructionsPhase;
      }
      return;
    }
    if (instructionsPhase.name == "") return;
    // Adapting 'nodeIdToInstructionRange' array to fix Turboshaft's nodes recreation
    this.adaptInstructionsPhases(selectedDynamicPhases);
  }

  public sourcePositionsToNodeIds(sourcePositions: Array<GenericPosition>): Set<string> {
    const nodeIds = new Set<string>();
    for (const position of sourcePositions) {
      const key = position.toString();
      let nodeIdsForPosition: Array<string> = null;
      if (position instanceof SourcePosition) {
        nodeIdsForPosition = this.positions.sourcePositionToNodes.get(key);
      } else {
        // Wasm support
        nodeIdsForPosition = this.positions.bytecodePositionToNodes.get(key);
      }
      if (!nodeIdsForPosition) continue;
      for (const nodeId of nodeIdsForPosition) {
        nodeIds.add(nodeId);
      }
    }
    return nodeIds;
  }

  public bytecodePositionsToNodeIds(bytecodePositions: Array<BytecodePosition>): Set<string> {
    const nodeIds = new Set<string>();
    for (const position of bytecodePositions) {
      const nodeIdsForPosition = this.positions.bytecodePositionToNodes.get(position.toString());
      if (!nodeIdsForPosition) continue;
      for (const nodeId of nodeIdsForPosition) {
        nodeIds.add(nodeId);
      }
    }
    return nodeIds;
  }

  public nodeIdsToSourcePositions(nodeIds: Iterable<string>): Array<GenericPosition> {
    const sourcePositions = new Map<string, GenericPosition>();
    for (const nodeId of nodeIds) {
      const sourcePosition = this.positions.nodeIdToSourcePositionMap[nodeId];
      if (sourcePosition) {
        sourcePositions.set(sourcePosition.toString(), sourcePosition);
      }
      // Wasm support
      if (this.bytecodeSources.size == 0) {
        const bytecodePosition = this.positions.nodeIdToBytecodePositionMap[nodeId];
        if (bytecodePosition) {
          sourcePositions.set(bytecodePosition.toString(), bytecodePosition);
        }
      }
    }
    const sourcePositionArray = new Array<GenericPosition>();
    for (const sourcePosition of sourcePositions.values()) {
      sourcePositionArray.push(sourcePosition);
    }
    return sourcePositionArray;
  }

  public nodeIdsToBytecodePositions(nodeIds: Iterable<string>): Array<BytecodePosition> {
    const bytecodePositions = new Map<string, BytecodePosition>();
    for (const nodeId of nodeIds) {
      const position = this.positions.nodeIdToBytecodePositionMap[nodeId];
      if (!position) continue;
      bytecodePositions.set(position.toString(), position);
    }
    return Array.from(bytecodePositions.values());
  }

  public translateToSourceId(sourceId: number, location?: SourcePosition): SourcePosition {
    for (const position of this.getInlineStack(location)) {
      const inlining = this.inlinings[position.inliningId];
      if (!inlining) continue;
      if (inlining.sourceId == sourceId) {
        return position;
      }
    }
    return location;
  }

  public addInliningPositions(sourcePosition: GenericPosition, locations: Array<SourcePosition>):
    void {
    const inlining = this.inliningsMap.get(sourcePosition.toString());
    if (!inlining) return;
    const source = this.sources[inlining.sourceId];
    for (const sp of source.sourcePositions) {
      locations.push(sp);
      this.addInliningPositions(sp, locations);
    }
  }

  public getInliningForPosition(sourcePosition: GenericPosition): InliningPosition {
    return this.inliningsMap.get(sourcePosition.toString());
  }

  public getSource(sourceId: number): Source {
    return this.sources[sourceId];
  }

  public addAnyPositionToLine(lineNumber: number | string, sourcePosition: GenericPosition): void {
    const lineNumberString = String(lineNumber);
    if (!this.linePositionMap.has(lineNumberString)) {
      this.linePositionMap.set(lineNumberString, new Array<GenericPosition>());
    }
    const storedPositions = this.linePositionMap.get(lineNumberString);
    if (!storedPositions.includes(sourcePosition)) storedPositions.push(sourcePosition);
  }

  public repairPhaseId(anyPhaseId: number): number {
    return Math.max(0, Math.min(anyPhaseId | 0, this.phases.length - 1));
  }

  public getPhase(phaseId: number): GenericPhase {
    return this.phases[phaseId];
  }

  public getGraphPhase(phaseId: number): GraphPhase | TurboshaftGraphPhase {
    const phase = this.phases[phaseId];
    return phase.isGraph() ? phase as GraphPhase | TurboshaftGraphPhase : null;
  }

  public getDynamicPhase(phaseId: number): DynamicPhase {
    const phase = this.phases[phaseId];
    return phase.isDynamic() ? phase as DynamicPhase : null;
  }

  public getPhaseNameById(phaseId: number): string {
    return this.getPhase(phaseId).name;
  }

  public getPhaseIdByName(phaseName: string): number {
    return this.phaseNames.get(phaseName);
  }

  public lineToSourcePositions(lineNumber: number | string): Array<GenericPosition> {
    return this.linePositionMap.get(String(lineNumber)) ?? new Array<GenericPosition>();
  }

  public getSourceName(sourceId: number): string {
    const source = this.sources[sourceId];
    return `${source.sourceName}:${source.functionName}`;
  }

  public sourcePositionsInRange(sourceId: number, start: number, end: number):
    Array<SourcePosition> {
    const inRange = Array<SourcePosition>();
    if (!this.sources[sourceId]) return inRange;
    const list = this.sources[sourceId].sourcePositions;
    for (const sourcePosition of list) {
      if (start <= sourcePosition.scriptOffset && sourcePosition.scriptOffset < end) {
        inRange.push(sourcePosition);
      }
    }
    return inRange;
  }

  public setSourceLineToBytecodePosition(sourceLineToBytecodePositionJson): void {
    if (!sourceLineToBytecodePositionJson) return;
    sourceLineToBytecodePositionJson.forEach((position, idx) => {
      this.addAnyPositionToLine(idx, new BytecodePosition(position, -1));
    });
  }

  private getInlineStack(sourcePosition?: SourcePosition): Array<SourcePosition> {
    const inliningStack = Array<SourcePosition>();
    if (!sourcePosition) return inliningStack;
    let cur = sourcePosition;
    while (cur && cur.inliningId != -1) {
      inliningStack.push(cur);
      const inlining = this.inlinings[cur.inliningId];
      if (!inlining) break;
      cur = inlining.inliningPosition;
    }
    if (cur && cur.inliningId == -1) {
      inliningStack.push(cur);
    }
    return inliningStack;
  }

  private sortSourcePositions(): void {
    for (const source of Object.values<Source>(this.sources)) {
      source.sourcePositions = sortUnique(source.sourcePositions,
        (a, b) => a.lessOrEquals(b),
        (a, b) => a.equals(b));
    }
  }

  private adaptInstructionsPhases(dynamicPhases: Array<DynamicPhase>): void {
    const seaOfNodesInstructions = new InstructionsPhase("sea of nodes");
    for (let phaseId = dynamicPhases.length - 2; phaseId >= 0; phaseId--) {
      const phase = dynamicPhases[phaseId];
      const prevPhase = dynamicPhases[phaseId + 1];
      if (phase.type == PhaseType.TurboshaftGraph && prevPhase.type == PhaseType.Schedule) {
        phase.instructionsPhase.merge(prevPhase.instructionsPhase);
        const oldIdToNewIdMap = this.getOldIdToNewIdMap(phaseId + 1);
        const maxNodeId = (phase as TurboshaftGraphPhase).highestNodeId;
        for (let nodeId = 0; nodeId <= maxNodeId; nodeId++) {
          const prevNodeId = oldIdToNewIdMap.has(nodeId) ? oldIdToNewIdMap.get(nodeId) : nodeId;
          phase.instructionsPhase.nodeIdToInstructionRange[nodeId] =
            prevPhase.instructionsPhase.getInstruction(prevNodeId);
        }
      } else if (phase.type == PhaseType.TurboshaftGraph &&
        prevPhase.type == PhaseType.TurboshaftGraph) {
        phase.instructionsPhase.merge(prevPhase.instructionsPhase);
        const maxNodeId = (phase as TurboshaftGraphPhase).highestNodeId;
        const originIdToNodesMap = (prevPhase as TurboshaftGraphPhase).originIdToNodesMap;
        for (let nodeId = 0; nodeId <= maxNodeId; nodeId++) {
          const nodes = originIdToNodesMap.get(String(nodeId));
          const prevNodeId = nodes?.length > 0 ? nodes[0]?.id : nodeId;
          phase.instructionsPhase.nodeIdToInstructionRange[nodeId] =
            prevPhase.instructionsPhase.getInstruction(prevNodeId);
        }
      } else if (phase.type == PhaseType.Schedule && prevPhase.type == PhaseType.TurboshaftGraph) {
        seaOfNodesInstructions.merge(prevPhase.instructionsPhase);
        phase.instructionsPhase = seaOfNodesInstructions;
        const originIdToNodesMap = (prevPhase as TurboshaftGraphPhase).originIdToNodesMap;
        for (const [originId, nodes] of originIdToNodesMap.entries()) {
          if (!originId || nodes.length == 0) continue;
          phase.instructionsPhase.nodeIdToInstructionRange[originId] =
            prevPhase.instructionsPhase.getInstruction(nodes[0].id);
        }
      } else if (phase.type == PhaseType.Graph && prevPhase.type == PhaseType.Graph) {
        phase.instructionsPhase = seaOfNodesInstructions;
        const prevGraphPhase = prevPhase as GraphPhase;
        for (const [originId, nodes] of prevGraphPhase.originIdToNodesMap.entries()) {
          if (!originId || nodes.length == 0) continue;
          for (const node of nodes) {
            if (!phase.instructionsPhase.nodeIdToInstructionRange[originId]) {
              if (!prevPhase.instructionsPhase.nodeIdToInstructionRange[node.id]) continue;
              phase.instructionsPhase.nodeIdToInstructionRange[originId] =
                prevPhase.instructionsPhase.getInstruction(node.id);
            } else {
              break;
            }
          }
        }
      } else {
        phase.instructionsPhase = seaOfNodesInstructions;
      }
    }
  }

  private getOldIdToNewIdMap(phaseId: number): Map<number, number> {
    // This function works with final node origins (we can have overwriting for Turboshaft IR)
    const oldIdToNewIdMap = new Map<number, number>();
    for (const [newId, nodeOrigin] of this.finalNodeOrigins.entries()) {
      if (!nodeOrigin) continue;
      if (nodeOrigin.phase === this.phases[phaseId].name) {
        oldIdToNewIdMap.set(nodeOrigin.nodeId, newId);
      }
    }
    return oldIdToNewIdMap;
  }
}
