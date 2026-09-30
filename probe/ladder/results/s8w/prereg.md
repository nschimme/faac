# Stage S8-W Pre-registration

Written on 2026-10-01 prior to running Stage S8-W research arms.

## Context & Gap
Stage S6-X established that the core carries the HE 48k quality gap against fdk-aac (+0.068..+0.099) and Apple (+0.056..+0.075). Stage S7-CORE attempted reference window injection ($W_{\text{fdk}}$, $W_{\text{apple}}$), but its alignment control matched only 82.6% (fdk) / 80.6% (Apple) of windows due to a frame misalignment, rendering its verdict void. Stage S8-W re-evaluates window injection at verified alignment ($\ge 95\%$ window match).

## Base Encoder
- FAAC STATIC build from probe branch
- Base environment / knobs: `FAAC_SF_SMOOTH=0.6 FAAC_BS_DROPRATIO=12 FAAC_SBR_FREQ_SCALE=3`
- Rung: HE 48k ABR (`-b 48`), 48 kHz stereo, 49 clips
- Bits-adjusted per clip with FAAC's own HE 40k/56k slope pair:
  $$\text{adj MOS} = \text{MOS}_X - \text{MOS}_F - \text{slope} \times \log_2(\text{bytes}_X / \text{bytes}_F)$$

## Controls (Step A) - Mandatory Gate
1. **A0 (Self-Injection Control):**
   - Re-inject FAAC's own full HE 48k dump (all fields: `win,cls,sf,ms,tns`) via `FAAC_CORE_INJECT`, `FAAC_CORE_INJECT_LOOSE_SFB=1`, `OFFSET=1` $\rightarrow$ decoded PCM 100% identical to F on 49/49 clips.
   - Re-inject FAAC's own win-only dump (`FAAC_CORE_INJECT_FIELDS="win"`) $\rightarrow$ decoded PCM 100% identical to F on 49/49 clips.
2. **A1 (Alignment Control):**
   - Sweep input sample pad around known lags (at least pad - 2048, pad, pad + 2048, plus S7-CORE values) and `FAAC_CORE_INJECT_OFFSET` 0..3 for window-only injection (`FAAC_CORE_INJECT_FIELDS="win"`).
   - Require $\ge 95\%$ of windows matched across the 49-clip corpus for a reference to proceed to Step B.
   - If no combination reaches 95%, STOP, report the sweep table, and do NOT run Step B.

## Window Arms Rule (Step B)
For reference $R \in \{\text{fdk}, \text{apple}\}$, let $W_R$ be FAAC re-encoding with rate loop ON, FAAC re-deciding all other parameters, injecting windows only (`win`) at the verified A1 alignment.

$$\text{Recovery}_R = \frac{\text{adj Mean}(W_R)}{\text{CoreGap}_R}$$

where $\text{CoreGap}_{\text{fdk}} \in [+0.068, +0.099]$ and $\text{CoreGap}_{\text{apple}} \in [+0.056, +0.075]$.

Windows "carry" the core gap if $\text{Recovery}_R \ge 50\%$ AND $W_R > L$ (wins > losses across 49 clips).

## Contingency Decision Tree
1. **If windows carry the gap for either reference:**
   - Characterise the frames where FAAC goes short and the reference long using `FAAC_BS_DUMP` detector inputs.
   - Sweep one existing `FAAC_BS_*` block-switch probe knob at HE only, bracketed ($\ge 3$ values + neutral), at HE 48k and HE 32k.
2. **If windows do NOT carry the gap for either reference:**
   - Run A1-verified injection for M/S (`FAAC_CORE_INJECT_FIELDS="ms"`), one field at a time.
   - Run A1-verified injection for TNS (`FAAC_CORE_INJECT_FIELDS="tns"`), one field at a time.
   - Apply the same 50% recovery & $W > L$ rule to determine if M/S or TNS carries the core gap.
