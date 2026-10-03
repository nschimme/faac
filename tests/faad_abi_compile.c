/* Freestanding ABI checks: compile for 32/64-bit, little/big-endian targets.
 * clang -target powerpc-none-elf -ffreestanding -fshort-enums -fsyntax-only
 *       -Iinclude tests/faad_abi_compile.c
 */
#include "faad.h"

_Static_assert(sizeof(faad_status) == 4, "status ABI");
_Static_assert(sizeof(enum faad_object_type) == 4, "object ABI");
_Static_assert(sizeof(enum faad_stream_format) == 4, "stream ABI");
_Static_assert(sizeof(enum faad_output_format) == 4, "PCM ABI");
_Static_assert(sizeof(enum faad_downmix_mode) == 4, "downmix ABI");
_Static_assert(sizeof(bool) == 1 && sizeof(float) == 4, "flag/float ABI");
_Static_assert(sizeof(faad_config) >= 16, "config baseline layout");
_Static_assert(offsetof(faad_config, downmix_mode) == 12, "config baseline");
_Static_assert(offsetof(faad_stream_info, format_known) == 32, "stream baseline");
_Static_assert(offsetof(faad_frame_info, degraded) == 29, "frame baseline");
_Static_assert(offsetof(faad_frame_info, decoder_delay) == 20, "frame padding");
_Static_assert(offsetof(faad_frame_info, reserved) == 30, "frame tail padding");
_Static_assert(offsetof(faad_stream_info, reserved) == 33, "stream tail padding");
#if UINTPTR_MAX == UINT64_MAX
_Static_assert(offsetof(faad_library_info, version) == 8, "64-bit pointer alignment");
_Static_assert(offsetof(faad_library_info, ps_supported) == 29, "64-bit library baseline");
#elif UINTPTR_MAX == UINT32_MAX
_Static_assert(offsetof(faad_library_info, version) == 4, "32-bit pointer alignment");
_Static_assert(offsetof(faad_library_info, ps_supported) == 17, "32-bit library baseline");
#else
#error Unsupported pointer width
#endif
