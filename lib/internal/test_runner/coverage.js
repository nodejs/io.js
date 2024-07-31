'use strict';
const {
  ArrayFrom,
  ArrayPrototypeMap,
  ArrayPrototypePush,
  JSONParse,
  MathFloor,
  MathMax,
  MathMin,
  NumberParseInt,
  ObjectAssign,
  RegExpPrototypeExec,
  RegExpPrototypeSymbolSplit,
  SafeMap,
  SafeSet,
  StringPrototypeIncludes,
  StringPrototypeLocaleCompare,
  StringPrototypeStartsWith,
} = primordials;
const {
  copyFileSync,
  mkdirSync,
  mkdtempSync,
  opendirSync,
  readFileSync,
} = require('fs');
const { setupCoverageHooks } = require('internal/util');
const { tmpdir } = require('os');
const { join, resolve, relative, matchesGlob } = require('path');
const { fileURLToPath } = require('internal/url');
const { kMappings, SourceMap } = require('internal/source_map/source_map');
const kCoverageFileRegex = /^coverage-(\d+)-(\d{13})-(\d+)\.json$/;
const kIgnoreRegex = /\/\* node:coverage ignore next (?<count>\d+ )?\*\//;
const kLineEndingRegex = /\r?\n$/u;
const kLineSplitRegex = /(?<=\r?\n)/u;
const kStatusRegex = /\/\* node:coverage (?<status>enable|disable) \*\//;

class CoverageLine {
  constructor(line, startOffset, src, length = src?.length) {
    const newlineLength = src == null ? 0 :
      RegExpPrototypeExec(kLineEndingRegex, src)?.[0].length ?? 0;

    this.line = line;
    this.src = src;
    this.startOffset = startOffset;
    this.endOffset = startOffset + length - newlineLength;
    this.ignore = false;
    this.count = this.startOffset === this.endOffset ? 1 : 0;
  }
}

class TestCoverage {
  constructor(coverageDirectory, originalCoverageDirectory, workingDirectory, excludeGlobs, includeGlobs) {
    this.coverageDirectory = coverageDirectory;
    this.originalCoverageDirectory = originalCoverageDirectory;
    this.workingDirectory = workingDirectory;
    this.excludeGlobs = excludeGlobs;
    this.includeGlobs = includeGlobs;
  }

  #sourceLines = new SafeMap();

  getLines(fileUrl, source) {
    // Split the file source into lines. Make sure the lines maintain their
    // original line endings because those characters are necessary for
    // determining offsets in the file.
    if (this.#sourceLines.has(fileUrl)) {
      return this.#sourceLines.get(fileUrl);
    }

    try {
      source ??= readFileSync(fileURLToPath(fileUrl), 'utf8');
    } catch {
      // The file can no longer be read. It may have been deleted among
      // other possibilities. Leave it out of the coverage report.
      this.#sourceLines.set(fileUrl, null);
      return;
    }

    const linesWithBreaks =
      RegExpPrototypeSymbolSplit(kLineSplitRegex, source);
    let ignoreCount = 0;
    let enabled = true;
    let offset = 0;

    const lines = ArrayPrototypeMap(linesWithBreaks, (line, i) => {
      const startOffset = offset;
      const coverageLine = new CoverageLine(i + 1, startOffset, line);

      offset += line.length;

      // Determine if this line is being ignored.
      if (ignoreCount > 0) {
        ignoreCount--;
        coverageLine.ignore = true;
      } else if (!enabled) {
        coverageLine.ignore = true;
      }

      if (!coverageLine.ignore) {
        // If this line is not already being ignored, check for ignore
        // comments.
        const match = RegExpPrototypeExec(kIgnoreRegex, line);

        if (match !== null) {
          ignoreCount = NumberParseInt(match.groups?.count ?? 1, 10);
        }
      }

      // Check for comments to enable/disable coverage no matter what. These
      // take precedence over ignore comments.
      const match = RegExpPrototypeExec(kStatusRegex, line);
      const status = match?.groups?.status;

      if (status) {
        ignoreCount = 0;
        enabled = status === 'enable';
      }

      return coverageLine;
    });
    this.#sourceLines.set(fileUrl, lines);
    return lines;
  }

  summary() {
    internalBinding('profiler').takeCoverage();
    const coverage = this.getCoverageFromDirectory();
    const coverageSummary = {
      __proto__: null,
      workingDirectory: this.workingDirectory,
      files: [],
      totals: {
        __proto__: null,
        totalLineCount: 0,
        totalBranchCount: 0,
        totalFunctionCount: 0,
        coveredLineCount: 0,
        coveredBranchCount: 0,
        coveredFunctionCount: 0,
        coveredLinePercent: 0,
        coveredBranchPercent: 0,
        coveredFunctionPercent: 0,
      },
    };

    if (!coverage) {
      return coverageSummary;
    }

    for (let i = 0; i < coverage.length; ++i) {
      const { functions, url } = coverage[i];

      let totalBranches = 0;
      let totalFunctions = 0;
      let branchesCovered = 0;
      let functionsCovered = 0;
      const functionReports = [];
      const branchReports = [];

      const lines = this.getLines(url);
      if (!lines) {
        continue;
      }


      for (let j = 0; j < functions.length; ++j) {
        const { isBlockCoverage, ranges } = functions[j];

        let maxCountPerFunction = 0;
        for (let k = 0; k < ranges.length; ++k) {
          const range = ranges[k];
          maxCountPerFunction = MathMax(maxCountPerFunction, range.count);

          // Add some useful data to the range. The test runner has read these ranges
          // from a file, so we own the data structures and can do what we want.
          ObjectAssign(range, mapRangeToLines(range, lines));

          if (isBlockCoverage) {
            ArrayPrototypePush(branchReports, {
              __proto__: null,
              line: range.lines[0]?.line,
              count: range.count,
            });

            if (range.count !== 0 ||
                range.ignoredLines === range.lines.length) {
              branchesCovered++;
            }

            totalBranches++;
          }
        }

        if (j > 0 && ranges.length > 0) {
          const range = ranges[0];

          ArrayPrototypePush(functionReports, {
            __proto__: null,
            name: functions[j].functionName,
            count: maxCountPerFunction,
            line: range.lines[0]?.line,
          });

          if (range.count !== 0 || range.ignoredLines === range.lines.length) {
            functionsCovered++;
          }

          totalFunctions++;
        }
      }

      let coveredCnt = 0;
      const lineReports = [];

      for (let j = 0; j < lines.length; ++j) {
        const line = lines[j];
        if (!line.ignore) {
          ArrayPrototypePush(lineReports, {
            __proto__: null,
            line: line.line,
            count: line.count,
          });
        }
        if (line.count > 0 || line.ignore) {
          coveredCnt++;
        }
      }

      ArrayPrototypePush(coverageSummary.files, {
        __proto__: null,
        path: fileURLToPath(url),
        totalLineCount: lines.length,
        totalBranchCount: totalBranches,
        totalFunctionCount: totalFunctions,
        coveredLineCount: coveredCnt,
        coveredBranchCount: branchesCovered,
        coveredFunctionCount: functionsCovered,
        coveredLinePercent: toPercentage(coveredCnt, lines.length),
        coveredBranchPercent: toPercentage(branchesCovered, totalBranches),
        coveredFunctionPercent: toPercentage(functionsCovered, totalFunctions),
        functions: functionReports,
        branches: branchReports,
        lines: lineReports,
      });

      coverageSummary.totals.totalLineCount += lines.length;
      coverageSummary.totals.totalBranchCount += totalBranches;
      coverageSummary.totals.totalFunctionCount += totalFunctions;
      coverageSummary.totals.coveredLineCount += coveredCnt;
      coverageSummary.totals.coveredBranchCount += branchesCovered;
      coverageSummary.totals.coveredFunctionCount += functionsCovered;
    }

    coverageSummary.totals.coveredLinePercent = toPercentage(
      coverageSummary.totals.coveredLineCount,
      coverageSummary.totals.totalLineCount,
    );
    coverageSummary.totals.coveredBranchPercent = toPercentage(
      coverageSummary.totals.coveredBranchCount,
      coverageSummary.totals.totalBranchCount,
    );
    coverageSummary.totals.coveredFunctionPercent = toPercentage(
      coverageSummary.totals.coveredFunctionCount,
      coverageSummary.totals.totalFunctionCount,
    );
    coverageSummary.files.sort(sortCoverageFiles);

    return coverageSummary;
  }

  cleanup() {
    // Restore the original value of process.env.NODE_V8_COVERAGE. Then, copy
    // all of the created coverage files to the original coverage directory.
    if (this.originalCoverageDirectory === undefined) {
      delete process.env.NODE_V8_COVERAGE;
      return;
    }

    process.env.NODE_V8_COVERAGE = this.originalCoverageDirectory;
    let dir;

    try {
      mkdirSync(this.originalCoverageDirectory, { __proto__: null, recursive: true });
      dir = opendirSync(this.coverageDirectory);

      for (let entry; (entry = dir.readSync()) !== null;) {
        const src = join(this.coverageDirectory, entry.name);
        const dst = join(this.originalCoverageDirectory, entry.name);
        copyFileSync(src, dst);
      }
    } finally {
      if (dir) {
        dir.closeSync();
      }
    }
  }

  getCoverageFromDirectory() {
    const result = new SafeMap();
    let dir;

    try {
      dir = opendirSync(this.coverageDirectory);

      for (let entry; (entry = dir.readSync()) !== null;) {
        if (RegExpPrototypeExec(kCoverageFileRegex, entry.name) === null) {
          continue;
        }

        const coverageFile = join(this.coverageDirectory, entry.name);
        const coverage = JSONParse(readFileSync(coverageFile, 'utf8'));
        this.mergeCoverage(result, this.mapCoverageWithSourceMap(coverage));
      }

      return ArrayFrom(result.values());
    } finally {
      if (dir) {
        dir.closeSync();
      }
    }
  }


  mapCoverageWithSourceMap(coverage) {
    const { result } = coverage;
    const sourceMapCache = coverage['source-map-cache'];
    if (!sourceMapCache) {
      return result;
    }
    const newResult = new SafeMap();
    for (let i = 0; i < result.length; ++i) {
      const script = result[i];
      const { url, functions } = script;

      if (this.shouldSkipFileCoverage(url) || sourceMapCache[url] == null) {
        newResult.set(url, script);
        continue;
      }
      const { data, lineLengths } = sourceMapCache[url];
      let offset = 0;
      const executedLines = ArrayPrototypeMap(lineLengths, (length, i) => {
        const coverageLine = new CoverageLine(i + 1, offset, null, length);
        offset += length;
        return coverageLine;
      });
      if (data.sourcesContent != null) {
        for (let j = 0; j < data.sources.length; ++j) {
          this.getLines(data.sources[j], data.sourcesContent[j]);
        }
      }
      const sourceMap = new SourceMap(data, { __proto__: null, lineLengths });

      for (let j = 0; j < functions.length; ++j) {
        const { ranges, functionName, isBlockCoverage } = functions[j];
        if (ranges == null) {
          continue;
        }
        let newUrl;
        const newRanges = [];
        for (let k = 0; k < ranges.length; ++k) {
          const { startOffset, endOffset, count } = ranges[k];
          const { lines } = mapRangeToLines(ranges[k], executedLines);

          let startEntry = sourceMap
            .findEntry(lines[0].line - 1, MathMax(0, startOffset - lines[0].startOffset));
          const endEntry = sourceMap
            .findEntry(lines[lines.length - 1].line - 1, (endOffset - lines[lines.length - 1].startOffset) - 1);
          if (!startEntry.originalSource && endEntry.originalSource &&
            lines[0].line === 1 && startOffset === 0 && lines[0].startOffset === 0) {
            // Edge case when the first line is not mappable
            const { 2: originalSource, 3: originalLine, 4: originalColumn } = sourceMap[kMappings][0];
            startEntry = { __proto__: null, originalSource, originalLine, originalColumn };
          }

          if (!startEntry.originalSource || startEntry.originalSource !== endEntry.originalSource) {
            // The range is not mappable. Skip it.
            continue;
          }

          newUrl ??= startEntry?.originalSource;
          const mappedLines = this.getLines(newUrl);
          const mappedStartOffset = this.entryToOffset(startEntry, mappedLines);
          const mappedEndOffset = this.entryToOffset(endEntry, mappedLines) + 1;
          for (let l = startEntry.originalLine; l <= endEntry.originalLine; l++) {
            mappedLines[l].count = count;
          }

          ArrayPrototypePush(newRanges, {
            __proto__: null, startOffset: mappedStartOffset, endOffset: mappedEndOffset, count,
          });
        }

        if (!newUrl) {
          // No mappable ranges. Skip the function.
          continue;
        }
        const newScript = newResult.get(newUrl) ?? { __proto__: null, url: newUrl, functions: [] };
        ArrayPrototypePush(newScript.functions, { __proto__: null, functionName, ranges: newRanges, isBlockCoverage });
        newResult.set(newUrl, newScript);
      }
    }

    return ArrayFrom(newResult.values());
  }

  entryToOffset(entry, lines) {
    const line = MathMax(entry.originalLine, 0);
    return MathMin(lines[line].startOffset + entry.originalColumn, lines[line].endOffset);
  }

  mergeCoverage(merged, coverage) {
    for (let i = 0; i < coverage.length; ++i) {
      const newScript = coverage[i];
      const { url } = newScript;

      if (this.shouldSkipFileCoverage(url)) {
        continue;
      }

      const oldScript = merged.get(url);

      if (oldScript === undefined) {
        merged.set(url, newScript);
      } else {
        mergeCoverageScripts(oldScript, newScript);
      }
    }
  }

  shouldSkipFileCoverage(url) {
    // This check filters out core modules, which start with 'node:' in
    // coverage reports, as well as any invalid coverages which have been
    // observed on Windows.
    if (!StringPrototypeStartsWith(url, 'file:')) return true;

    const absolutePath = fileURLToPath(url);
    const relativePath = relative(this.workingDirectory, absolutePath);

    // This check filters out files that match the exclude globs.
    if (this.excludeGlobs?.length > 0) {
      for (let i = 0; i < this.excludeGlobs.length; ++i) {
        if (matchesGlob(relativePath, this.excludeGlobs[i]) ||
            matchesGlob(absolutePath, this.excludeGlobs[i])) return true;
      }
    }

    // This check filters out files that do not match the include globs.
    if (this.includeGlobs?.length > 0) {
      for (let i = 0; i < this.includeGlobs.length; ++i) {
        if (matchesGlob(relativePath, this.includeGlobs[i]) ||
            matchesGlob(absolutePath, this.includeGlobs[i])) return false;
      }
      return true;
    }

    // This check filters out the node_modules/ directory, unless it is explicitly included.
    return StringPrototypeIncludes(url, '/node_modules/');
  }
}

function toPercentage(covered, total) {
  return total === 0 ? 100 : (covered / total) * 100;
}

function sortCoverageFiles(a, b) {
  return StringPrototypeLocaleCompare(a.path, b.path);
}

function setupCoverage(options) {
  let originalCoverageDirectory = process.env.NODE_V8_COVERAGE;
  const cwd = process.cwd();

  if (originalCoverageDirectory) {
    // NODE_V8_COVERAGE was already specified. Convert it to an absolute path
    // and store it for later. The test runner will use a temporary directory
    // so that no preexisting coverage files interfere with the results of the
    // coverage report. Then, once the coverage is computed, move the coverage
    // files back to the original NODE_V8_COVERAGE directory.
    originalCoverageDirectory = resolve(cwd, originalCoverageDirectory);
  }

  const coverageDirectory = mkdtempSync(join(tmpdir(), 'node-coverage-'));
  const enabled = setupCoverageHooks(coverageDirectory);

  if (!enabled) {
    return null;
  }

  // Ensure that NODE_V8_COVERAGE is set so that coverage can propagate to
  // child processes.
  process.env.NODE_V8_COVERAGE = coverageDirectory;

  return new TestCoverage(
    coverageDirectory,
    originalCoverageDirectory,
    cwd,
    options.coverageExcludeGlobs,
    options.coverageIncludeGlobs,
  );
}

function mapRangeToLines(range, lines) {
  const { startOffset, endOffset, count } = range;
  const mappedLines = [];
  let ignoredLines = 0;
  let start = 0;
  let end = lines.length;
  let mid;

  while (start <= end) {
    mid = MathFloor((start + end) / 2);
    let line = lines[mid];

    if (startOffset >= line?.startOffset && startOffset <= line?.endOffset) {
      while (endOffset > line?.startOffset) {
        // If the range is not covered, and the range covers the entire line,
        // then mark that line as not covered.
        if (startOffset <= line.startOffset && endOffset >= line.endOffset) {
          line.count = count;
        }

        ArrayPrototypePush(mappedLines, line);

        if (line.ignore) {
          ignoredLines++;
        }

        mid++;
        line = lines[mid];
      }

      break;
    } else if (startOffset >= line?.endOffset) {
      start = mid + 1;
    } else {
      end = mid - 1;
    }
  }

  return { __proto__: null, lines: mappedLines, ignoredLines };
}

function mergeCoverageScripts(oldScript, newScript) {
  // Merge the functions from the new coverage into the functions from the
  // existing (merged) coverage.
  for (let i = 0; i < newScript.functions.length; ++i) {
    const newFn = newScript.functions[i];
    let found = false;

    for (let j = 0; j < oldScript.functions.length; ++j) {
      const oldFn = oldScript.functions[j];

      if (newFn.functionName === oldFn.functionName &&
          newFn.ranges?.[0].startOffset === oldFn.ranges?.[0].startOffset &&
          newFn.ranges?.[0].endOffset === oldFn.ranges?.[0].endOffset) {
        // These are the same functions.
        found = true;

        // If newFn is block level coverage, then it will:
        // - Replace oldFn if oldFn is not block level coverage.
        // - Merge with oldFn if it is also block level coverage.
        // If newFn is not block level coverage, then it has no new data.
        if (newFn.isBlockCoverage) {
          if (oldFn.isBlockCoverage) {
            // Merge the oldFn ranges with the newFn ranges.
            mergeCoverageRanges(oldFn, newFn);
          } else {
            // Replace oldFn with newFn.
            oldFn.isBlockCoverage = true;
            oldFn.ranges = newFn.ranges;
          }
        }

        break;
      }
    }

    if (!found) {
      // This is a new function to track. This is possible because V8 can
      // generate a different list of functions depending on which code paths
      // are executed. For example, if a code path dynamically creates a
      // function, but that code path is not executed then the function does
      // not show up in the coverage report. Unfortunately, this also means
      // that the function counts in the coverage summary can never be
      // guaranteed to be 100% accurate.
      ArrayPrototypePush(oldScript.functions, newFn);
    }
  }
}

function mergeCoverageRanges(oldFn, newFn) {
  const mergedRanges = new SafeSet();

  // Keep all of the existing covered ranges.
  for (let i = 0; i < oldFn.ranges.length; ++i) {
    const oldRange = oldFn.ranges[i];

    if (oldRange.count > 0) {
      mergedRanges.add(oldRange);
    }
  }

  // Merge in the new ranges where appropriate.
  for (let i = 0; i < newFn.ranges.length; ++i) {
    const newRange = newFn.ranges[i];
    let exactMatch = false;

    for (let j = 0; j < oldFn.ranges.length; ++j) {
      const oldRange = oldFn.ranges[j];

      if (doesRangeEqualOtherRange(newRange, oldRange)) {
        // These are the same ranges, so keep the existing one.
        oldRange.count += newRange.count;
        mergedRanges.add(oldRange);
        exactMatch = true;
        break;
      }

      // Look at ranges representing missing coverage and add ranges that
      // represent the intersection.
      if (oldRange.count === 0 && newRange.count === 0) {
        if (doesRangeContainOtherRange(oldRange, newRange)) {
          // The new range is completely within the old range. Discard the
          // larger (old) range, and keep the smaller (new) range.
          mergedRanges.add(newRange);
        } else if (doesRangeContainOtherRange(newRange, oldRange)) {
          // The old range is completely within the new range. Discard the
          // larger (new) range, and keep the smaller (old) range.
          mergedRanges.add(oldRange);
        }
      }
    }

    // Add new ranges that do not represent missing coverage.
    if (newRange.count > 0 && !exactMatch) {
      mergedRanges.add(newRange);
    }
  }

  oldFn.ranges = ArrayFrom(mergedRanges);
}

function doesRangeEqualOtherRange(range, otherRange) {
  return range.startOffset === otherRange.startOffset &&
         range.endOffset === otherRange.endOffset;
}

function doesRangeContainOtherRange(range, otherRange) {
  return range.startOffset <= otherRange.startOffset &&
         range.endOffset >= otherRange.endOffset;
}

module.exports = { setupCoverage, TestCoverage };
