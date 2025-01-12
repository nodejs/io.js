import { describe, it } from "node:test";

describe("sequential", async () => {
  it("first", async () => {
    await new Promise(resolve => setTimeout(resolve, 2000));
  });

  it("second", () => {
    throw new Error("Second test should not run");
  });
})
