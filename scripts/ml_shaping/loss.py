#!/usr/bin/env python3
"""
FAAC ML Audio Shaping Multi-Objective Objective Function
Defines Pareto loss balancing BD-rate, stereo coherence, and transient pre-echo.
Guarantees hard barriers against degradation of transients and stereo imaging.
"""

import numpy as np


def compute_pareto_loss(
    bd_rate_delta,
    stereo_coherence_delta,
    nper_preecho_delta,
    lambda_stereo=2.5,
    lambda_transient=3.0,
    max_stereo_loss=0.005,
    max_transient_loss=0.10,
):
    """
    Computes Pareto multi-objective loss for audio shaping parameters.
    - bd_rate_delta: relative delta in BD-rate (negative is better).
    - stereo_coherence_delta: drop in inter-channel phase/energy coherence (positive is worse).
    - nper_preecho_delta: increase in normalized pre-echo error (positive is worse).
    """
    loss = bd_rate_delta

    # Barrier penalty if stereo coherence drops beyond allowed budget
    if stereo_coherence_delta > max_stereo_loss:
        loss += 50.0 * (stereo_coherence_delta - max_stereo_loss) ** 2
    else:
        loss += lambda_stereo * max(0.0, stereo_coherence_delta)

    # Hard barrier penalty if transient pre-echo increases
    if nper_preecho_delta > max_transient_loss:
        loss += 100.0 * (nper_preecho_delta - max_transient_loss) ** 2
    else:
        loss += lambda_transient * max(0.0, nper_preecho_delta)

    return loss
