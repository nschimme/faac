# Pre-registered Rules for Stage S7-CORE (2026-09-30)

## Base Configuration (F)
- Probe libfaac from this branch, static build.
- Environment: `FAAC_SF_SMOOTH=0.6`, `FAAC_BS_DROPRATIO=12`, `FAAC_SBR_FREQ_SCALE=3` (S6 "#601 base").
- Rung: HE 48k ABR (`faac -b 48`), 48 kHz stereo, 49 clips.
- Bits-adjustment: per-clip HE slope pair using FAAC's 40/56k HE encodes.

## Step A — Controls (must pass before reading arms)
- **A0**: Injection of FAAC's OWN HE 48k dump (`FAAC_CORE_INJECT` C records with `g=` tokens via `h_conv.py`, `FAAC_CORE_INJECT_LOOSE_SFB=1`) = F. Decoded PCM must be 100% identical on 49/49 clips.
- **A1**: Alignment verification. fdk raw lag: pad FAAC input 2015 samples, same frame (`FAAC_CORE_INJECT_OFFSET=1`). Apple raw lag: pad FAAC input 96 samples, Apple frame n+1 = FAAC frame n (`FAAC_CORE_INJECT_OFFSET=2`). Derived offsets verified by window sequence match against references across all frames (report matched/total, explain misses).

## Step B — Window Arms (W_fdk, W_apple)
- Inject reference window sequence into FAAC's HE core rate loop (`FAAC_CORE_INJECT_FIELDS=win`).
- **Carry Threshold**: Windows "carry" the core gap if `W_ref >= 50%` of that reference's core gap (against fdk's +0.068..+0.099 and Apple's +0.056..+0.075) with `W > L`.
- If windows carry the gap: characterize short/long frame triggers via `FAAC_BS_DUMP` and sweep block-switch probe knobs (`FAAC_BS_*`) at HE 48k and HE 32k.
- If windows do not carry the gap (< 50%): proceed to Step C.

## Step C — PNS at HE Threshold Sweep
- Locate PNS decision logic for HE long blocks in `libfaac/quantize.c`.
- Add probe env knob (unset = production, 49/49 PCM-identical control).
- Sweep bracketed (at least 3 values + neutral) at HE 48k, then check HE 32k.

## Encoder-Knob Rule
- Mean bits-adjusted MOS delta vs F >= +0.005.
- `W > L` (wins outnumber losses).
- No individual clip delta < -0.05.
- Realized byte count within +-12.5% vs F.
- Chosen value is at the bracket centre (both adjacent neighbours worse).
- HE 32k performance is not worse than F.
- Passing knob must be re-measured on plain master before opening a PR on `nschimme/faac`.
