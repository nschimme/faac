#!/usr/bin/env python3
"""Build isolated diagnostic candidates; never edit a resampler source in place."""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
# External Rust references: cloned and pinned here, never linked into libfaar.
TUNEOUT_URL='https://github.com/studio-nodoka/tuneout-resampler'
TUNEOUT_COMMIT='a0b9f56c256ac67efe9d133438fe007a19332744'
ARDFTSRC_RS_URL='https://github.com/phayes/ardftsrc-rs'
ARDFTSRC_RS_COMMIT='78305ae1a26ce0676a099c3666d20bd31e2dbfa1'
RUST_VERSION='1.91.0'  # tuneout-resampler requires rustc >= 1.89

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def build_external(a,run,commands,env):
    """Clone/pin the Rust references, stage soxr.h, build the Rust driver. Returns (soxr_inc, rust_dir, versions)."""
    a.refs.mkdir(parents=True,exist_ok=True)
    for name,url,commit in [('tuneout-resampler',TUNEOUT_URL,TUNEOUT_COMMIT),('ardftsrc-rs',ARDFTSRC_RS_URL,ARDFTSRC_RS_COMMIT)]:
        d=a.refs/name
        if not d.exists():run(['git','clone',url,d],name+'-clone')
        run(['git','-C',d,'checkout','--detach',commit],name+'-checkout')
        if subprocess.check_output(['git','-C',str(d),'rev-parse','HEAD'],text=True).strip()!=commit:raise RuntimeError(name+' is not at the pinned commit')
    soxr_inc=a.refs/'soxr-include';soxr_inc.mkdir(exist_ok=True);shutil.copy2(a.soxr_header,soxr_inc/'soxr.h')
    toolchain=a.rust_toolchain or (a.root/'rust-toolchain' if (a.root/'rust-toolchain/bin/cargo').exists() else None)
    rust_env=dict(env,CARGO_TARGET_DIR=str(a.root/'rust-target'))
    if toolchain:rust_env['PATH']=str(toolchain/'bin')+os.pathsep+rust_env['PATH']
    rust_dir=REPO/'tools/faar_recovery/rust_driver'
    if a.refs.resolve()!=Path('/tmp/faar-recovery/refs'):raise RuntimeError('rust_driver/Cargo.toml path dependencies are fixed to /tmp/faar-recovery/refs')
    cmd=['cargo','build','--release','--manifest-path',str(rust_dir/'Cargo.toml')];commands.append(cmd)
    with (a.root/'rust-driver-build.log').open('w') as log:subprocess.run(cmd,check=True,stdout=log,stderr=subprocess.STDOUT,env=rust_env,cwd=REPO)
    shutil.copy2(a.root/'rust-target/release/faar-recovery-rust-driver',a.root/'rust_driver')
    rustc_version=subprocess.check_output(['rustc','--version'],text=True,env=rust_env).strip()
    cargo_version=subprocess.check_output(['cargo','--version'],text=True,env=rust_env).strip()
    soxr_version=subprocess.check_output(['dpkg-query','-W','-f=${Version}','libsoxr0:amd64'],text=True).strip() if shutil.which('dpkg-query') else None
    return soxr_inc,rust_dir,dict(rustc=rustc_version,cargo=cargo_version,soxr=soxr_version)

def driver_command(a,soxr_inc,sox_dst_unused=None):
    return ['c++','-std=c++17','-O3','-g','-flto','-I'+str(REPO/'include'),'-I'+str(a.sox_source/'src'),'-I'+str(a.kbsrc_source/'include'),'-I'+str(soxr_inc),REPO/'tools/faar_recovery/driver.cpp',a.root/'kbsrc-cpu-build/libkbsrc.a',a.soxr_library,'-L'+str(a.root/'sox-instrumented-build/src'),'-Wl,-rpath,'+str(a.root/'sox-instrumented-build/src'),'-llibsox','-ldl','-lpthread','-lm','-o',a.root/'driver']

def external_manifest(a,versions):
    revision=lambda src:subprocess.check_output(['git','-C',str(src),'rev-parse','HEAD'],text=True).strip()
    soxr_version,rustc_version,cargo_version=versions['soxr'],versions['rustc'],versions['cargo']
    return {
        'soxr':{'header':str(a.soxr_header),'header_source':'qaac copy (headers not installed); NOT libsoxr 0.1.3 source','header_sha256':sha(a.soxr_header),'library':str(a.soxr_library.resolve()),'library_sha256':sha(a.soxr_library.resolve()),'debian_package_version':soxr_version,'qaac_configuration':'soxr_quality_spec(SOXR_HQ|SOXR_VHQ,0); driver uses float32 interleaved I/O for both, one thread','license':'LGPL-2.1-or-later (library); header copy as shipped by qaac'},
        'tuneout_resampler':{'url':TUNEOUT_URL,'commit':revision(a.refs/'tuneout-resampler'),'license':'Apache-2.0','policy':'Generic','presets':['standard','long','extralong'],'io_type':'f32'},
        'ardftsrc_rs':{'url':ARDFTSRC_RS_URL,'commit':revision(a.refs/'ardftsrc-rs'),'license':'MIT OR Apache-2.0','config':{'matched':'quality 16384, bandwidth 0.99','default':'crate default (quality 2304, bandwidth 0.975)'},'io_type':'f32'},
        'rust':{'rustc':rustc_version,'cargo':cargo_version}}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,default=Path('/tmp/faar-recovery'))
    p.add_argument('--sox-source',type=Path,default=Path('/tmp/sox-vulkan-audit'))
    p.add_argument('--kbsrc-source',type=Path,default=Path('/tmp/kbsrc'))
    p.add_argument('--ardft-source',type=Path,default=Path(__file__).with_name('ardftsrc_reference.cpp'))
    p.add_argument('--ardft-deps',type=Path,default=Path('/tmp/ardft-deps/root/usr'))
    p.add_argument('--audiofile-include',type=Path,default=Path('/tmp/ARDFTSRC-AudioFile'))
    p.add_argument('--external-only',action='store_true',help='only (re)build the soxr/Rust reference drivers and merge their manifest entries into an existing build-manifest.json')
    p.add_argument('--refs',type=Path,help='external reference checkouts (default ROOT/refs)')
    p.add_argument('--soxr-header',type=Path,default=Path('/home/nschimme/gitprojects/qaac/include/soxr.h'),
                   help='soxr.h copy (libsoxr headers are not installed); default is the qaac copy')
    p.add_argument('--soxr-library',type=Path,default=Path('/usr/lib/x86_64-linux-gnu/libsoxr.so.0'),
                   help='runtime libsoxr linked by the C++ driver')
    p.add_argument('--rust-toolchain',type=Path,help='directory with bin/cargo >= 1.89 (default ROOT/rust-toolchain, else PATH)')
    a=p.parse_args(); a.refs=a.refs or a.root/'refs'; a.root.mkdir(parents=True,exist_ok=True)
    env=dict(os.environ,CCACHE_DIR='/tmp/faac-ccache')
    commands=[]
    def run(cmd,name):
        cmd=list(map(str,cmd));commands.append(cmd)
        with (a.root/(name+'.log')).open('w') as log:
            subprocess.run(cmd,check=True,stdout=log,stderr=subprocess.STDOUT,env=env,cwd=REPO)
    soxr_inc,rust_dir,versions=build_external(a,run,commands,env)
    if a.external_only:
        run(driver_command(a,soxr_inc),'driver-build')
        mpath=a.root/'build-manifest.json';manifest=json.loads(mpath.read_text())
        manifest['external_references']=external_manifest(a,versions);manifest['commands']+=commands
        extra_paths=[a.root/'driver',a.root/'rust_driver',rust_dir/'Cargo.toml',rust_dir/'Cargo.lock',rust_dir/'src/main.rs',REPO/'tools/faar_recovery/driver.cpp',a.soxr_header,a.soxr_library.resolve()]
        manifest['source_sha256'].update({str(x):sha(x) for x in extra_paths})
        mpath.write_text(json.dumps(manifest,indent=2)+'\n');print(mpath);return
    sources=['dft_coeffs.c','dft_prefilter.c','dft_fir.c','fft_double.c']
    poly=REPO/'libfaar/polyresample.c'; text=poly.read_text()
    old='double cutoff_factor = 0.48;\n    double cutoff = (r->ratio < 1.0) ? cutoff_factor * r->ratio : 0.48;'
    new='double cutoff = (r->ratio < 1.0)\n        ? (filter_len_override ? 0.49 : 0.48 * r->ratio) : 0.48;'
    if old in text:
        baseline=text.replace(old,new);regression=text
    elif new in text:
        baseline=text;regression=text.replace(new,old)
    else:
        raise RuntimeError('cutoff configuration changed; review reconstruction before rebuilding')
    recovered=a.root/'reconstructed/polyresample.c';recovered.parent.mkdir(exist_ok=True);recovered.write_text(baseline)
    regressed=a.root/'regressed/polyresample.c';regressed.parent.mkdir(exist_ok=True);regressed.write_text(regression)
    cc=['cc','-O3','-g','-flto','-fPIC','-shared','-DFAAR_BUILDING','-DFAAR_DOUBLE_COEFFICIENTS','-I'+str(REPO/'include'),'-I'+str(REPO/'libfaar')]
    for name,src,staged in [('default',poly,False),('regressed',regressed,True),('reconstructed',recovered,True)]:
        d=a.root/name;d.mkdir(exist_ok=True)
        run(cc+(['-DFAAR_STAGED_BACKEND'] if staged else [])+[src]+([REPO/'libfaar'/x for x in sources] if staged else [])+['-lm','-o',d/'libfaar.so'],name+'-build')
        for target in ['quality','tuneout_matrix']:
            opts=[]
            if target=='tuneout_matrix':
                fixture='matrix-fingerprints-staged-double.txt' if staged else 'matrix-fingerprints-double.txt'
                opts=['-DFAAR_RATE_FIXTURE_PATH="'+str(REPO/'libfaar/verification/fixtures/rates.txt')+'"', '-DFAAR_FINGERPRINT_FIXTURE_PATH="'+str(REPO/'libfaar/verification/fixtures'/fixture)+'"']
            run(['cc','-O3','-g','-I'+str(REPO/'include'),REPO/'libfaar'/('test_faar_'+target+'.c'),*opts,'-L'+str(d),'-Wl,-rpath,'+str(d),'-lfaar','-lm','-o',d/target],name+'-'+target+'-build')
    soxr_inc,rust_dir,versions=build_external(a,run,commands,env)
    sox_dst=a.root/'sox-instrumented-source'; sox_dst.mkdir(exist_ok=True)
    for name in ['CMakeLists.txt','src','libgsm','lpc10','sox.pc.in','COPYING','LICENSE.GPL','LICENSE.LGPL']:
        origin=a.sox_source/name
        if origin.is_dir():shutil.copytree(origin,sox_dst/name,dirs_exist_ok=True)
        else:shutil.copy2(origin,sox_dst/name)
    rate=sox_dst/'src/rate.c'; rate_text=rate.read_text()
    marker='      free(coef_lows);\n      free(coefs);'
    instrumentation='''      if (getenv("FAAR_RECOVERY_COEFFICIENTS")) {
        FILE *dump = fopen(getenv("FAAR_RECOVERY_COEFFICIENTS"), "wb");
        if (dump) {
          int dimensions[3] = {num_taps, num_coefs, phases};
          fwrite(dimensions, sizeof(int), 3, dump);
          fwrite(coefs, sizeof(double), num_taps, dump);
          fwrite(arb_stage.shared->poly_fir_coefs, sizeof(double), num_coefs * phases, dump);
          fclose(dump);
        }
      }
'''
    if marker not in rate_text:raise RuntimeError('SoX coefficient export location changed')
    rate.write_text(rate_text.replace(marker,instrumentation+marker))
    dsp=sox_dst/'src/effects_i_dsp.c';dsp.write_text(dsp.read_text().replace('Fc=%.7g β=%g ρ=%g','Fc=%.17g β=%.17g ρ=%.17g'))
    sox_text=(a.sox_source/'CMakeLists.txt').read_text()
    options=re.findall(r'option\((WITH_\w+)',sox_text)
    run(['cmake','-S',sox_dst,'-B',a.root/'sox-instrumented-build','-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DBUILD_SHARED_LIBS=ON','-DCMAKE_C_FLAGS_RELEASE=-O3 -g -flto -DNDEBUG']+['-D'+x+'=OFF' for x in options],'sox-configure')
    run(['cmake','--build',a.root/'sox-instrumented-build','--target','libsox','-j2'],'sox-build')
    run(['cmake','--build',a.root/'sox-instrumented-build','--target','sox_exe','-j2'],'sox-cli-build')
    shutil.copy2(a.root/'sox-instrumented-build/src/sox',a.root/'sox-native-cli')
    run(['cmake','-S',a.kbsrc_source,'-B',a.root/'kbsrc-portable','-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DKBSRC_ARCH=portable'],'kbsrc-original-configure')
    run(['cmake','--build',a.root/'kbsrc-portable','-j2'],'kbsrc-original-build')
    dst=a.root/'kbsrc-cpu-source';dst.mkdir(exist_ok=True)
    for name in ['CMakeLists.txt','src','include','app','tests','reference','LICENSE']:
        src=a.kbsrc_source/name
        if src.is_dir():shutil.copytree(src,dst/name,dirs_exist_ok=True)
        else:shutil.copy2(src,dst/name)
    timer=dst/'src/resampler.cpp'; s=timer.read_text()
    expression='std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count()'
    if expression not in s:raise RuntimeError('KBSRC timer changed; review timing-only instrumentation')
    timer.write_text(s.replace('#include <chrono>','#include <chrono>\n#include <ctime>\n#include <valgrind/callgrind.h>').replace(expression,'(double)std::clock() / CLOCKS_PER_SEC').replace('    const double t1 = now();','    if (std::getenv("FAAR_RECOVERY_PROFILE")) { CALLGRIND_START_INSTRUMENTATION; CALLGRIND_ZERO_STATS; }\n    const double t1 = now();'))
    run(['cmake','-S',dst,'-B',a.root/'kbsrc-cpu-build','-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DKBSRC_ARCH=portable','-DCMAKE_CXX_FLAGS_RELEASE=-O3 -g -flto -DNDEBUG'],'kbsrc-cpu-configure')
    run(['cmake','--build',a.root/'kbsrc-cpu-build','-j2'],'kbsrc-cpu-build')
    run(driver_command(a,soxr_inc),'driver-build')
    run(['c++','-std=c++17','-O3','-g','-flto','-I'+str(a.audiofile_include),'-I'+str(a.ardft_deps/'include'),'-I'+str(a.ardft_deps/'include/eigen3'),a.ardft_source,a.ardft_deps/'lib/x86_64-linux-gnu/libfftw3.a','-lm','-o',a.root/'ardftsrc-uninstrumented'],'ardft-original-build')
    run(['c++','-std=c++17','-O3','-g','-flto','-I'+str(a.kbsrc_source/'include'),REPO/'tools/faar_recovery/kbsrc_sweep.cpp',a.root/'kbsrc-cpu-build/libkbsrc.a','-lpthread','-lm','-o',a.root/'kbsrc-sweep'],'kbsrc-sweep-build')
    ardft=a.root/'ardftsrc-profile.cpp'; ardft_text=a.ardft_source.read_text()
    ardft_text=ardft_text.replace('#include <ctime>','#include <ctime>\n#include <valgrind/callgrind.h>')
    ardft_text=ardft_text.replace('clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &setup_end);','clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &setup_end);\n    CALLGRIND_START_INSTRUMENTATION; CALLGRIND_ZERO_STATS;')
    ardft_text=ardft_text.replace('clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &processing_end);','clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &processing_end);\n    CALLGRIND_STOP_INSTRUMENTATION;')
    ardft.write_text(ardft_text)
    run(['c++','-std=c++17','-O3','-g','-flto','-I'+str(a.audiofile_include),'-I'+str(a.ardft_deps/'include'),'-I'+str(a.ardft_deps/'include/eigen3'),ardft,a.ardft_deps/'lib/x86_64-linux-gnu/libfftw3.a','-lm','-o',a.root/'ardftsrc'],'ardft-build')
    run(['cc','-O3','-g','-flto','-I'+str(REPO/'libfaar'),'-c',REPO/'libfaar/fft_double.c','-o',a.root/'fft-micro.o'],'fft-micro-object')
    run(['c++','-std=c++17','-O3','-g','-flto','-I'+str(REPO/'libfaar'),'-I'+str(a.kbsrc_source/'src'),'-I'+str(a.ardft_deps/'include'),REPO/'tools/faar_recovery/fft_bench.cpp',a.root/'fft-micro.o',a.ardft_deps/'lib/x86_64-linux-gnu/libfftw3.a','-L'+str(a.root/'sox-instrumented-build/src'),'-Wl,-rpath,'+str(a.root/'sox-instrumented-build/src'),'-llibsox','-lpthread','-lm','-o',a.root/'fft-bench'],'fft-micro-build')
    run(['cc','-O3','-g','-I'+str(REPO/'include'),REPO/'tools/faar_recovery/memory.c','-ldl','-o',a.root/'memory'],'memory-build')
    revision=lambda src:subprocess.check_output(['git','-C',str(src),'rev-parse','HEAD'],text=True).strip()
    paths=[poly,recovered,regressed,REPO/'tools/faar_recovery/driver.cpp',a.sox_source/'src/rate.c',a.kbsrc_source/'src/resampler.cpp',timer,rate,dsp,ardft,a.ardft_source,a.ardft_deps/'lib/x86_64-linux-gnu/libfftw3.a',a.root/'ardftsrc',a.root/'driver']+[a.root/x/'libfaar.so' for x in ['default','regressed','reconstructed']]
    paths += sorted((REPO/'libfaar').glob('*.c')) + sorted((REPO/'libfaar').glob('*.h')) + [REPO/'include/faar.h'] + sorted((REPO/'tools/faar_recovery').glob('*.py')) + sorted((REPO/'tools/faar_recovery').glob('*.cpp')) + sorted((REPO/'tools/faar_recovery').glob('*.c')) + [a.audiofile_include/'AudioFile.h']
    paths += [a.root/'rust_driver',rust_dir/'Cargo.toml',rust_dir/'Cargo.lock',rust_dir/'src/main.rs',a.soxr_header,a.soxr_library.resolve()]
    manifest={'external_references':external_manifest(a,versions),
        'commands':commands,'source_sha256':{str(x):sha(x) for x in paths},'sox_revision':revision(a.sox_source),'kbsrc_revision':revision(a.kbsrc_source),'compiler':subprocess.check_output(['cc','--version'],text=True).splitlines()[0], 'reconstruction':'127-tap fixed 0.49 cycles/input-sample output filter from saved Callgrind annotated source; this is a reconstructed configuration, not an exact historical source snapshot', 'sox_instrumentation':'Scratch copy: optional raw prototype and phase-bank export at setup; debug parameters printed with 17 digits. No arithmetic or planner changes.', 'kbsrc_instrumentation':'Only existing resampler.cpp now() clock changed from steady wall clock to process CPU clock. No arithmetic, kernel, block geometry, or dispatch changes.'}
    (a.root/'build-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(a.root/'build-manifest.json')

if __name__=='__main__':main()
