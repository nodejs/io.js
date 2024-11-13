// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { GraphStateType, Phase, PhaseType } from "../phase";
import { TurboshaftGraphOperation } from "./turboshaft-graph-operation";
import { TurboshaftGraphEdge } from "./turboshaft-graph-edge";
import { TurboshaftGraphBlock } from "./turboshaft-graph-block";
import { DataTarget, TurboshaftCustomDataPhase } from "../turboshaft-custom-data-phase";
import { GraphNode } from "../graph-phase/graph-node";
import { NodeOrigin } from "../../origin";
import { Source } from "../../source";
import { InstructionsPhase } from "../instructions-phase";
import {
  BytecodePosition,
  InliningPosition,
  PositionsContainer,
  SourcePosition
} from "../../position";

export class TurboshaftGraphPhase extends Phase {
  data: TurboshaftGraphData;
  customData: TurboshaftCustomData;
  stateType: GraphStateType;
  instructionsPhase: InstructionsPhase;
  nodeIdToNodeMap: Array<TurboshaftGraphOperation>;
  blockIdToBlockMap: Array<TurboshaftGraphBlock>;
  originIdToNodesMap: Map<string, Array<TurboshaftGraphOperation>>;
  positions: PositionsContainer;
  highestNodeId: number;
  rendered: boolean;
  customDataShowed: boolean;
  transform: { x: number, y: number, scale: number };

  constructor(name: string, dataJson, nodeMap: Array<GraphNode | TurboshaftGraphOperation>,
              sources: Array<Source>, inlinings: Array<InliningPosition>) {
    super(name, PhaseType.TurboshaftGraph);
    this.stateType = GraphStateType.NeedToFullRebuild;
    this.instructionsPhase = new InstructionsPhase();
    this.customData = new TurboshaftCustomData();
    this.nodeIdToNodeMap = new Array<TurboshaftGraphOperation>();
    this.blockIdToBlockMap = new Array<TurboshaftGraphBlock>();
    this.originIdToNodesMap = new Map<string, Array<TurboshaftGraphOperation>>();
    this.positions = new PositionsContainer();
    this.highestNodeId = 0;
    this.rendered = false;
    this.parseDataFromJSON(dataJson, nodeMap, sources, inlinings);
  }

  public addCustomData(customDataPhase: TurboshaftCustomDataPhase) {
    this.customData?.addCustomData(customDataPhase);
    const propertyName: string = "Properties";
    if(customDataPhase.dataTarget === DataTarget.Nodes &&
        customDataPhase.name === propertyName) {
      this.data.nodes.forEach(operation => operation.propertiesChanged(customDataPhase));
    }
  }

  private parseDataFromJSON(dataJson, nodeMap: Array<GraphNode | TurboshaftGraphOperation>,
                            sources: Array<Source>, inlinings: Array<InliningPosition>): void {
    this.data = new TurboshaftGraphData();
    this.parseBlocksFromJSON(dataJson.blocks);
    this.parseNodesFromJSON(dataJson.nodes, nodeMap, sources, inlinings);
    this.parseEdgesFromJSON(dataJson.edges);
  }

  private parseBlocksFromJSON(blocksJson): void {
    for (const blockJson of blocksJson) {
      const block = new TurboshaftGraphBlock(blockJson.id, blockJson.type,
        blockJson.deferred, blockJson.predecessors);
      this.data.blocks.push(block);
      this.blockIdToBlockMap[block.identifier()] = block;
    }
    for (const block of this.blockIdToBlockMap) {
      for (const [idx, predecessor] of block.predecessors.entries()) {
        const source = this.blockIdToBlockMap[predecessor];
        const edge = new TurboshaftGraphEdge(block, idx, source);
        block.inputs.push(edge);
        source.outputs.push(edge);
      }
    }
  }

  private parseNodesFromJSON(nodesJson, nodeMap: Array<GraphNode | TurboshaftGraphOperation>,
                             sources: Array<Source>, inlinings: Array<InliningPosition>): void {
    for (const nodeJson of nodesJson) {
      const block = this.blockIdToBlockMap[nodeJson.block_id];

      let sourcePosition: SourcePosition = null;
      const sourcePositionJson = nodeJson.sourcePosition;
      if (sourcePositionJson) {
        const scriptOffset = sourcePositionJson.scriptOffset;
        const inliningId = sourcePositionJson.inliningId;
        sourcePosition = new SourcePosition(scriptOffset, inliningId);
      }

      let origin: NodeOrigin = null;
      let bytecodePosition: BytecodePosition = null;
      const originJson = nodeJson.origin;
      if (originJson) {
        const nodeId = originJson.nodeId;
        const originNode = nodeMap[nodeId];
        origin = new NodeOrigin(nodeId, originNode, originJson.phase, originJson.reducer);
        if (originNode) {
          if (originNode instanceof GraphNode) {
            bytecodePosition = originNode.nodeLabel.bytecodePosition;
          } else {
            bytecodePosition = originNode.bytecodePosition;
          }
        }
      }

      const node = new TurboshaftGraphOperation(nodeJson.id, nodeJson.title, block, sourcePosition,
        bytecodePosition, origin, nodeJson.op_effects);

      block.nodes.push(node);
      this.data.nodes.push(node);
      this.nodeIdToNodeMap[node.identifier()] = node;
      this.highestNodeId = Math.max(this.highestNodeId, node.id);

      if (origin) {
        const identifier = origin.identifier();
        if (!this.originIdToNodesMap.has(identifier)) {
          this.originIdToNodesMap.set(identifier, new Array<TurboshaftGraphOperation>());
        }
        this.originIdToNodesMap.get(identifier).push(node);
      }

      nodeMap[node.id] = node;

      if (sourcePosition) {
        const inlining = inlinings[sourcePosition.inliningId];
        if (inlining) sources[inlining.sourceId].sourcePositions.push(sourcePosition);
        this.positions.addSourcePosition(node.identifier(), sourcePosition);
      }

      if (bytecodePosition) {
        this.positions.addBytecodePosition(node.identifier(), bytecodePosition);
      }
    }
    for (const block of this.blockIdToBlockMap) {
      block.initCollapsedLabel();
    }
  }

  private parseEdgesFromJSON(edgesJson): void {
    for (const edgeJson of edgesJson) {
      const target = this.nodeIdToNodeMap[edgeJson.target];
      const source = this.nodeIdToNodeMap[edgeJson.source];
      const edge = new TurboshaftGraphEdge(target, -1, source);
      this.data.edges.push(edge);
      target.inputs.push(edge);
      source.outputs.push(edge);
    }
    for (const node of this.data.nodes) {
      node.initDisplayLabel();
    }
  }
}

export class TurboshaftGraphData {
  nodes: Array<TurboshaftGraphOperation>;
  edges: Array<TurboshaftGraphEdge<TurboshaftGraphOperation>>;
  blocks: Array<TurboshaftGraphBlock>;

  constructor() {
    this.nodes = new Array<TurboshaftGraphOperation>();
    this.edges = new Array<TurboshaftGraphEdge<TurboshaftGraphOperation>>();
    this.blocks = new Array<TurboshaftGraphBlock>();
  }
}

export class TurboshaftCustomData {
  nodes: Map<string, TurboshaftCustomDataPhase>;
  blocks: Map<string, TurboshaftCustomDataPhase>;

  constructor() {
    this.nodes = new Map<string, TurboshaftCustomDataPhase>();
    this.blocks = new Map<string, TurboshaftCustomDataPhase>();
  }

  public addCustomData(customDataPhase: TurboshaftCustomDataPhase): void {
    switch (customDataPhase.dataTarget) {
      case DataTarget.Nodes:
        this.nodes.set(customDataPhase.name, customDataPhase);
        break;
      case DataTarget.Blocks:
        this.blocks.set(customDataPhase.name, customDataPhase);
        break;
      default:
        throw "Unsupported turboshaft custom data target type";
    }
  }

  public getTitle(key: number, dataTarget: DataTarget): string {
    switch (dataTarget) {
      case DataTarget.Nodes:
        return this.concatCustomData(key, this.nodes);
      case DataTarget.Blocks:
        return this.concatCustomData(key, this.blocks);
    }
  }

  private concatCustomData(key: number, items: Map<string, TurboshaftCustomDataPhase>): string {
    let customData = "";
    for (const [name, dataPhase] of items.entries()) {
      if (dataPhase.data[key] && dataPhase.data[key].length > 0) {
        customData += `\n${name}: ${dataPhase.data[key]}`;
      }
    }
    return customData;
  }
}
