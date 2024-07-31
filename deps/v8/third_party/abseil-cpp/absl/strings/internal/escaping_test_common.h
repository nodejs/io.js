// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// This test contains common things needed by both escaping_test.cc and
// escaping_benchmark.cc.

#ifndef ABSL_STRINGS_INTERNAL_ESCAPING_TEST_COMMON_H_
#define ABSL_STRINGS_INTERNAL_ESCAPING_TEST_COMMON_H_

#include <array>
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace strings_internal {

struct base64_testcase {
  absl::string_view plaintext;
  absl::string_view cyphertext;
};

inline const std::array<base64_testcase, 5>& base64_strings() {
  static const std::array<base64_testcase, 5> testcase{{
      // Some google quotes
      // Cyphertext created with "uuencode (GNU sharutils) 4.6.3"
      // (Note that we're testing the websafe encoding, though, so if
      // you add messages, be sure to run "tr -- '+/' '-_'" on the output)
      { "I was always good at math and science, and I never realized "
        "that was unusual or somehow undesirable. So one of the things "
        "I care a lot about is helping to remove that stigma, "
        "to show girls that you can be feminine, you can like the things "
        "that girls like, but you can also be really good at technology. "
        "You can be really good at building things."
        " - Marissa Meyer, Newsweek, 2010-12-22" "\n",

        "SSB3YXMgYWx3YXlzIGdvb2QgYXQgbWF0aCBhbmQgc2NpZW5jZSwgYW5kIEkg"
        "bmV2ZXIgcmVhbGl6ZWQgdGhhdCB3YXMgdW51c3VhbCBvciBzb21laG93IHVu"
        "ZGVzaXJhYmxlLiBTbyBvbmUgb2YgdGhlIHRoaW5ncyBJIGNhcmUgYSBsb3Qg"
        "YWJvdXQgaXMgaGVscGluZyB0byByZW1vdmUgdGhhdCBzdGlnbWEsIHRvIHNo"
        "b3cgZ2lybHMgdGhhdCB5b3UgY2FuIGJlIGZlbWluaW5lLCB5b3UgY2FuIGxp"
        "a2UgdGhlIHRoaW5ncyB0aGF0IGdpcmxzIGxpa2UsIGJ1dCB5b3UgY2FuIGFs"
        "c28gYmUgcmVhbGx5IGdvb2QgYXQgdGVjaG5vbG9neS4gWW91IGNhbiBiZSBy"
        "ZWFsbHkgZ29vZCBhdCBidWlsZGluZyB0aGluZ3MuIC0gTWFyaXNzYSBNZXll"
        "ciwgTmV3c3dlZWssIDIwMTAtMTItMjIK" },

      { "Typical first year for a new cluster: "
        "~0.5 overheating "
        "~1 PDU failure "
        "~1 rack-move "
        "~1 network rewiring "
        "~20 rack failures "
        "~5 racks go wonky "
        "~8 network maintenances "
        "~12 router reloads "
        "~3 router failures "
        "~dozens of minor 30-second blips for dns "
        "~1000 individual machine failures "
        "~thousands of hard drive failures "
        "slow disks, bad memory, misconfigured machines, flaky machines, etc."
        " - Jeff Dean, The Joys of Real Hardware" "\n",

        "VHlwaWNhbCBmaXJzdCB5ZWFyIGZvciBhIG5ldyBjbHVzdGVyOiB-MC41IG92"
        "ZXJoZWF0aW5nIH4xIFBEVSBmYWlsdXJlIH4xIHJhY2stbW92ZSB-MSBuZXR3"
        "b3JrIHJld2lyaW5nIH4yMCByYWNrIGZhaWx1cmVzIH41IHJhY2tzIGdvIHdv"
        "bmt5IH44IG5ldHdvcmsgbWFpbnRlbmFuY2VzIH4xMiByb3V0ZXIgcmVsb2Fk"
        "cyB-MyByb3V0ZXIgZmFpbHVyZXMgfmRvemVucyBvZiBtaW5vciAzMC1zZWNv"
        "bmQgYmxpcHMgZm9yIGRucyB-MTAwMCBpbmRpdmlkdWFsIG1hY2hpbmUgZmFp"
        "bHVyZXMgfnRob3VzYW5kcyBvZiBoYXJkIGRyaXZlIGZhaWx1cmVzIHNsb3cg"
        "ZGlza3MsIGJhZCBtZW1vcnksIG1pc2NvbmZpZ3VyZWQgbWFjaGluZXMsIGZs"
        "YWt5IG1hY2hpbmVzLCBldGMuIC0gSmVmZiBEZWFuLCBUaGUgSm95cyBvZiBS"
        "ZWFsIEhhcmR3YXJlCg" },

      { "I'm the head of the webspam team at Google.  "
        "That means that if you type your name into Google and get porn back, "
        "it's my fault. Unless you're a porn star, in which case porn is a "
        "completely reasonable response."
        " - Matt Cutts, Google Plus" "\n",

        "SSdtIHRoZSBoZWFkIG9mIHRoZSB3ZWJzcGFtIHRlYW0gYXQgR29vZ2xlLiAg"
        "VGhhdCBtZWFucyB0aGF0IGlmIHlvdSB0eXBlIHlvdXIgbmFtZSBpbnRvIEdv"
        "b2dsZSBhbmQgZ2V0IHBvcm4gYmFjaywgaXQncyBteSBmYXVsdC4gVW5sZXNz"
        "IHlvdSdyZSBhIHBvcm4gc3RhciwgaW4gd2hpY2ggY2FzZSBwb3JuIGlzIGEg"
        "Y29tcGxldGVseSByZWFzb25hYmxlIHJlc3BvbnNlLiAtIE1hdHQgQ3V0dHMs"
        "IEdvb2dsZSBQbHVzCg" },

      { "It will still be a long time before machines approach human "
        "intelligence. "
        "But luckily, machines don't actually have to be intelligent; "
        "they just have to fake it. Access to a wealth of information, "
        "combined with a rudimentary decision-making capacity, "
        "can often be almost as useful. Of course, the results are better yet "
        "when coupled with intelligence. A reference librarian with access to "
        "a good search engine is a formidable tool."
        " - Craig Silverstein, Siemens Pictures of the Future, Spring 2004"
        "\n",

        "SXQgd2lsbCBzdGlsbCBiZSBhIGxvbmcgdGltZSBiZWZvcmUgbWFjaGluZXMg"
        "YXBwcm9hY2ggaHVtYW4gaW50ZWxsaWdlbmNlLiBCdXQgbHVja2lseSwgbWFj"
        "aGluZXMgZG9uJ3QgYWN0dWFsbHkgaGF2ZSB0byBiZSBpbnRlbGxpZ2VudDsg"
        "dGhleSBqdXN0IGhhdmUgdG8gZmFrZSBpdC4gQWNjZXNzIHRvIGEgd2VhbHRo"
        "IG9mIGluZm9ybWF0aW9uLCBjb21iaW5lZCB3aXRoIGEgcnVkaW1lbnRhcnkg"
        "ZGVjaXNpb24tbWFraW5nIGNhcGFjaXR5LCBjYW4gb2Z0ZW4gYmUgYWxtb3N0"
        "IGFzIHVzZWZ1bC4gT2YgY291cnNlLCB0aGUgcmVzdWx0cyBhcmUgYmV0dGVy"
        "IHlldCB3aGVuIGNvdXBsZWQgd2l0aCBpbnRlbGxpZ2VuY2UuIEEgcmVmZXJl"
        "bmNlIGxpYnJhcmlhbiB3aXRoIGFjY2VzcyB0byBhIGdvb2Qgc2VhcmNoIGVu"
        "Z2luZSBpcyBhIGZvcm1pZGFibGUgdG9vbC4gLSBDcmFpZyBTaWx2ZXJzdGVp"
        "biwgU2llbWVucyBQaWN0dXJlcyBvZiB0aGUgRnV0dXJlLCBTcHJpbmcgMjAw"
        "NAo" },

      // Degenerate edge case
      { "",
        "" },
  }};

  return testcase;
}

}  // namespace strings_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_ESCAPING_TEST_COMMON_H_
