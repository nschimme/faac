#!/usr/bin/env python3
"""
FAAC ML Audio Shaping Dataset Builder

Builds paired train/test datasets by associating per-frame audio shaping telemetry
logs (attack ratio, SFM, energy, M/S correlation, rate budget) with objective
benchmark metrics from faac-benchmark (MOS, BD-rate, stereo image delta, and transient metrics).
"""

import argparse
import csv
import glob
import json
import os
import sys

def parse_telemetry_csv(csv_path):
    """Parses an encoder telemetry CSV file and returns per-frame feature dicts."""
    records = []
    with open(csv_path, 'r', newline='', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                record = {
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
                }
                records.append(record)
            except (ValueError, KeyError):
                continue
    return records

def aggregate_clip_features(telemetry_records):
    """Aggregates per-frame telemetry into clip-level summary statistics."""
    if not telemetry_records:
        return {}

    n = len(telemetry_records)
    attacks = [r['attack_ratio'] for r in telemetry_records]
    sfms = [r['sfm'] for r in telemetry_records]
    energies = [r['total_energy'] for r in telemetry_records]
    tns_active = [r['tns_enabled'] for r in telemetry_records]

    return {
        'frame_count': n,
        'mean_attack': sum(attacks) / n,
        'max_attack': max(attacks) if attacks else 0.0,
        'mean_sfm': sum(sfms) / n,
        'mean_energy': sum(energies) / n,
        'tns_ratio': sum(tns_active) / n,
        'sample_rate': telemetry_records[0]['sample_rate'],
        'num_channels': telemetry_records[0]['num_channels'],
        'bitrate_per_ch': telemetry_records[0]['bitrate_per_ch']
    }

def join_telemetry_with_benchmark(telemetry_dir, benchmark_json_path, output_dataset_path, train_ratio=0.8):
    """
    Pairs encoder telemetry logs with benchmark JSON results (MOS, BD-rate, stereo diff, transients)
    and splits into train and test sets.
    """
    benchmark_data = {}
    if os.path.exists(benchmark_json_path):
        with open(benchmark_json_path, 'r', encoding='utf-8') as f:
            try:
                raw_bench = json.load(f)
                if 'matrix' in raw_bench:
                    for scenario, clips in raw_bench['matrix'].items():
                        for clip_name, metrics in clips.items():
                            key = f"{scenario}_{clip_name}"
                            benchmark_data[key] = metrics
            except json.JSONDecodeError:
                print(f"Warning: could not parse benchmark JSON at {benchmark_json_path}")

    csv_files = sorted(glob.glob(os.path.join(telemetry_dir, "*.csv")))
    dataset_rows = []

    for path in csv_files:
        base_name = os.path.splitext(os.path.basename(path))[0]
        records = parse_telemetry_csv(path)
        if not records:
            continue

        clip_features = aggregate_clip_features(records)
        target_metrics = benchmark_data.get(base_name, {})

        row = {
            'clip_id': base_name,
            **clip_features,
            'mos': target_metrics.get('mos', 0.0),
            'bd_rate': target_metrics.get('bd_rate', 0.0),
            'stereo_delta': target_metrics.get('stereo_delta', 0.0),
            'pre_echo_db': target_metrics.get('pre_echo_db', 0.0),
            'attack_centroid_ms': target_metrics.get('attack_centroid_ms', 0.0),
        }
        dataset_rows.append(row)

    split_idx = int(len(dataset_rows) * train_ratio)
    train_set = dataset_rows[:split_idx]
    test_set = dataset_rows[split_idx:]

    output_data = {
        'total_clips': len(dataset_rows),
        'train_count': len(train_set),
        'test_count': len(test_set),
        'train': train_set,
        'test': test_set
    }

    os.makedirs(os.path.dirname(os.path.abspath(output_dataset_path)), exist_ok=True)
    with open(output_dataset_path, 'w', encoding='utf-8') as f:
        json.dump(output_data, f, indent=2)

    print(f"Dataset successfully built with {len(dataset_rows)} samples: {output_dataset_path}")
    print(f"  Train: {len(train_set)}, Test: {len(test_set)}")
    return output_data

def main():
    parser = argparse.ArgumentParser(description="FAAC ML Audio Shaping Dataset Builder")
    parser.add_argument("--telemetry-dir", required=True, help="Directory containing shaping CSV logs")
    parser.add_argument("--benchmark-json", default="", help="Path to faac-benchmark results JSON")
    parser.add_argument("--output", default="data/ml_shaping_dataset.json", help="Output dataset JSON path")
    parser.add_argument("--train-ratio", type=float, default=0.8, help="Train split ratio (default: 0.8)")
    args = parser.parse_args()

    join_telemetry_with_benchmark(args.telemetry_dir, args.benchmark_json, args.output, args.train_ratio)

if __name__ == "__main__":
    main()
