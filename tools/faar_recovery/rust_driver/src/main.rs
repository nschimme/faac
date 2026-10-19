// Rust reference driver: same argv and JSON contract as ../driver.cpp.
// driver <tuneout|ardftsrc-rs> <variant> input.f32 output.f32 in_rate out_rate channels block_frames output_capacity
//   tuneout variants: standard | long | extralong (Generic policy, f32 I/O, f64 accumulation)
//   ardftsrc-rs variants: matched (quality 16384, bandwidth 0.99) | default (crate default config)
// Raw files are native little-endian float32 interleaved PCM.
use std::alloc::{GlobalAlloc, Layout, System};
use std::io::Write;
use std::sync::atomic::{AtomicUsize, Ordering::Relaxed};

struct Counting;
static LIVE: AtomicUsize = AtomicUsize::new(0);
static PEAK: AtomicUsize = AtomicUsize::new(0);
unsafe impl GlobalAlloc for Counting {
    unsafe fn alloc(&self, l: Layout) -> *mut u8 {
        let p = System.alloc(l);
        if !p.is_null() { let n = LIVE.fetch_add(l.size(), Relaxed) + l.size(); PEAK.fetch_max(n, Relaxed); }
        p
    }
    unsafe fn dealloc(&self, p: *mut u8, l: Layout) { System.dealloc(p, l); LIVE.fetch_sub(l.size(), Relaxed); }
    unsafe fn realloc(&self, p: *mut u8, l: Layout, n: usize) -> *mut u8 {
        let q = System.realloc(p, l, n);
        if !q.is_null() {
            if n >= l.size() { let v = LIVE.fetch_add(n - l.size(), Relaxed) + n - l.size(); PEAK.fetch_max(v, Relaxed); }
            else { LIVE.fetch_sub(l.size() - n, Relaxed); }
        }
        q
    }
}
#[global_allocator]
static A: Counting = Counting;

fn cpu() -> f64 {
    let mut t = libc::timespec { tv_sec: 0, tv_nsec: 0 };
    unsafe { libc::clock_gettime(libc::CLOCK_PROCESS_CPUTIME_ID, &mut t) };
    t.tv_sec as f64 + t.tv_nsec as f64 * 1e-9
}

type Stream = (f64, f64, usize, usize, usize);
fn ardftsrc_stream<T: ardftsrc::Sample>(cfg: ardftsrc::Config, input: &[f32], block: usize, ch: usize, result: &mut Vec<f32>,
                                         to: fn(f32) -> T, from: fn(T) -> f32) -> Result<Stream, String> {
    use ardftsrc::InterleavedResampler;
    let before = LIVE.load(Relaxed);
    let t = cpu();
    let mut r = InterleavedResampler::<T>::new(cfg).map_err(|e| format!("ardftsrc create: {e:?}"))?;
    let setup = cpu() - t;
    let live_setup = LIVE.load(Relaxed).saturating_sub(before);
    let chunk = r.input_buffer_size();
    let mut out = vec![T::zero(); r.output_buffer_size()];
    let delay = r.output_delay_frames();
    let mut pending: Vec<T> = Vec::with_capacity(chunk + block * ch);
    let t = cpu();
    for blk in input.chunks(block * ch) {
        pending.extend(blk.iter().map(|&x| to(x)));
        let mut pos = 0;
        // Hold back a full chunk so the last one goes through process_chunk_final.
        while pending.len() - pos > chunk {
            let n = r.process_chunk(&pending[pos..pos + chunk], &mut out).map_err(|e| format!("ardftsrc process: {e:?}"))?;
            result.extend(out[..n].iter().map(|&x| from(x)));
            pos += chunk;
        }
        pending.drain(..pos);
    }
    let n = r.process_chunk_final(&pending, &mut out).map_err(|e| format!("ardftsrc final: {e:?}"))?;
    result.extend(out[..n].iter().map(|&x| from(x)));
    let n = r.finalize(&mut out).map_err(|e| format!("ardftsrc finalize: {e:?}"))?;
    result.extend(out[..n].iter().map(|&x| from(x)));
    Ok((setup, cpu() - t, live_setup, chunk, delay))
}

fn run() -> Result<(), String> {
    let a: Vec<String> = std::env::args().collect();
    if a.len() != 10 && a.len() != 11 {
        return Err("usage: rust_driver tuneout|ardftsrc-rs variant input.f32 output.f32 in_rate out_rate channels block_frames output_capacity".into());
    }
    let engine = a[1].as_str();
    let variant = a[2].as_str();
    let num = |i: usize| a[i].parse::<usize>().map_err(|e| format!("argv[{i}]: {e}"));
    let (ir, orate, ch, block) = (num(5)?, num(6)?, num(7)?, num(8)?);
    let _capacity = num(9)?;
    if ir == 0 || orate == 0 || ch == 0 || block == 0 { return Err("zero dimension".into()); }
    let bytes = std::fs::read(&a[3]).map_err(|e| format!("cannot open input: {e}"))?;
    if bytes.len() % (4 * ch) != 0 { return Err("unaligned input".into()); }
    let input: Vec<f32> = bytes.chunks_exact(4).map(|b| f32::from_le_bytes([b[0], b[1], b[2], b[3]])).collect();
    drop(bytes);
    let frames = input.len() / ch;
    let mut result: Vec<f32> = Vec::with_capacity(((frames * orate + ir - 1) / ir + 131072) * ch);
    let (setup, process, live_setup);
    let extra: String;

    match engine {
        "tuneout" => {
            use tuneout_resampler::{FilterLength, FilterLengthPolicy, Resampler};
            let length = match variant {
                "standard" => FilterLength::Standard,
                "long" => FilterLength::Long,
                "extralong" => FilterLength::ExtraLong,
                _ => return Err("tuneout variant must be standard|long|extralong".into()),
            };
            let before = LIVE.load(Relaxed);
            let t = cpu();
            let mut r = Resampler::with_filter_length_policy(ir as u32, orate as u32, ch, length, FilterLengthPolicy::Generic)
                .map_err(|e| format!("tuneout create: {e:?}"))?;
            setup = cpu() - t;
            live_setup = LIVE.load(Relaxed).saturating_sub(before);
            let info = r.filter_info();
            extra = format!(",\"taps_per_phase\":{},\"phases\":{},\"lookahead_input_frames\":{}", info.taps_per_phase, info.phases, info.lookahead_input_frames);
            let t = cpu();
            for blk in input.chunks(block * ch) {
                let o = r.process_f32(blk, false).map_err(|e| format!("tuneout process: {e:?}"))?;
                result.extend_from_slice(&o);
            }
            let o = r.process_f32(&[], true).map_err(|e| format!("tuneout flush: {e:?}"))?;
            result.extend_from_slice(&o);
            process = cpu() - t;
        }
        "ardftsrc-rs" => {
            use ardftsrc::Config;
            let mut cfg = Config::new(ir, orate, ch);
            if variant == "matched" { cfg = cfg.with_quality(16384).with_bandwidth(0.99); }
            else if variant != "default" { return Err("ardftsrc-rs variant must be matched|default".into()); }
            // The crate rejects quality 16384 with f32 samples (QualityTooHighForF32), so the
            // matched variant runs f64 internally with f32 file I/O; default runs f32 samples.
            let r = if variant == "matched" {
                ardftsrc_stream::<f64>(cfg, &input, block, ch, &mut result, |x| x as f64, |x| x as f32)?
            } else {
                ardftsrc_stream::<f32>(cfg, &input, block, ch, &mut result, |x| x, |x| x)?
            };
            setup = r.0; process = r.1; live_setup = r.2;
            extra = format!(",\"sample_type\":\"{}\",\"input_chunk_samples\":{},\"output_delay_frames\":{}", if variant == "matched" { "f64" } else { "f32" }, r.3, r.4);
        }
        _ => return Err("unknown engine".into()),
    }
    if result.iter().any(|x| !x.is_finite()) { return Err("nonfinite output".into()); }
    let mut f = std::fs::File::create(&a[4]).map_err(|e| format!("output: {e}"))?;
    let mut buf = Vec::with_capacity(result.len() * 4);
    for x in &result { buf.extend_from_slice(&x.to_le_bytes()); }
    f.write_all(&buf).map_err(|e| format!("output write: {e}"))?;
    let mut ru: libc::rusage = unsafe { std::mem::zeroed() };
    unsafe { libc::getrusage(libc::RUSAGE_SELF, &mut ru) };
    let name = if engine == "tuneout" { format!("tuneout-{variant}") } else if variant == "matched" { "ardftsrc-rs".to_string() } else { format!("ardftsrc-rs-{variant}") };
    println!("{{\"engine\":\"{name}\",\"io_type\":\"f32\",\"input_frames\":{frames},\"output_frames\":{},\"setup_cpu_seconds\":{setup:.17},\"processing_cpu_seconds\":{process:.17},\"api_total_cpu_seconds\":{:.17},\"setup_live_heap_bytes\":{live_setup},\"peak_live_heap_bytes\":{},\"whole_process_peak_rss_kib\":{}{extra}}}",
        result.len() / ch, setup + process, PEAK.load(Relaxed), ru.ru_maxrss);
    Ok(())
}

fn main() {
    if let Err(e) = run() { eprintln!("{e}"); std::process::exit(1); }
}
