const sources = {
// Virtual file. Doesn't exist on disk
  "virtual.js": `
  import { test } from 'node:test';
  test('test', async () => {});
`,
// file with source map. this emulates the behavior of tsx
  "sum.test.ts": `\
import{describe,it}from"node:test";import assert from"node:assert";import{sum}from"./sum.ts";describe("sum",()=>{it("should sum two numbers",()=>{assert.deepStrictEqual(sum(1,2),3)});it("should error out if one is not a number",()=>{assert.throws(()=>sum(1,"b"),Error)})});

//# sourceMappingURL=data:application/json;base64,eyJ2ZXJzaW9uIjozLCJtYXBwaW5ncyI6IkFBQUEsT0FBUyxTQUFVLE9BQVUsWUFDN0IsT0FBTyxXQUFZLGNBQ25CLE9BQVMsUUFBVyxRQUVwQixTQUFTLE1BQU8sSUFBTSxDQUNwQixHQUFHLHlCQUEwQixJQUFNLENBQy9CLE9BQU8sZ0JBQWdCLElBQUksRUFBRSxDQUFDLEVBQUcsQ0FBQyxDQUN0QyxDQUFDLEVBRUQsR0FBRywwQ0FBMkMsSUFBTSxDQUNsRCxPQUFPLE9BQU8sSUFBTSxJQUFJLEVBQUcsR0FBRyxFQUFHLEtBQUssQ0FDeEMsQ0FBQyxDQUNILENBQUMiLCJuYW1lcyI6W10sImlnbm9yZUxpc3QiOltdLCJzb3VyY2VzIjpbIi4vc3VtLnRlc3QudHMiXSwic291cmNlc0NvbnRlbnQiOltudWxsXX0=`,
// file with source map. this emulates the behavior of tsx
  "sum.ts": `\
  var __defProp=Object.defineProperty;var __name=(target,value)=>__defProp(target,"name",{value,configurable:true});function sum(...n){if(!n.every(num=>typeof num==="number"))throw new Error("Not a number");return n.reduce((acc,cur)=>acc+cur)}__name(sum,"sum");export{sum};

  //# sourceMappingURL=data:application/json;base64,eyJ2ZXJzaW9uIjozLCJtYXBwaW5ncyI6ImtIQUFPLFNBQVMsT0FBUSxFQUFHLENBQ3pCLEdBQUksQ0FBQyxFQUFFLE1BQU8sS0FBUSxPQUFPLE1BQVEsUUFBUSxFQUFHLE1BQU0sSUFBSSxNQUFNLGNBQWMsRUFDOUUsT0FBTyxFQUFFLE9BQU8sQ0FBQyxJQUFLLE1BQVEsSUFBTSxHQUFHLENBQ3pDLENBSGdCIiwibmFtZXMiOltdLCJpZ25vcmVMaXN0IjpbXSwic291cmNlcyI6WyIuL3N1bS50cyJdLCJzb3VyY2VzQ29udGVudCI6W251bGxdfQ==`,
};

export async function load(url, context, nextLoad) {
  const file = url.split('/').at(-1);
  if (sources[file] !== undefined) {
    return { format: "module", source: sources[file], shortCircuit: true };
  }
  return nextLoad(url, context);
}
