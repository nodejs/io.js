// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import * as C from "./common/constants";
import { MovableContainer } from "./movable-container";
import { TurboshaftGraphOperation } from "./phases/turboshaft-graph-phase/turboshaft-graph-operation";
import { TurboshaftGraphBlock } from "./phases/turboshaft-graph-phase/turboshaft-graph-block";
import { TurboshaftGraphEdge } from "./phases/turboshaft-graph-phase/turboshaft-graph-edge";
import { DataTarget } from "./phases/turboshaft-custom-data-phase";
import {
  TurboshaftCustomData,
  TurboshaftGraphPhase
} from "./phases/turboshaft-graph-phase/turboshaft-graph-phase";

export class TurboshaftGraph extends MovableContainer<TurboshaftGraphPhase> {
  blockMap: Array<TurboshaftGraphBlock>;
  nodeMap: Array<TurboshaftGraphOperation>;
  customData: TurboshaftCustomData;

  constructor(graphPhase: TurboshaftGraphPhase) {
    super(graphPhase);
    this.blockMap = graphPhase.blockIdToBlockMap;
    this.nodeMap = graphPhase.nodeIdToNodeMap;
    this.customData = graphPhase.customData;
  }

  public *blocks(func = (b: TurboshaftGraphBlock) => true) {
    for (const block of this.blockMap) {
      if (!block || !func(block)) continue;
      yield block;
    }
  }

  public *nodes(func = (n: TurboshaftGraphOperation) => true) {
    for (const node of this.nodeMap) {
      if (!node || !func(node)) continue;
      yield node;
    }
  }

  public *blocksEdges(func = (e: TurboshaftGraphEdge<TurboshaftGraphBlock>) => true) {
    for (const block of this.blockMap) {
      if (!block) continue;
      for (const edge of block.inputs) {
        if (!edge || !func(edge)) continue;
        yield edge;
      }
    }
  }

  public *nodesEdges(func = (e: TurboshaftGraphEdge<TurboshaftGraphOperation>) => true) {
    for (const block of this.nodeMap) {
      if (!block) continue;
      for (const edge of block.inputs) {
        if (!edge || !func(edge)) continue;
        yield edge;
      }
    }
  }

  public redetermineGraphBoundingBox(showCustomData: boolean):
    [[number, number], [number, number]] {
    this.minGraphX = 0;
    this.maxGraphNodeX = 1;
    this.minGraphY = 0;
    this.maxGraphY = 1;

    for (const block of this.blocks()) {
      this.minGraphX = Math.min(this.minGraphX, block.x);
      this.maxGraphNodeX = Math.max(this.maxGraphNodeX, block.x + block.getWidth());

      this.minGraphY = Math.min(this.minGraphY, block.y - C.NODE_INPUT_WIDTH);
      this.maxGraphY = Math.max(this.maxGraphY, block.y + block.getHeight(showCustomData, false)
        + C.NODE_INPUT_WIDTH);
    }

    this.maxGraphX = this.maxGraphNodeX + this.maxBackEdgeNumber * C.MINIMUM_EDGE_SEPARATION;

    this.width = this.maxGraphX - this.minGraphX;
    this.height = this.maxGraphY - this.minGraphY;

    return [
      [this.minGraphX - this.width / 2, this.minGraphY - this.height / 2],
      [this.maxGraphX + this.width / 2, this.maxGraphY + this.height / 2]
    ];
  }

  public hasCustomData(customData: string, dataTarget: DataTarget): boolean {
    switch (dataTarget) {
      case DataTarget.Nodes:
        return this.customData.nodes.has(customData);
      case DataTarget.Blocks:
        return this.customData.blocks.has(customData);
    }
  }

  public getCustomData(customData: string, key: number, dataTarget: DataTarget): string {
    switch (dataTarget) {
      case DataTarget.Nodes:
        return this.customData.nodes.get(customData).data[key];
      case DataTarget.Blocks:
        return this.customData.blocks.get(customData).data[key];
    }
  }

  public getRanksMaxBlockHeight(showCustomData: boolean): Array<number> {
    const ranksMaxBlockHeight = new Array<number>();

    for (const block of this.blocks()) {
      ranksMaxBlockHeight[block.rank] = Math.max(ranksMaxBlockHeight[block.rank] ?? 0,
        block.getHeight(showCustomData, false));
    }

    return ranksMaxBlockHeight;
  }
}
