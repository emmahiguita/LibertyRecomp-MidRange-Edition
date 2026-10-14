#pragma once

#include <cstdint>
#include <string>
#include <rex/cvar.h>

// One registry owner in rexruntime; renderer plugins only consume these values.
REXCVAR_DECLARE(bool, gta4_native_vector_fonts);
REXCVAR_DECLARE(bool, gta4_trace_vector_fonts);
REXCVAR_DECLARE(bool, gta4_native_spatial_aa);
REXCVAR_DECLARE(bool, gta4_native_output_dither);
REXCVAR_DECLARE(bool, gta4_native_hdr_high_precision);
REXCVAR_DECLARE(std::string, gta4_texture_filtering);
REXCVAR_DECLARE(std::string, gta4_anisotropic_filtering);
REXCVAR_DECLARE(std::string, gta4_native_msaa);
REXCVAR_DECLARE(std::string, gta4_native_light_overrides);
REXCVAR_DECLARE(bool, gta4_native_host_sun_shafts);
REXCVAR_DECLARE(bool, gta4_native_host_fog);
REXCVAR_DECLARE(uint32_t, gta4_native_frames_in_flight);
