#pragma once
#include "native_gpu_scope_summary.h"
#include <fmt/format.h>

namespace rex::graphics::gta4_native {
// The callback consumes each string synchronously. No names or GPU objects are
// retained here. This format is consumed by analyze_native_gpu_scope_labels.py.
template <typename Emit>
void EmitNativeGpuScopeLabels(const NativeGpuScopeSummary& summary, Emit&& emit) {
  if (!summary.active()) return;
    const auto totals = fmt::format(
        "GTA4/scope-summary v=1 frame={} scope={} first_cmd={} entries={} draws={} "
        "vertices={} indices={} clears={} overflow={}",
        summary.frame(), summary.serial(), summary.first_command(), summary.entries().size(),
        summary.draws(), summary.vertices(), summary.indices(), summary.clears(),
        summary.overflow_draws());
    emit(totals);
    for (const auto& entry : summary.entries()) {
      const auto& key = entry.key;
      const auto label = fmt::format(
          "GTA4/scope-member v=1 frame={} scope={} retail={}:{} source={} semantic={} "
          "guest_vs={:016X}:{} guest_ps={:016X}:{} draws={} vertices={} indices={} "
          "pipeline={:X} mixed_pipelines={} first_list={} last_list={} "
          "selected_vs={:016X}:{} selected_ps={:016X}:{} variant={:X} samples={}",
          summary.frame(), summary.serial(), key.retail_phase,
          RetailGpuPassName(key.retail_phase), GpuPassOriginSourceName(key.source),
          key.semantic_phase, key.vertex_shader, entry.vertex_name,
          key.pixel_shader, entry.pixel_name, entry.draws, entry.vertices, entry.indices,
          entry.first_pipeline, entry.mixed_pipelines ? 1 : 0,
          entry.first_list_scope, entry.last_list_scope,
          key.vertex_shader, entry.selected_vertex_name, key.pixel_shader, entry.selected_pixel_name,
          key.shader_variant, key.samples);
      emit(label);
    }
}
}  // namespace rex::graphics::gta4_native
