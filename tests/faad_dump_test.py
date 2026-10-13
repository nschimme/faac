#!/usr/bin/env python3
"""Provide isolated dump paths for the sequential, in-process lifecycle test."""
import os
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory() as directory:
    subprocess.run([sys.argv[1], os.path.join(directory, "first.dump"),
                    os.path.join(directory, "second.dump")], check=True)
