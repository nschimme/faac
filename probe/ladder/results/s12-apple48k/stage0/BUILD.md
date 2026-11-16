# S12 stage0 builds (all under /home/user/work; paths in /home/user/work/ENV.sh)

Host: 4 CPU, gcc 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04.1), cmake 3.28.3, ninja 1.11.1, meson 1.12.1 (venv), python 3.13.18, ffmpeg/ffprobe 6.1.1-3ubuntu5 (/usr/bin).

| component | source / commit | build |
|---|---|---|
| faac master | origin/master `3a656f77466396621f75f9b1d83b6b9faf70eb4f` (worktree /home/user/work/faac-master, detached) | `meson setup build --buildtype=release && ninja -C build`; binaries build/frontend/{faac,faad,faam}; `faac --help` banner `FAAC 3.0.0 (3a656f77)` |
| FAAD2 | nschimme/faad2 `287c00ccf20086db395869cfe8b09fc50a76e988` (HANDOFF-pinned; prints "V2.11.4") | `cmake -S faad2-src -B faad2-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DFAAD_BUILD_CLI=ON -DCMAKE_INSTALL_PREFIX=/home/user/work/faad2-prefix; cmake --build; cmake --install` -> faad2-prefix/bin/faad |
| libfdk-aac | mstorsjo/fdk-aac tag v2.0.0 `2326faaf8f2cdf2c3a9108ccdaf1d7551aec543e` | v2.0.0 ships no CMakeLists; used v2.0.2's `CMakeLists.txt`, `fdk-aac-config.cmake.in`, `fdk-aac.def` (untracked copies in fdk-src) with `libSBRdec/src/sbr_crc.cpp` added to the source list (needed by v2.0.0 sbrdecoder). Library sources are exactly v2.0.0. `cmake -S fdk-src -B fdk-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DBUILD_PROGRAMS=OFF -DCMAKE_INSTALL_PREFIX=/home/user/work/fdkprefix` |
| fdkaac CLI | nu774/fdkaac `cdfd8a555c31ffa7d0c2e303e84bb197ba518ed8` (v1.0.9-9) | `autoreconf -i; PKG_CONFIG_PATH=/home/user/work/fdkprefix/lib/pkgconfig ./configure --prefix=/home/user/work/fdkaac-prefix; make` -> fdkaac-src/fdkaac (not installed) |
| fdkdec | faac-benchmark `9f5f1027d529fc780d226ad2d8fe250faaea6148` scripts/build_fdkdec.sh | `PKG_CONFIG_PATH=/home/user/work/fdkprefix/lib/pkgconfig FAAC_SRC=/home/user/work/faac-master scripts/build_fdkdec.sh` -> fb/bin/fdkdec (links faac-master libfaam.a) |
| libxaac | ittiam-systems/libxaac `2fbadd57bca46693b7a077bcf1513131106d960c` (v0.1.14, shallow clone HEAD) | `FAAC_DIR=/home/user/work/faac-master XAAC_BUILD_DIR=/home/user/work/xaac-build scripts/build_libxaac.sh` -> fb/bin/xaacenc, fb/bin/xaacdec. Built OK; encode smoke-tested (`-aot:2`), not scored. |
| ffmpeg | system 6.1.1-3ubuntu5 | /usr/bin/ffmpeg (corpus builder in manifest used 9.0.2 Homebrew) |
| faac-benchmark | `9f5f1027d529fc780d226ad2d8fe250faaea6148` (v1-162-g9f5f102) | /home/user/work/fb; venv /home/user/work/venv (zimtohrli 0.2.1.post1, scipy 1.18.1, numpy 2.5.5, soundfile 0.14.0, visqol-python 3.8.0, meson) |

The benchmark's own FAAD2 reference is 2.11.4 (FreewareAdvancedAudio tarball, build_faad2.sh); its default scoring decoder is the in-tree FAAD3 (`frontend/faad`, run with `--strict`), which the harness exposes as decoder `faad3`. Decoder `faad2` is the 287c00c build.

Note: default `meson setup build --buildtype=release` of master has no HE-AAC v2 (needs `-Dencoder-ps=true`); LC and HE-v1 work.
