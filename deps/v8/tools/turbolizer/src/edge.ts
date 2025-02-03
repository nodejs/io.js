// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import * as C from "./common/constants";
import { GraphNode } from "./phases/graph-phase/graph-node";
import { TurboshaftGraphOperation } from "./phases/turboshaft-graph-phase/turboshaft-graph-operation";
import { TurboshaftGraphBlock } from "./phases/turboshaft-graph-phase/turboshaft-graph-block";
import { Graph } from "./graph";
import { TurboshaftGraph } from "./turboshaft-graph";

export abstract class Edge<NodeType extends GraphNode | TurboshaftGraphOperation
  | TurboshaftGraphBlock> {
  target: NodeType;
  source: NodeType;
  index: number;
  backEdgeNumber: number;
  visible: boolean;

  constructor(target: NodeType, index: number, source: NodeType) {
    this.target = target;
    this.index = index;
    this.source = source;
    this.backEdgeNumber = 0;
    this.visible = false;
  }

  public getInputHorizontalPosition(graph: Graph | TurboshaftGraph, extendHeight: boolean): number {
    if (graph.graphPhase.rendered && this.backEdgeNumber > 0) {
      return graph.maxGraphNodeX + this.backEdgeNumber * C.MINIMUM_EDGE_SEPARATION;
    }
    const source = this.source;
    const target = this.target;
    const index = this.index;
    const inputX = target.x + target.getInputX(index);
    const inputApproach = target.getInputApproach(this.index);
    const outputApproach = source.getOutputApproach(extendHeight);
    if (inputApproach > outputApproach) return inputX;
    const inputOffset = C.MINIMUM_EDGE_SEPARATION * (index + 1);
    return target.x < source.x
      ? target.x + target.getWidth() + inputOffset
      : target.x - inputOffset;
  }

  public generatePath(graph: Graph | TurboshaftGraph, extendHeight: boolean, compactView: boolean = false): string {
    const target = this.target;
    const source = this.source;
    const inputX = target.x + target.getInputX(this.index);
    const inputY = target.y - 2 * C.DEFAULT_NODE_BUBBLE_RADIUS - C.ARROW_HEAD_HEIGHT;
    const outputX = source.x + source.getOutputX();
    const outputY = source.y + source.getHeight(extendHeight, compactView) + C.DEFAULT_NODE_BUBBLE_RADIUS;
    let inputApproach = target.getInputApproach(this.index);
    const outputApproach = source.getOutputApproach(extendHeight);
    const horizontalPos = this.getInputHorizontalPosition(graph, extendHeight);

    let path: string;

    if (inputY < outputY) {
      path = `M ${outputX} ${outputY}\nL ${outputX} ${outputApproach}\nL ${horizontalPos} ${outputApproach}`;
      if (horizontalPos !== inputX) {
        path += `L ${horizontalPos} ${inputApproach}`;
      } else if (inputApproach < outputApproach) {
        inputApproach = outputApproach;
      }
      path += `L ${inputX} ${inputApproach}\nL ${inputX} ${inputY}`;
    } else {
      const controlY = outputY + (inputY - outputY) * C.BEZIER_CONSTANT;
      path = `M ${outputX} ${outputY}\nC ${outputX} ${controlY},\n${inputX} ${outputY},\n${inputX} ${inputY}`;
    }

    return path;
  }

  public isVisible(): boolean {
    return this.visible && this.source.visible && this.target.visible;
  }

  public toString(): string {
    return `${this.source.id},${this.index},${this.target.id}`;
  }
}
