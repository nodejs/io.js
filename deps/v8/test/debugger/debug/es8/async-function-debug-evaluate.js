// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --noalways-turbofan

var Debug = debug.Debug;
var breakPointCount = 0;

function listener(event, exec_state, event_data, data) {
  if (event != Debug.DebugEvent.Break) return;
  ++breakPointCount;
  try {
    if (breakPointCount === 1) {
      assertEquals(
          "inner", exec_state.frame(0).evaluate("inner").value());
      // Variables in TDZ should throw an 'unavailable in debugger' error.
      assertThrows(() => exec_state.frame(0).evaluate("letInner"));
      assertThrows(() => exec_state.frame(0).evaluate("constInner"));

      assertEquals("outer", exec_state.frame(0).evaluate("outer").value());
      assertEquals(
          "const outer", exec_state.frame(0).evaluate("constOuter").value());
      assertEquals(
          "let outer", exec_state.frame(0).evaluate("letOuter").value());

      assertEquals("outer", exec_state.frame(1).evaluate("outer").value());
      assertEquals(
          "const outer", exec_state.frame(1).evaluate("constOuter").value());
      assertEquals(
          "let outer", exec_state.frame(1).evaluate("letOuter").value());

      // Variables in TDZ should throw an 'unavailable in debugger' error.
      assertThrows(() => exec_state.frame(0).evaluate("withVar"));

    } else if (breakPointCount === 2) {
      assertEquals(
          "inner", exec_state.frame(0).evaluate("inner").value());
      // Variables in TDZ should throw an 'unavailable in debugger' error.
      assertThrows(() => exec_state.frame(0).evaluate("letInner"));
      assertThrows(() => exec_state.frame(0).evaluate("constInner"));

      assertEquals(57, exec_state.frame(0).evaluate("x").value());
      assertEquals(100, exec_state.frame(0).evaluate("y").value());

      // From breakPointCount === 1 and later, it's not possible to access
      // earlier framestates.
      assertEquals("outer", exec_state.frame(0).evaluate("outer").value());
      assertEquals(
          "const outer", exec_state.frame(0).evaluate("constOuter").value());
      assertEquals(
          "let outer", exec_state.frame(0).evaluate("letOuter").value());

      exec_state.frame(0).evaluate("x = `x later(${x})`");
      exec_state.frame(0).evaluate("y = `y later(${y})`");
      exec_state.frame(0).evaluate("z = `ZEE`");

    } else if (breakPointCount === 3) {
      assertEquals(
          "inner", exec_state.frame(0).evaluate("inner").value());
      assertEquals(
          "let inner", exec_state.frame(0).evaluate("letInner").value());
      assertEquals(
          "const inner", exec_state.frame(0).evaluate("constInner").value());

    } else if (breakPointCount === 4) {
      assertEquals(
          "oop", exec_state.frame(0).evaluate("error.message").value());
      assertEquals(
          "Error",
          exec_state.frame(0).evaluate("error.constructor.name").value());
      assertEquals("floof", exec_state.frame(0).evaluate("bun").value());
      // Variables in TDZ should throw an 'unavailable in debugger' error.
      assertThrows(() => exec_state.frame(0).evaluate("cow"));

      assertEquals("outer", exec_state.frame(0).evaluate("outer").value());
      assertEquals(
          "const outer", exec_state.frame(0).evaluate("constOuter").value());
      assertEquals(
          "let outer", exec_state.frame(0).evaluate("letOuter").value());
    }
  } catch (e) {
    print(e.stack);
    quit(1);
  }
}

Debug.setListener(listener);

var outer = "outer";
const constOuter = "const outer";
let letOuter = "let outer"

async function thrower() {
  return Promise.reject(new Error("oop"));
}

async function testLater() {
  return { x: 57, y: 100 };
}

async function test() {
  var inner = "inner";
  debugger;

  let withVar = await testLater();
  with (withVar) {
    debugger;
  }

  assertEquals("x later(57)", withVar.x);
  assertEquals("y later(100)", withVar.y);
  assertEquals(undefined, withVar.z);
  assertEquals("ZEE", z);

  let letInner = "let inner";
  const constInner = "const inner";
  debugger;

  try {
    await thrower();
  } catch (error) {
    const bun = "floof";
    debugger;
    let cow = "moo";
  }
}

test().
then(x => {
  Debug.setListener(null);
}).
catch(error => {
  print(error.stack);
  quit(1);
  Debug.setListener(null);
});
