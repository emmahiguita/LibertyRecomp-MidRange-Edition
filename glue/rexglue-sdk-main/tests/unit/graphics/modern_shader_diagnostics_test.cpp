#include <catch2/catch_test_macros.hpp>
#include "graphics/gta4_native/modern_diagnostic_gate.h"
using rex::graphics::gta4_native::ModernDiagnosticGate;

TEST_CASE("Repeated resolve failures cannot hide a later sky failure", "[modern][diagnostics]") {
  ModernDiagnosticGate gate;
  uint64_t records=0;
  for(unsigned i=0;i<8192;++i)records+=gate.Observe("resolve:unproduced").report;
  CHECK(records<32);
  auto sky=gate.Observe("draw.pipeline:missing sky override");
  CHECK(sky.report); CHECK(sky.count==1); CHECK_FALSE(sky.overflow);
  CHECK(gate.total()==8193);
  CHECK(gate.entries().front().count==8192);
}
TEST_CASE("Failure signature overflow is visible and bounded", "[modern][diagnostics]") {
  ModernDiagnosticGate gate;
  for(unsigned i=0;i<256;++i)CHECK(gate.Observe(std::to_string(i)).report);
  CHECK(gate.Observe("extra").overflow);
  CHECK(gate.overflow()==1);
  CHECK(gate.entries().size()==256);
  CHECK(gate.Observe("0").count==2);
  gate.Reset();
  CHECK(gate.total()==0);CHECK(gate.overflow()==0);CHECK(gate.entries().empty());
  CHECK(gate.Observe("extra").report);
}
