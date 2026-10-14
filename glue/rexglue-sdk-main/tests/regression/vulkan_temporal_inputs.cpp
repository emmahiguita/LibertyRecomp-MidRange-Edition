#include "../../src/graphics/gta4_native/temporal/camera_math.h"
#include "../../src/graphics/gta4_native/temporal/command_capture.h"
#include "../../src/graphics/gta4_native/temporal/sampling.h"

#include <cassert>
#include <limits>
#include <thread>

using namespace rex::graphics::gta4_native;
using namespace rex::graphics::gta4_native::temporal;
int main() {
  // Expectations calculated independently in Python/NumPy, including the
  // translation row, so a column-vector convention cannot pass accidentally.
  const CameraMatrix camera{0,1,0,0,-1,0,0,0,0,0,1,0,3,-2,5,1};
  const CameraMatrix expected{0,-1,0,0,1,0,0,0,0,0,1,0,2,3,-5,1};
  const auto inverse = InverseCameraMatrix(camera);
  assert(inverse);
  for (size_t i = 0; i < expected.size(); ++i) assert(std::abs((*inverse)[i] - expected[i]) < 1e-6f);
  const auto identity = MultiplyCameraMatrices(camera, *inverse);
  for (size_t row = 0; row < 4; ++row) for (size_t col = 0; col < 4; ++col)
    assert(std::abs(identity[row * 4 + col] - float(row == col)) < 1e-6f);
  assert(!InverseCameraMatrix(CameraMatrix{}));
  auto corrupt = camera; corrupt[0] = std::numeric_limits<float>::infinity();
  assert(!InverseCameraMatrix(corrupt));

  const Extent render{960,540}, output{1920,1080};
  const auto first = ReconstructionJitter(0, render, output);
  assert(first && std::abs((*first)[0]) < 1e-6f && std::abs((*first)[1] + 0.1666666667f) < 1e-6f);
  assert(ReconstructionJitter(32, render, output) == first);
  assert(ReconstructionJitter(31, render, output) != first);
  assert(ReconstructionJitter(UINT64_MAX, render, output));
  assert(!ReconstructionJitter(0, {}, output));
  assert(!ReconstructionJitter(0, output, render));
  Configuration configuration{render, output};
  assert(std::abs(ReconstructionMipBias(configuration, true) + 2.0f) < 1e-6f);
  assert(ReconstructionMipBias(configuration, false) == 0);
  configuration.render_extent = output;
  assert(ReconstructionMipBias(configuration, true) == 0);

  CommandCapture capture;
  TemporalCommand instance;
  instance.device = 1; instance.sequence = 7; instance.instance = 100;
  assert(ValidTemporalCommand(instance));
  capture.Observe(instance);
  const auto queued = capture.Capture(1);
  assert(queued && queued->instance == 100 && !capture.Capture(2));
  instance.instance = 200; capture.Observe(instance);
  assert(queued->instance == 100 && capture.Capture(1)->instance == 200);
  std::thread concurrent([&] {
    assert(!capture.Capture(1));
    TemporalCommand other = instance; other.instance = 300;
    capture.Observe(other);
    assert(capture.Capture(1)->instance == 300);
  });
  concurrent.join();
  assert(capture.Capture(1)->instance == 200);
  capture.Reset();
  assert(!capture.Capture(1) && queued->instance == 100);
  instance.flags = kTemporalFinalCompositeExecution;
  assert(!ValidTemporalCommand(instance));
  instance.flags = kTemporalExecutionCamera;
  assert(!ValidTemporalCommand(instance));
  instance.view = 1;
  assert(ValidTemporalCommand(instance));
}
