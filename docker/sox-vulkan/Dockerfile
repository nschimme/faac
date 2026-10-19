# Build SoX Vulkan from its upstream release tag.
# Override SOX_REF with a release tag and SOX_COMMIT with its full commit ID.
FROM debian:trixie

ARG DEBIAN_FRONTEND=noninteractive
ARG SOX_REF=sox-16.2.2
ARG SOX_COMMIT=54102e40ffbdbe991505b9ac0a7c62e0b4d4ae3c

RUN apt-get update && apt-get install -y --no-install-recommends \
      bash build-essential ca-certificates cmake curl git pkg-config time \
      autoconf automake libtool xz-utils bzip2 unzip \
      libvulkan-dev libvulkan1 vulkan-tools glslc glslang-dev glslang-tools \
      mesa-vulkan-drivers \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
RUN git clone --depth 1 --branch "${SOX_REF}" \
      https://github.com/simonefil/sox-vulkan.git sox-vulkan \
    && test "$(git -C sox-vulkan rev-parse HEAD)" = "${SOX_COMMIT}" \
    && cd sox-vulkan \
    && ./build_static_libs.sh --vulkan --no-ffmpeg --no-amr --no-mp2 \
      --no-ogg --no-flac --no-opus --no-mp3 --no-wavpack --no-sndfile \
      --no-id3tag --no-png --no-magic \
    && install -D -m 0755 output/sox /usr/local/bin/sox

WORKDIR /work
ENTRYPOINT ["/usr/local/bin/sox"]
CMD ["--version"]
