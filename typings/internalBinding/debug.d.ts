/**
 * The `internalBinding('debug')` binding provides access to internal debugging
 * utilities. They are only available when Node.js is built with the `--debug`
 * or `--debug-node` compile-time flags.
 */
export interface DebugBinding {
  getV8FastApiCallCount(name: string): number;
  isEven(value: number): boolean;
  isOdd(value: number): boolean;
}
