#pragma once

#include "core/core.h"

// Hardware counters of NVIDIA GPUs through the Nsight Perf SDK (Range Profiler), DX12 only.
// The SDK is optional: put it into external/nsight_perf and regenerate the solution (LUMIX_NVPERF is defined then),
// otherwise everything here is a stub and getState() returns UNAVAILABLE. The SDK is not part of the repository, see its license.
//
// Flow: the profiler UI calls request(metric), the next frames are recorded with a range for every profile block (gpu::pushDebugGroup),
// the SDK replays them in as many frames as it needs (passes of the counter * nesting levels), then the values of all ranges are
// available in getResult(). The capture stalls the CPU on the GPU a few frames. The counters are used by one tool at a time, so the session
// only lives during a capture.

namespace Lumix::gpu::nvperf {

enum class State : u32 {
	UNAVAILABLE,	// SDK not built in, not an NVIDIA GPU, ... see getStatus()
	IDLE,
	REQUESTED,		// waiting for the next frame
	RECORDING,
	DONE,			// results are available
	FAILED			// see getStatus()
};

// can be called from any thread
bool isCompiledIn();
State getState();
void getStatus(char* out, u32 size);						// last message of the SDK or of this module
bool init(void* d3d12_device);								// loads the SDK, builds the list of metrics, idempotent
void shutdown();											// render thread, at the end of the GPU backend
u32 getMetricCount();
const char* getMetricName(u32 index);						// with rollup, e.g. "sm__throughput.avg.pct_of_peak_sustained_elapsed"
bool request(const char* metric, u32 max_depth);			// max_depth: how deeply are profile blocks nested at most
void getProgress(u32* done, u32* total);					// passes (frames) of the running capture
u32 getResultCount();
bool getResult(u32 index, char* path, u32 path_size, double* value);	// path of a range, e.g. "Frame/geom pass/cluster cull"

// render thread only
bool isRecordingFrame();									// the command list of this frame gets ranges
void frameBegin(void* d3d12_command_queue);					// after the command list of the new frame was reset
void pushRange(void* d3d12_command_list, const char* name);
void popRange(void* d3d12_command_list);
void beforeSubmit(void* d3d12_command_queue);				// around ExecuteCommandLists of the frame
void afterSubmit(void* d3d12_command_queue);

} // namespace Lumix::gpu::nvperf
