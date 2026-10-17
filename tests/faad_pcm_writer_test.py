#!/usr/bin/env python3
"""Provide an isolated path for sparse large-output PCM writer tests."""
import os
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="faad-large-output-") as directory:
    result = subprocess.run([sys.argv[1], os.path.join(directory, "output.pcm")])
    sys.exit(result.returncode)
