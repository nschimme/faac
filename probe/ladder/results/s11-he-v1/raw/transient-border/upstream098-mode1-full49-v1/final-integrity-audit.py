#!/usr/bin/env python3
"""Read-only integrity check for frozen HEv1 full49 quality matrices; no encode/scoring."""
import hashlib, json, math, subprocess
from pathlib import Path

ROOT = Path('/Users/nschimme/gitprojects/faac')
CACHE = ROOT / '.cache/he-v1-feature-study/transient-border'
A_DIR = CACHE / 'upstream098-mode1-full49-v1'
F_DIR = CACHE / 'upstream098-fixed8-gate-v2/full49'
O_DIR = CACHE / 'upstream-fixed8-validation'
BUILD_MANIFEST = Path('/private/tmp/faac-upstream098-build-manifest.json')
ADAPTIVE_EQ = Path('/private/tmp/faac-upstream098-fdk-ab/adaptive32/equivalence.json')
CONTROL_EQ = CACHE / 'upstream098-fixed8-gate-v2/validation-build/full392-normalized-equivalence.json'

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def load(path):
    return json.loads(Path(path).read_text())
def git(*args, cwd=ROOT):
    return subprocess.run(['git', *args], cwd=cwd, capture_output=True, text=True, check=True).stdout.strip()

A = load(A_DIR / 'rows.json')['rows']
F = load(F_DIR / 'rows.json')['rows']
O = load(O_DIR / 'validation.json')['rows']
AP = load(A_DIR / 'provenance.json')
FP = load(F_DIR / 'provenance.json')
M = load(BUILD_MANIFEST)
E = load(ADAPTIVE_EQ)
CE = load(CONTROL_EQ)
strict = load(A_DIR / 'strict-interop.json')

clips = set(AP['source_anchor_hashes'])
rates = [32, 48, 64, 96]
expected = {f'{clip}|{rate}' for clip in clips for rate in rates}
base = {k: v for k, v in F.items() if k.startswith('upstream_baseline|')}
fixed = {k: v for k, v in F.items() if k.startswith('upstream_fixed8|')}
base_suffix = {k.split('|', 1)[1] for k in base}
fixed_suffix = {k.split('|', 1)[1] for k in fixed}

checks = {}
checks['source_commit_is_098'] = M['upstream_commit'] == '098e8900289b87d1592a859351ca0bd420397d4b'
checks['captured_fixed8_commit_parent_is_098'] = git('show', '-s', '--format=%P', 'HEAD', cwd='/private/tmp/faac-upstream098-fixed8') == M['upstream_commit']
checks['captured_fixed8_checkout_clean'] = git('status', '--porcelain=v1', cwd='/private/tmp/faac-upstream098-fixed8') == ''
checks['captured_fixed8_binary_matches_manifest'] = sha(M['variants']['upstream_fixed8']['binary']) == M['variants']['upstream_fixed8']['binary_sha256']
checks['captured_baseline_binary_matches_manifest'] = sha(M['variants']['upstream_baseline']['binary']) == M['variants']['upstream_baseline']['binary_sha256']
checks['fixed8_quality_manifest_pins_same_build_manifest'] = sha(BUILD_MANIFEST) == FP['source']['manifest_sha256']
checks['fixed8_manifest_and_quality_source_commit_match'] = FP['source']['source_commit'] == M['upstream_commit']
checks['source_patch_effect_matches_frozen_archive'] = subprocess.run(['git', '-C', '/private/tmp/faac-upstream098-fixed8', 'apply', '--reverse', '--check', str(O_DIR / 'base_fixed8.patch')], capture_output=True).returncode == 0
checks['full_controls_are_49x4_each'] = len(base) == len(fixed) == 196 and base_suffix == fixed_suffix == expected
checks['adaptive_matrix_is_49x4x2'] = len(A) == 392 and len(clips) == 49
checks['adaptive_keys_complete_per_lambda'] = all({f'{lam}|{c}|{r}' for c in clips for r in rates} <= set(A) for lam in ('0.001', '0.003'))
checks['adaptive_profile_finite_mos_positive_payload'] = all(v.get('profile') == 'HE-AAC' and isinstance(v.get('mos'), (int, float)) and math.isfinite(v['mos']) and v.get('actual_payload_kbps', 0) > 0 for v in A.values())
checks['adaptive_source_anchor_hashes_match_rows'] = all(v['input_sha256'] == AP['source_anchor_hashes'][Path(v['input_path']).name]['input_sha256'] for v in A.values())
checks['adaptive_original_source_files_unchanged'] = all(sha(a['input_path']) == a['input_sha256'] for a in AP['source_anchor_hashes'].values())
checks['adaptive_and_control_scorer_helpers_match'] = AP['benchmark_helpers'] == FP['benchmark']['helper_files']
checks['adaptive_rows_reference_frozen_fresh_controls'] = AP['full_reference_rows_sha256'] == sha(F_DIR / 'rows.json')
checks['adaptive_strict392_matches_raw_rows'] = strict['source_rows_sha256'] == sha(A_DIR / 'rows.json') and strict['summary'] == {'errors': 0, 'faad_pass': 392, 'ffmpeg_pass': 392, 'rows': 392}
checks['archived_combo16_baseline_refs_exact_packet_pcm'] = True
checks['archived_combo16_fixed8_candidates_exact_packet_pcm'] = True
old_ref_count = old_candidate_count = 0
old_ref_mismatches, old_candidate_mismatches = [], []
for key, old in O.items():
    if not key.startswith('combo_fixed8|'):
        continue
    _, clip, rate = key.split('|')
    ref = old['reference']
    current_base = F[f'upstream_baseline|{clip}|{rate}']
    current_fixed = F[f'upstream_fixed8|{clip}|{rate}']
    old_ref_count += 1
    if (ref['payload_sha256'], ref['decoded_sha256']) != (current_base['packet_payload_sha256'], current_base['decoded_file_sha256']):
        old_ref_mismatches.append(key)
    if (old['payload_sha256'], old['decoded_sha256']) != (current_fixed['packet_payload_sha256'], current_fixed['decoded_file_sha256']):
        old_candidate_mismatches.append(key)
checks['archived_combo16_baseline_refs_exact_packet_pcm'] = old_ref_count == 16 and not old_ref_mismatches
checks['archived_combo16_fixed8_candidates_exact_packet_pcm'] = old_ref_count == 16 and not old_candidate_mismatches

# Compare patch content semantically while retaining all artifacts and hashes.
commit_patch = subprocess.run(['git', '-C', '/private/tmp/faac-upstream098-fixed8', 'show', '--format=', '--binary', 'HEAD'], capture_output=True, check=True).stdout
archived_patch = (O_DIR / 'base_fixed8.patch').read_bytes()
def changed_lines(patch):
    return [line for line in patch.decode('utf-8').splitlines() if (line.startswith('+') and not line.startswith('+++')) or (line.startswith('-') and not line.startswith('---'))]
source_diff_path = Path(M['variants']['upstream_fixed8']['source_diff_path'])
source_diff_hash = sha(source_diff_path)
patch_metadata = {
    'commit': git('rev-parse', 'HEAD', cwd='/private/tmp/faac-upstream098-fixed8'),
    'parent': git('show', '-s', '--format=%P', 'HEAD', cwd='/private/tmp/faac-upstream098-fixed8'),
    'commit_patch_sha256': hashlib.sha256(commit_patch).hexdigest(),
    'archived_patch_sha256': sha(O_DIR / 'base_fixed8.patch'),
    'manifest_patch_path': str(source_diff_path),
    'manifest_patch_actual_sha256': source_diff_hash,
    'manifest_patch_recorded_sha256': M['variants']['upstream_fixed8']['source_diff_sha256'],
    'manifest_patch_hash_matches': source_diff_hash == M['variants']['upstream_fixed8']['source_diff_sha256'],
    'commit_and_archive_changed_lines_match': changed_lines(commit_patch) == changed_lines(archived_patch),
    'manifest_patch_is_parseable': subprocess.run(['git', '-C', '/private/tmp/faac-upstream098-fixed8', 'apply', '--check', str(source_diff_path)], capture_output=True).returncode == 0,
    'note': 'The frozen build manifest hash equals the recorded /private/tmp patch hash, but that patch artifact lacks its final newline and is not parseable. The committed clean fixed8 checkout has parent 098; its change lines match the archived base_fixed8.patch. Both captured binary hashes match the build manifest.'
}

# Decoder-equivalence records are deliberately scoped: adaptive candidate old/new equivalence is the 32-row gate only; full392 applies to fresh baseline/fixed8 controls.
E_summary = E.get('summary', {})
CE_summary = CE.get('summary', {})
adaptive_eq_rows = E.get('rows', [])
adaptive_eq_summary = {
    'rows_checked': len(adaptive_eq_rows),
    'normalized_pcm_equal': sum(bool(row.get('normalized_pcm_equal')) for row in adaptive_eq_rows),
    'errors': len(E_summary.get('errors', [])),
    'old_decoder_sha256': E_summary.get('old_decoder_sha256'),
    'new_decoder_sha256': E_summary.get('new_decoder_sha256'),
}
control_eq_summary = {k: CE_summary.get(k) for k in ('rows_input', 'rows_checked', 'normalized_pcm_equal', 'errors', 'old_decoder_sha256', 'new_decoder_sha256')}

status = git('status', '--short', '--untracked-files=all')
checks['original_checkout_only_AGENTS_untracked'] = status == '?? AGENTS.md'
report = {
    'schema': 1,
    'audit_kind': 'read-only final matrix/provenance integrity; no encoding, MOS, or decoder tests rerun',
    'all_checks_pass': all(checks.values()),
    'checks': checks,
    'matrices': {
        'adaptive': {'rows': len(A), 'lambdas': ['0.001', '0.003'], 'clips': len(clips), 'rates_kbps': rates, 'rows_sha256': sha(A_DIR / 'rows.json'), 'summary_sha256': sha(A_DIR / 'summary.json')},
        'fresh_baseline': {'rows': len(base), 'rows_sha256': sha(F_DIR / 'rows.json')},
        'fresh_fixed8': {'rows': len(fixed)},
        'archived_combo_reference_rows': old_ref_count,
        'archived_combo_reference_baseline_exact_packet_pcm': old_ref_count - len(old_ref_mismatches),
        'archived_combo_fixed8_candidate_exact_packet_pcm': old_ref_count - len(old_candidate_mismatches),
        'archived_combo_mismatches': {'baseline_refs': old_ref_mismatches, 'fixed8_candidates': old_candidate_mismatches}
    },
    'source_and_scorer': {
        'upstream_commit': M['upstream_commit'],
        'build_manifest_sha256': sha(BUILD_MANIFEST),
        'adaptive_manifest_sha256': AP['probe_manifest_sha256'],
        'adaptive_run_config_sha256': AP['config_sha256'],
        'adaptive_runner_sha256': AP['runner_sha256'],
        'adaptive_source_anchor_count': len(AP['source_anchor_hashes']),
        'adaptive_fdk_path_sha256': [AP['fdk_path'], AP['fdk_sha256']],
        'fixed8_source_patch': patch_metadata,
        'original_checkout_status': status
    },
    'decoder_equivalence_scope': {
        'adaptive_candidate_gate_32_rows_only': {'path': str(ADAPTIVE_EQ), 'sha256': sha(ADAPTIVE_EQ), 'summary': adaptive_eq_summary},
        'fresh_baseline_and_fixed8_controls_full392': {'path': str(CONTROL_EQ), 'sha256': sha(CONTROL_EQ), 'summary': control_eq_summary},
        'adaptive_full392_old_new_fdk_equivalence_claimed': False
    },
    'audit_script_sha256': sha(A_DIR / 'final-integrity-audit.py')
}
out = A_DIR / 'final-integrity-audit.json'
out.write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
print(json.dumps({'all_checks_pass': report['all_checks_pass'], 'failed': [k for k,v in checks.items() if not v], 'old_combo_reference_matches': old_ref_count-len(old_ref_mismatches), 'old_combo_candidate_matches': old_ref_count-len(old_candidate_mismatches), 'source_patch_metadata': patch_metadata, 'adaptive_gate_equivalence': adaptive_eq_summary, 'control_full392_equivalence': control_eq_summary, 'audit_sha256': sha(out)}, indent=2))
