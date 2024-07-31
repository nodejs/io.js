#!/usr/bin/env python3

"""Unit tests for the xcode_emulation.py file."""

from gyp.xcode_emulation import XcodeSettings
import sys
import unittest


class TestXcodeSettings(unittest.TestCase):
    def setUp(self):
        if sys.platform != "darwin":
            self.skipTest("This test only runs on macOS")

    def test_GetCflags(self):
        target = {
            "type": "static_library",
            "configurations": {
                "Release": {},
            },
        }
        configuration_name = "Release"
        xcode_settings = XcodeSettings(target)
        cflags = xcode_settings.GetCflags(configuration_name, "arm64")

        # Do not quote `-arch arm64` with spaces in one string.
        self.assertEqual(
            cflags,
            ["-fasm-blocks", "-mpascal-strings", "-Os", "-gdwarf-2", "-arch", "arm64"],
        )

    def GypToBuildPath(self, path):
        return path

    def test_GetLdflags(self):
        target = {
            "type": "static_library",
            "configurations": {
                "Release": {},
            },
        }
        configuration_name = "Release"
        xcode_settings = XcodeSettings(target)
        ldflags = xcode_settings.GetLdflags(
            configuration_name, "PRODUCT_DIR", self.GypToBuildPath, "arm64"
        )

        # Do not quote `-arch arm64` with spaces in one string.
        self.assertEqual(ldflags, ["-arch", "arm64", "-LPRODUCT_DIR"])


if __name__ == "__main__":
    unittest.main()
