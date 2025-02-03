"use strict";
import { isSwcError, wrapAndReThrowSwcError } from "./errors.js";
import { transformSync } from "./index.js";
export async function load(url, context, nextLoad) {
  const { format } = context;
  if (format?.endsWith("-typescript")) {
    try {
      const { source } = await nextLoad(url, {
        ...context,
        format: "module"
      });
      const { code } = transformSync(source.toString(), {
        mode: "strip-only"
      });
      return {
        format: format.replace("-typescript", ""),
        // Source map is not necessary in strip-only mode. However, to map the source
        // file in debuggers to the original TypeScript source, add a sourceURL magic
        // comment to hint that it is a generated source.
        source: `${code}

//# sourceURL=${url}`
      };
    } catch (error) {
      if (isSwcError(error)) {
        wrapAndReThrowSwcError(error);
      }
      throw error;
    }
  }
  return nextLoad(url, context);
}
