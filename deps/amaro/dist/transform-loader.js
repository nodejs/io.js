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
      const { code, map } = transformSync(source.toString(), {
        mode: "transform",
        sourceMap: true,
        filename: url
      });
      let output = code;
      if (map) {
        const base64SourceMap = Buffer.from(map).toString("base64");
        output = `${code}

//# sourceMappingURL=data:application/json;base64,${base64SourceMap}`;
      }
      return {
        format: format.replace("-typescript", ""),
        source: `${output}

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
