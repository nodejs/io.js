
import { describe, it } from "node:test";

describe("sequential", {concurrency: false }, () => {
  it("first", async () => {
    throw new Error("First test failed");
  });

  it("second", () => {
    throw new Error("Second test should not run");
  });
})
