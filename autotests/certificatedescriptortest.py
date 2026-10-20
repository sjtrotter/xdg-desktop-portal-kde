#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest


class CertificateDescriptorTest(unittest.TestCase):
    def check_generator(self, build):
        cache = (build / "CMakeCache.txt").read_text()
        self.assertIn(f"CMAKE_GENERATOR:INTERNAL={sys.argv[3]}\n", cache)
        self.assertIn(f"CMAKE_MAKE_PROGRAM:FILEPATH={sys.argv[4]}\n", cache)

    def test_descriptor_regenerated(self):
        with tempfile.TemporaryDirectory(prefix="certificate-descriptor-") as directory:
            source = Path(directory) / "source"
            source.mkdir()
            shutil.copytree(Path(sys.argv[1]) / "data", source / "data")
            (source / "CMakeLists.txt").write_text('''
cmake_minimum_required(VERSION 3.16)
project(certificate_descriptor NONE)
set(KDE_INSTALL_DATADIR share)
set(KDE_INSTALL_DBUSSERVICEDIR share/dbus-1/services)
set(KDE_INSTALL_APPDIR share/applications)
function(ecm_install_configured_files)
endfunction()
add_subdirectory(data)
''')
            build = Path(directory) / "build"
            configure = [sys.argv[2], "-S", str(source), "-G", sys.argv[3],
                         f"-DCMAKE_MAKE_PROGRAM:FILEPATH={sys.argv[4]}"]
            subprocess.run([*configure, "-B", str(build),
                            "-DBUILD_CERTIFICATE_PORTAL=ON"], check=True)
            self.check_generator(build)
            descriptor = source / "data/kde.portal"
            generated = build / "data/kde.portal"
            original = descriptor.read_text()
            certificate = "org.freedesktop.impl.portal.experimental.Certificate1"
            self.assertEqual(generated.read_text(), original.replace(
                "org.freedesktop.impl.portal.Usb", "org.freedesktop.impl.portal.Usb;" + certificate))
            # Cross filesystem timestamp granularity before changing the input.
            time.sleep(1.1)
            descriptor.write_text(original + "\n# Regeneration test\n")
            os.utime(descriptor, None)
            subprocess.run([sys.argv[2], "--build", str(build), "--parallel", "1"], check=True)
            self.assertIn("# Regeneration test", generated.read_text())
            self.assertEqual(generated.read_text().count(certificate), 1)
            disabled = Path(directory) / "disabled"
            subprocess.run([*configure, "-B", str(disabled),
                            "-DBUILD_CERTIFICATE_PORTAL=OFF"], check=True)
            self.check_generator(disabled)
            self.assertFalse((disabled / "data/kde.portal").exists())
            self.assertNotIn(certificate, descriptor.read_text())


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
