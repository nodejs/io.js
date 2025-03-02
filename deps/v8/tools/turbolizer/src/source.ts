// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { SourcePosition } from "./position";

export class Source {
  sourceName: string;
  functionName: string;
  sourceText: string;
  sourceId: number;
  backwardsCompatibility: boolean;
  sourcePositions: Array<SourcePosition>;
  startPosition?: number;
  endPosition?: number;

  constructor(sourceName: string, functionName: string, sourceText: string, sourceId: number,
              backwardsCompatibility: boolean, startPosition?: number, endPosition?: number) {
    this.sourceName = sourceName;
    this.functionName = functionName;
    this.sourceText = sourceText;
    this.sourceId = sourceId;
    this.backwardsCompatibility = backwardsCompatibility;
    this.startPosition = startPosition;
    this.endPosition = endPosition;
    this.sourcePositions = new Array<SourcePosition>();
  }

  public toString(): string {
    return `${this.sourceName}:${this.functionName}`;
  }
}

export class BytecodeSource {
  sourceId: number;
  inliningIds: Array<number>;
  functionName: string;
  data: Array<BytecodeSourceData>;
  constantPool: Array<string>;
  feedbackVector: string;

  constructor(sourceId: number, inliningIds: Array<number>, functionName: string,
              data: Array<BytecodeSourceData>, constantPool: Array<string>,
              feedbackVector: string) {
    this.sourceId = sourceId;
    this.inliningIds = inliningIds;
    this.functionName = functionName;
    this.data = data;
    this.constantPool = constantPool;
    this.feedbackVector = feedbackVector;
  }
}

export class BytecodeSourceData {
  offset: number;
  disassembly: string;

  constructor(offset: number, disassembly: string) {
    this.offset = offset;
    this.disassembly = disassembly;
  }
}
