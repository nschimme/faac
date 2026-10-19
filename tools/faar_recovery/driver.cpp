// Local reference driver. External engines are never linked into libfaar.
// Raw files are native little-endian float32 interleaved PCM, peak 1.0.
#include "faar.h"
#include "sox.h"
#include "kbsrc/kbsrc.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <dlfcn.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <malloc.h>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <vector>
#include <valgrind/callgrind.h>

static void profile_start() { CALLGRIND_START_INSTRUMENTATION; CALLGRIND_ZERO_STATS; }
static void profile_stop() { CALLGRIND_STOP_INSTRUMENTATION; }
static double cpu() { timespec t{}; clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static size_t heap() { auto m = mallinfo2(); return m.uordblks + m.hblkhd; }
static std::vector<double> sx_input;
static std::vector<float> result;
static size_t sx_pos;
static int source_drain(sox_effect_t*, sox_sample_t* out, size_t* n) {
    *n = std::min(*n, sx_input.size() - sx_pos);
    std::copy_n(sx_input.data() + sx_pos, *n, out); sx_pos += *n;
    return *n ? SOX_SUCCESS : SOX_EOF;
}
static int sink_flow(sox_effect_t*, const sox_sample_t* in, sox_sample_t*, size_t* ni, size_t* no) {
    result.insert(result.end(), in, in + *ni); *no = 0; return SOX_SUCCESS;
}
static const sox_effect_handler_t source_handler = {"recovery-input", nullptr, SOX_EFF_MCHAN, nullptr, nullptr, nullptr, source_drain, nullptr, nullptr, 0};
static const sox_effect_handler_t sink_handler = {"recovery-output", nullptr, SOX_EFF_MCHAN, nullptr, nullptr, sink_flow, nullptr, nullptr, nullptr, 0};

int main(int argc, char** argv) try {
    if (argc != 10 && argc != 11) throw std::runtime_error("usage: driver faar|sox|kbsrc-fast|kbsrc-exact library-or-engine input.f32 output.f32 in_rate out_rate channels block_frames output_capacity [faar_quality]");
    const std::string engine = argv[1], library = argv[2];
    const unsigned ir = std::stoul(argv[5]), orate = std::stoul(argv[6]), ch = std::stoul(argv[7]);
    const size_t block = std::stoull(argv[8]), capacity = std::stoull(argv[9]);
    const int faar_quality = argc == 11 ? std::stoi(argv[10]) : FAAR_QUALITY_HIGH;
    if (faar_quality < FAAR_QUALITY_FAST || faar_quality > FAAR_QUALITY_HIGH)
        throw std::runtime_error("faar_quality must be 0, 1, or 2");
    if (!ir || !orate || !ch || !block || !capacity) throw std::runtime_error("zero dimension");
    std::ifstream file(argv[3], std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("cannot open input");
    const size_t bytes = (size_t)file.tellg();
    if (bytes % (sizeof(float) * ch)) throw std::runtime_error("unaligned input");
    std::vector<float> input(bytes / sizeof(float)); file.seekg(0); file.read((char*)input.data(), bytes);
    if (!file) throw std::runtime_error("input read failed");
    const size_t frames = input.size() / ch;
    result.reserve(((frames * (uint64_t)orate + ir - 1) / ir + 131072) * ch);
    double setup = 0, process = 0, api_total = 0;
    size_t live_setup = 0;
    std::string extra;
    if (engine == "faar") {
        void* dll = dlopen(library.c_str(), RTLD_NOW); if (!dll) throw std::runtime_error(dlerror());
        auto create = (decltype(&faar_create))dlsym(dll,"faar_create");
        auto boundary = (decltype(&faar_set_boundary))dlsym(dll,"faar_set_boundary");
        auto run = (decltype(&faar_process))dlsym(dll,"faar_process");
        auto flush = (decltype(&faar_flush))dlsym(dll,"faar_flush");
        auto destroy = (decltype(&faar_destroy))dlsym(dll,"faar_destroy");
        if (!create || !boundary || !run || !flush || !destroy) throw std::runtime_error("missing FAAR symbol");
        auto reset_timers = (void (*)())dlsym(dll,"faar_recovery_reset");
        auto get_timers = (void (*)(double*))dlsym(dll,"faar_recovery_get");
        for (float& x: input) x *= 32768.0f;
        std::vector<float> out(capacity * ch);
        size_t before = heap(); double t = cpu();
        auto h = create(ir, orate, ch, static_cast<faar_quality_t>(faar_quality)); if (!h) throw std::runtime_error("FAAR create failed");
        if (boundary(h, FAAR_BOUNDARY_ZERO)) throw std::runtime_error("FAAR boundary failed");
        setup = cpu() - t; live_setup = heap() - before;
        if (reset_timers) reset_timers();
        auto collect = [&](size_t n) { for (size_t i=0; i<n*ch; ++i) result.push_back(out[i] / 32768.0f); };
        profile_start(); t = cpu();
        for (size_t pos = 0; pos < frames; pos += block) {
            size_t n = std::min(block, frames-pos);
            collect(run(h, input.data()+pos*ch, n, out.data(), capacity));
            for (;;) { n = run(h, nullptr, 0, out.data(), capacity); if (!n) break; collect(n); }
        }
        size_t guard = 0;
        for (;;) { size_t n = flush(h, out.data(), capacity); if (!n) break; collect(n); if (++guard > frames+1024) throw std::runtime_error("flush did not terminate"); }
        process = cpu()-t; profile_stop(); api_total = setup + process;
        if (get_timers) {
            double counters[3]; get_timers(counters);
            extra=",\"prefilter_cpu_seconds\":"+std::to_string(counters[0])+",\"polyphase_cpu_seconds\":"+std::to_string(counters[1])+",\"real_fft_cpu_seconds_in_prefilter\":"+std::to_string(counters[2]);
        }
        destroy(h); dlclose(dll);
    } else if (engine == "sox") {
        sx_input.assign(input.begin(), input.end());
        size_t before = heap(); double t = cpu(); sox_init();
        sox_get_globals()->use_threads = sox_false;
        sox_get_globals()->bufsiz = block * ch;
        sox_get_globals()->verbosity = getenv("FAAR_RECOVERY_DEBUG") ? 6 : 1;
        sox_encodinginfo_t encoding{}; sox_init_encodinginfo(&encoding);
        auto chain = sox_create_effects_chain(&encoding, &encoding);
        sox_signalinfo_t signal{}; signal.rate=ir; signal.channels=ch; signal.precision=32; signal.length=input.size();
        sox_signalinfo_t target = signal; target.rate = orate;
        auto add = [&](sox_effect_t* e) { if (sox_add_effect(chain,e,&signal,&target)!=SOX_SUCCESS) throw std::runtime_error("SoX add effect failed"); free(e); };
        add(sox_create_effect(&source_handler));
        auto rate = sox_create_effect(sox_find_effect("rate"));
        std::string rate_arg=std::to_string(orate);
        std::vector<std::string> opts={"-v"};
        if (library == "wide") opts={"-v","-b","99.5"};
        if (library == "steep") opts.push_back("-s");
        if (library == "extreme") opts={"-x","-s"};
        opts.push_back(rate_arg); std::vector<char*> args; for(auto& x:opts) args.push_back(x.data());
        if(sox_effect_options(rate,args.size(),args.data())!=SOX_SUCCESS) throw std::runtime_error("SoX options failed");
        add(rate); add(sox_create_effect(&sink_handler));
        setup=cpu()-t; live_setup=heap()-before;
        profile_start(); t=cpu(); if(sox_flow_effects(chain,nullptr,nullptr)!=SOX_SUCCESS) throw std::runtime_error("SoX flow failed");
        process=cpu()-t; profile_stop(); api_total=setup+process;
        sox_delete_effects_chain(chain); sox_quit();
    } else if (engine == "kbsrc-fast" || engine == "kbsrc-exact") {
        setenv("KBSRC_CPU",library.c_str(),1);
        std::vector<double> source(input.begin(),input.end());
        kbsrc::Options opts; opts.threads=1; opts.lpc_edges=false;
        opts.precision = engine == "kbsrc-fast" ? kbsrc::Precision::Fast : kbsrc::Precision::Exact;
        kbsrc::Stats stats; double t=cpu();
        auto output=kbsrc::resample(source.data(),frames,ch,ir,orate,opts,&stats);
        api_total=cpu()-t; profile_stop();
        // Linked diagnostic KBSRC copy uses process CPU clock at its existing timing boundaries.
        setup=stats.design_seconds; process=stats.process_seconds;
        for(size_t i=0; i<(output.empty()?0:output[0].size()); ++i)
            for(unsigned c=0;c<ch;++c) result.push_back((float)output[c][i]);
        extra=",\"selected_isa\":\""+std::string(stats.isa)+"\",\"block_factor\":"+std::to_string(stats.block)+",\"kernel_taps\":"+std::to_string(stats.taps)+",\"general_fold\":"+std::to_string(stats.general_fold)+",\"passband_hz\":"+std::to_string(stats.passband)+",\"stopband_hz\":"+std::to_string(stats.stopband);
    } else throw std::runtime_error("unknown engine");
    for(float x:result) if(!std::isfinite(x)) throw std::runtime_error("nonfinite output");
    std::ofstream out(argv[4],std::ios::binary); out.write((char*)result.data(),result.size()*sizeof(float));
    if(!out) throw std::runtime_error("output write failed");
    rusage usage{}; getrusage(RUSAGE_SELF,&usage);
    std::cout<<std::setprecision(17)<<"{\"engine\":\""<<engine<<"\",\"input_frames\":"<<frames<<",\"output_frames\":"<<result.size()/ch<<",\"setup_cpu_seconds\":"<<setup<<",\"processing_cpu_seconds\":"<<process<<",\"api_total_cpu_seconds\":"<<api_total<<",\"setup_live_heap_bytes\":"<<live_setup<<",\"whole_process_peak_rss_kib\":"<<usage.ru_maxrss<<extra<<"}\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<"\n"; return 1; }
