# SoX Vulkan container

Build the image from the pinned upstream release tag:

```sh
podman build --platform linux/amd64 --pull=always \
  -t sox-vulkan:16.2.2 -f docker/sox-vulkan/Dockerfile .
```

The build uses SoX Vulkan's `sox-16.2.2` release tag and checks that it resolves
to commit `54102e40ffbdbe991505b9ac0a7c62e0b4d4ae3c`. It enables Vulkan and
omits optional codec support. The image keeps the Debian Vulkan loader and Mesa
software driver so it can also run without a passed-through GPU.

Check the build and list the Vulkan devices visible to the container:

```sh
podman run --rm sox-vulkan:16.2.2 --version
podman run --rm --entrypoint vulkaninfo sox-vulkan:16.2.2 --summary
```

For a host GPU, expose the device and host driver libraries as required by the
host's Vulkan setup. On common Linux systems this starts with:

```sh
podman run --rm --device /dev/dri sox-vulkan:16.2.2 \
  --vulkan-precise input.wav output.wav rate 44100
```

For software Vulkan, force Mesa's CPU ICD:

```sh
podman run --rm -e VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json \
  sox-vulkan:16.2.2 --vulkan-precise input.wav output.wav rate 44100
```

For CPU benchmarking through Podman, use `sox-podman-timed` as the SoX
executable passed to `tools/compare_faar_sox_vulkan.py`. It reports SoX's own
CPU time and peak RSS from inside the container. On cgroup v2 hosts, the wrapper
uses the container's microsecond-resolution CPU usage counter; GNU `time` is the
fallback. Podman's host child accounting only measures the container client. The
script mounts `/tmp`, so keep benchmark inputs and outputs there.

The executable accepts SoX's normal command line, including the upstream
`fast`, `precise`, and `reference` rate profiles. Mount input and output files
or use a work directory, for example:

```sh
podman run --rm -v "$PWD:/work" sox-vulkan:16.2.2 \
  --vulkan-precise input.wav output.wav rate 44100
```

To build another release, pass both `--build-arg SOX_REF=<tag>` and
`--build-arg SOX_COMMIT=<full-commit-id>`.
