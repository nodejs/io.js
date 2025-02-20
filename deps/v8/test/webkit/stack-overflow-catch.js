// Copyright 2013 the V8 project authors. All rights reserved.
// Copyright (C) 2005, 2006, 2007, 2008, 2009 Apple Inc. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1.  Redistributions of source code must retain the above copyright
//     notice, this list of conditions and the following disclaimer.
// 2.  Redistributions in binary form must reproduce the above copyright
//     notice, this list of conditions and the following disclaimer in the
//     documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
// ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

description('Test that when the stack overflows, the exception goes to the last frame before the overflow');

var level = 0;
var stackLevel = 0;
var gotWrongCatch = false;

function test1()
{
    var myLevel = level;
    var dummy;

    try {
        level = level + 1;
        // Dummy code to make this function different from test2()
        dummy = level * level + 1;
        if (dummy == 0)
            debug('Should never get here!!!!');
    } catch(err) {
        gotWrongCatch = true;
    }

    try {
        test2();
    } catch(err) {
        stackLevel = myLevel;
    }
}

function test2()
{
    var myLevel = level;

    // Dummy code to make this function different from test1()
    if (gotWrongCatch)
        debug('Should never get here!!!!');

    try {
        level = level + 1;
    } catch(err) {
        gotWrongCatch = true;
    }

    try {
        test1();
    } catch(err) {
        stackLevel = myLevel;
    }
}

test1();

shouldBeFalse("gotWrongCatch");
shouldBe("(stackLevel)", "(level - 1)");
