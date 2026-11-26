#pragma once

namespace Lumix {

struct StudioApp;

// "GPU counters" section of the GPU tab of the profiler (NVIDIA hardware counters through the Nsight Perf SDK, see gpu/gpu_nvperf.h)
void registerGPUCountersUI(StudioApp& app);
void unregisterGPUCountersUI(StudioApp& app);

} // namespace Lumix
