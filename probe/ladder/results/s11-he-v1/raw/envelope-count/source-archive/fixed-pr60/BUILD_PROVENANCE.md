# Fixed four-envelope PR #60 build controls

These isolated builds apply only the five encoder-source changes used by live PR #60's fixed 1/2/4 envelope rule. No PR branch or original tracked checkout was changed.

- PR #60 commit: `6dfbfe3e2af279b03cd7658f03d4cf94be33dfa8`
- Upstream baseline: `99ce2b0980180194f9ccd1993a786208d277cf3e`
- Baseline build worktree: `/private/tmp/faac-he-v1-envcount-pr60-baseline`, HEAD `99ce2b0980180194f9ccd1993a786208d277cf3e`
- Retained static-noise/limiter stack worktree: `/private/tmp/faac-he-v1-envcount-pr60-stack`, HEAD `5d1bd74703657ae6e9acc5fae1df63ce07190071`
- Both builds used Meson/Ninja and completed successfully.
- The stack worktree preserves its existing limiter header gate (`numEnvFixFix > 1`); the fixed-tier extension therefore raises the header threshold through four envelopes at the high rung.
- Baseline PR60 frontend SHA-256: `c0dc1e7420c70657307af24d866786c3d15e37fee919834d43cd37157d7529b9`
- Baseline PR60 libfaac SHA-256: `65004149a009ca63d1a7c18697a11b6bc7182ef46e62518efbfaad722a0d9477`
- Stack PR60 frontend SHA-256: `7741458a69d12c7ceabd7e0b2b3e60f85f74620f29a28c7be49fb2f1c6b32d5d`
- Stack PR60 libfaac SHA-256: `ce27ce08242bf41a28ee7858037806538e20a0f3695a5c03c7330c55dd0361e3`

Patches are stored as `baseline.patch` and `stack.patch`. Production equivalence is complete: all 196 baseline streams match measured `tier_base` packet and strict FDK PCM hashes, and all 196 stack streams match `tier_stack`. Machine-readable evidence is in `.cache/he-v1-feature-study/envelope-count/production-equivalence/pr60_tier_base/equivalence-summary.json` and `.../pr60_tier_stack/equivalence-summary.json`.
