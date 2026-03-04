"""
 * FAAC Benchmark Suite - Shared Configuration
 * Copyright (C) 2026 Nils Schimmelmann
"""

SCENARIOS = {
    "voip": {
        "mode": "speech",
        "rate": 16000,
        "visqol_rate": 16000,
        "bitrate": 16,
        "thresh": 2.5},
    "vss": {
        "mode": "speech",
        "rate": 16000,
        "visqol_rate": 16000,
        "bitrate": 40,
        "thresh": 3.0},
    "music_low": {
        "mode": "audio",
        "rate": 48000,
        "visqol_rate": 48000,
        "bitrate": 64,
        "thresh": 3.5},
    "music_std": {
        "mode": "audio",
        "rate": 48000,
        "visqol_rate": 48000,
        "bitrate": 128,
        "thresh": 4.0},
    "music_high": {
        "mode": "audio",
        "rate": 48000,
        "visqol_rate": 48000,
        "bitrate": 256,
        "thresh": 4.3}}
