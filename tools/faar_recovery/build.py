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

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,default=Path('/tmp/faar-recovery'))
    p.add_argument('--sox-source',type=Path,default=Path('/tmp/sox-vulkan-audit'))
    p.add_argument('--kbsrc-source',type=Path,default=Path('/tmp/kbsrc'))
    p.add_argument('--ardft-source',type=Path,default=Path(__file__).with_name('ardftsrc_reference.cpp'))
    p.add_argument('--ardft-deps',type=Path,default=Path('/tmp/ardft-deps/root/usr'))
    p.add_argument('--audiofile-include',type=Path,default=Path('/tmp/ARDFTSRC-AudioFile'))
    a=p.parse_args(); a.root.mkdir(parents=True,exist_ok=True)
    env=dict(os.environ,CCACHE_DIR='/tmp/faac-ccache')
    commands=[]
    def run(cmd,name):
        cmd=list(map(str,cmd));commands.append(cmd)
        with (a.root/(name+'.log')).open('w') as log:
            subprocess.run(cmd,check=True,stdout=log,stderr=subprocess.STDOUT,env=env,cwd=REPO)
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
    run(['c++','-std=c++17','-O3','-g','-flto','-I'+str(REPO/'include'),'-I'+str(a.sox_source/'src'),'-I'+str(a.kbsrc_source/'include'),REPO/'tools/faar_recovery/driver.cpp',a.root/'kbsrc-cpu-build/libkbsrc.a','-L'+str(a.root/'sox-instrumented-build/src'),'-Wl,-rpath,'+str(a.root/'sox-instrumented-build/src'),'-llibsox','-ldl','-lpthread','-lm','-o',a.root/'driver'],'driver-build')
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
    manifest={'commands':commands,'source_sha256':{str(x):sha(x) for x in paths},'sox_revision':revision(a.sox_source),'kbsrc_revision':revision(a.kbsrc_source),'compiler':subprocess.check_output(['cc','--version'],text=True).splitlines()[0], 'reconstruction':'127-tap fixed 0.49 cycles/input-sample output filter from saved Callgrind annotated source; this is a reconstructed configuration, not an exact historical source snapshot', 'sox_instrumentation':'Scratch copy: optional raw prototype and phase-bank export at setup; debug parameters printed with 17 digits. No arithmetic or planner changes.', 'kbsrc_instrumentation':'Only existing resampler.cpp now() clock changed from steady wall clock to process CPU clock. No arithmetic, kernel, block geometry, or dispatch changes.'}
    (a.root/'build-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(a.root/'build-manifest.json')

if __name__=='__main__':main()
