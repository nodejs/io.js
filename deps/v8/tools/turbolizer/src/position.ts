// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { TurboshaftGraphOperation } from "./phases/turboshaft-graph-phase/turboshaft-graph-operation";

export class InliningPosition {
  sourceId: number;
  inliningPosition: SourcePosition;

  constructor(sourceId: number, inliningPosition: SourcePosition) {
    this.sourceId = sourceId;
    this.inliningPosition = inliningPosition;
  }
}

export class SourcePosition {
  scriptOffset: number;
  inliningId: number;

  constructor(scriptOffset: number, inliningId: number) {
    this.scriptOffset = scriptOffset;
    this.inliningId = inliningId;
  }

  public lessOrEquals(other: SourcePosition): number {
    if (this.inliningId == other.inliningId) {
      return this.scriptOffset - other.scriptOffset;
    }
    return this.inliningId - other.inliningId;
  }

  public equals(other: SourcePosition): boolean {
    return this.inliningId == other.inliningId && this.scriptOffset == other.scriptOffset;
  }

  public isValid(): boolean {
    return typeof this.scriptOffset !== undefined && typeof this.inliningId !== undefined;
  }

  public toString(): string {
    return `SP:${this.inliningId}:${this.scriptOffset}`;
  }
}

export class BytecodePosition {
  bytecodePosition: number;
  inliningId: number;

  constructor(bytecodePosition: number, inliningId: number) {
    this.bytecodePosition = bytecodePosition;
    this.inliningId = inliningId;
  }

  public isValid(): boolean {
    return typeof this.bytecodePosition !== undefined;
  }

  public toString(): string {
    return `BCP:${this.inliningId}:${this.bytecodePosition}`;
  }
}

export class PositionsContainer {
  nodeIdToSourcePositionMap: Array<SourcePosition>;
  nodeIdToBytecodePositionMap: Array<BytecodePosition>;
  sourcePositionToNodes: Map<string, Array<string>>;
  bytecodePositionToNodes: Map<string, Array<string>>;

  constructor() {
    this.nodeIdToSourcePositionMap = new Array<SourcePosition>();
    this.nodeIdToBytecodePositionMap = new Array<BytecodePosition>();
    this.sourcePositionToNodes = new Map<string, Array<string>>();
    this.bytecodePositionToNodes = new Map<string, Array<string>>();
  }

  public addSourcePosition(nodeIdentifier: string, sourcePosition: SourcePosition): void {
    this.nodeIdToSourcePositionMap[nodeIdentifier] = sourcePosition;
    const key = sourcePosition.toString();
    if (!this.sourcePositionToNodes.has(key)) {
      this.sourcePositionToNodes.set(key, new Array<string>());
    }
    const nodes = this.sourcePositionToNodes.get(key);
    if (!nodes.includes(nodeIdentifier)) nodes.push(nodeIdentifier);
  }

  public addBytecodePosition(nodeIdentifier: string, bytecodePosition: BytecodePosition): void {
    this.nodeIdToBytecodePositionMap[nodeIdentifier] = bytecodePosition;
    const key = bytecodePosition.toString();
    if (!this.bytecodePositionToNodes.has(key)) {
      this.bytecodePositionToNodes.set(key, new Array<string>());
    }
    const nodes = this.bytecodePositionToNodes.get(key);
    if (!nodes.includes(nodeIdentifier)) nodes.push(nodeIdentifier);
  }

  public merge(nodes: Array<TurboshaftGraphOperation>, replacements: Map<number, number>): void {
    for (const node of nodes) {
      const sourcePosition = node.sourcePosition;
      const bytecodePosition = node.bytecodePosition;
      const nodeId = replacements.has(node.id) ? replacements.get(node.id) : node.id;
      if (sourcePosition && !this.nodeIdToSourcePositionMap[nodeId]) {
        this.addSourcePosition(String(nodeId), sourcePosition);
      }
      if (bytecodePosition && !this.nodeIdToBytecodePositionMap[nodeId]) {
        this.addBytecodePosition(String(nodeId), bytecodePosition);
      }
    }
  }
}
