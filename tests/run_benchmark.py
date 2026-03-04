"""
 * FAAC Benchmark Suite - User-Friendly Entrypoint
 * Copyright (C) 2026 Nils Schimmelmann
"""

import os
import sys
import subprocess
import argparse

def main():
    parser = argparse.ArgumentParser(description="FAAC Benchmark Suite")
    parser.add_argument("faac_bin", help="Path to faac binary")
    parser.add_argument("lib_path", help="Path to libfaac.so")
    parser.add_argument("name", help="Name for this run")
    parser.add_argument("output", help="Output JSON path")
    parser.add_argument("--coverage", type=int, default=100, help="Coverage percentage (1-100)")
    parser.add_argument("--skip-mos", action="store_true", help="Skip perceptual quality (MOS) computation")

    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    phase1_script = os.path.join(script_dir, "phase1_encode.py")
    phase2_script = os.path.join(script_dir, "phase2_mos.py")

    # Phase 1: Encoding
    print(">>> Phase 1: Encoding and Basic Metrics")
    cmd_phase1 = [
        sys.executable, phase1_script,
        args.faac_bin, args.lib_path, args.name, args.output,
        "--coverage", str(args.coverage)
    ]
    subprocess.run(cmd_phase1, check=True)

    if args.skip_mos:
        print(">>> Skipping Phase 2 as requested.")
        return

    # Phase 2: MOS
    print(">>> Phase 2: Perceptual Quality (MOS)")

    # Strategy 1: Local Python (check if requirements_visqol are met)
    try:
        import visqol_py
        print("Using local ViSQOL installation...")
        cmd_phase2 = [
            sys.executable, phase2_script,
            args.output,
            os.path.join(script_dir, "output"),
            os.path.join(script_dir, "data", "external")
        ]
        subprocess.run(cmd_phase2, check=True)
    except ImportError:
        # Strategy 2: Docker
        print("Local ViSQOL not found. Attempting Docker strategy...")
        try:
            subprocess.run(["docker", "--version"], check=True, capture_output=True)

            # Build if needed
            print("Building faac-visqol Docker image...")
            subprocess.run([
                "docker", "build", "-t", "faac-visqol", "-f",
                os.path.join(script_dir, "Dockerfile.visqol"), script_dir
            ], check=True)

            # Run
            print("Running MOS computation in Docker...")
            # We need absolute paths for volume mounting
            abs_output = os.path.abspath(args.output)
            abs_results_dir = os.path.dirname(abs_output)
            results_file = os.path.basename(abs_output)
            abs_output_dir = os.path.abspath(os.path.join(script_dir, "output"))
            abs_data_dir = os.path.abspath(os.path.join(script_dir, "data", "external"))

            cmd_docker = [
                "docker", "run", "--rm",
                "-v", f"{abs_results_dir}:/results",
                "-v", f"{abs_output_dir}:/output",
                "-v", f"{abs_data_dir}:/data",
                "faac-visqol", f"/results/{results_file}", "/output", "/data"
            ]
            subprocess.run(cmd_docker, check=True)

        except (subprocess.CalledProcessError, FileNotFoundError):
            print(">>> ERROR: Perceptual quality computation failed.")
            print("Please either:")
            print("  1. Install ViSQOL dependencies: pip install -r tests/requirements_visqol.txt")
            print("  2. Install Docker and ensure the daemon is running.")
            print("  3. Run with --skip-mos if you only need encoding metrics.")
            sys.exit(1)

    print(">>> Benchmark complete.")

if __name__ == "__main__":
    main()
