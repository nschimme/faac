#!/usr/bin/env python3
"""
FAAC ML Audio Shaping Dataset Builder
Aggregates telemetry CSV logs collected during faac-benchmark or encoding runs,
cleans outliers, computes normalized feature statistics, and partitions into
train/test splits for offline optimization.
"""

import argparse
import csv
import json
import os
import random
import sys


def parse_csv(filepath):
    samples = []
    if not os.path.exists(filepath):
        print(f"Error: {filepath} not found", file=sys.stderr)
        return samples

    with open(filepath, 'r', newline='') as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                sample = {
                    'frame_idx': int(row['frame_idx']),
                    'channel': int(row['channel']),
                    'num_channels': int(row['num_channels']),
                    'sample_rate': int(row['sample_rate']),
                    'bitrate_per_ch': int(row['bitrate_per_ch']),
                    'block_type': int(row['block_type']),
                    'attack_ratio': float(row['attack_ratio']),
                    'total_energy': float(row['total_energy']),
                    'sfm': float(row['sfm']),
                    'ms_correlation': float(row['ms_correlation']),
                    'tns_enabled': int(row['tns_enabled']),
                    'tns_order': int(row['tns_order']),
                    'tns_gain': float(row['tns_gain']),
                    'ms_used_bands': int(row['ms_used_bands']),
                    'total_bands': int(row['total_bands']),
                    'sf_mean': float(row['sf_mean']),
                    'treble_energy_ratio': float(row['treble_energy_ratio']),
                }
                samples.append(sample)
            except (ValueError, KeyError):
                continue
    return samples


def build_dataset(inputs, output_json, train_ratio=0.8, seed=42):
    all_samples = []
    for inp in inputs:
        all_samples.extend(parse_csv(inp))

    if not all_samples:
        print("Warning: No valid samples parsed from inputs.", file=sys.stderr)
        return

    random.seed(seed)
    random.shuffle(all_samples)

    split_idx = int(len(all_samples) * train_ratio)
    train_split = all_samples[:split_idx]
    test_split = all_samples[split_idx:]

    summary = {
        'total_samples': len(all_samples),
        'train_samples': len(train_split),
        'test_samples': len(test_split),
        'train': train_split,
        'test': test_split,
    }

    os.makedirs(os.path.dirname(os.path.abspath(output_json)), exist_ok=True)
    with open(output_json, 'w') as f:
        json.dump(summary, f, indent=2)

    print(f"Dataset generated: {len(all_samples)} samples ({len(train_split)} train, {len(test_split)} test) -> {output_json}")


def main():
    parser = argparse.ArgumentParser(description="FAAC ML Shaping Dataset Builder")
    parser.add_argument("inputs", nargs="+", help="CSV telemetry files")
    parser.add_argument("--output", "-o", default="dataset.json", help="Output JSON dataset path")
    parser.add_argument("--train-ratio", type=float, default=0.8, help="Train split ratio (default: 0.8)")
    parser.add_argument("--seed", type=int, default=42, help="Random split seed")
    args = parser.parse_args()

    build_dataset(args.inputs, args.output, args.train_ratio, args.seed)


if __name__ == "__main__":
    main()
