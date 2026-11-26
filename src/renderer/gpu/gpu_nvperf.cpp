#include "gpu_nvperf.h"

#if defined(LUMIX_NVPERF) && defined(_WIN32)

// Everything from the SDK stays in this file. The SDK API itself is built on std containers (ByteVector below), nothing else here is.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOCRYPT
#pragma warning(push)
#pragma warning(disable: 4265 4623 4625 4626 5026 5027 4668 4710 4711 4820 5039)
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <nvperf_host_impl.h>
#include "NvPerfInit.h"
#include "NvPerfCounterConfiguration.h"
#include "NvPerfCounterData.h"
#include "NvPerfD3D12.h"
#include "NvPerfDeviceProperties.h"
#include "NvPerfMetricsConfigBuilder.h"
#include "NvPerfMetricsEvaluator.h"
#include "NvPerfRangeProfilerD3D12.h"
#pragma warning(pop)
#include <stdio.h>
#include <string.h>

// after the SDK, its STL headers do not compile when some of these come first
#include "core/allocator.h"
#include "core/array.h"
#include "core/atomic.h"
#include "core/sort.h"
#include "core/string.h"
#include "core/sync.h"

namespace Lumix::gpu::nvperf {

static constexpr size_t MAX_RANGES = 2048;	// per pass, profile blocks of one frame (every shadow cascade, pass, ...)
static constexpr DWORD WAIT_GPU_MS = 10000;

using ByteVector = decltype(nv::perf::CounterConfiguration::configImage);	// the SDK wants this type for its buffers

struct Result {
	char path[256];
	double value;
};

struct Module {
	explicit Module(IAllocator& allocator)
		: metric_names(allocator)
		, metric_offsets(allocator)
		, results(allocator)
	{}

	Mutex mutex;						// guards state changes, status, results, the request and the progress
	AtomicI32 state = (i32)State::UNAVAILABLE;
	char status[512] = "Nsight Perf SDK: not initialized";

	bool initialized = false;
	bool log_callback_set = false;
	size_t device_index = ~size_t(0);
	char chip_name[64] = {};
	nv::perf::MetricsEvaluator evaluator;
	Array<char> metric_names;			// "name.rollup\0name.rollup\0..." sorted by offsets
	Array<u32> metric_offsets;

	// request
	char metric[160] = {};
	u32 max_depth = 1;

	// capture
	nv::perf::profiler::RangeProfilerD3D12 profiler;
	nv::perf::CounterConfiguration config;
	NVPW_MetricEvalRequest eval_request = {};
	bool in_session = false;
	bool in_pass = false;
	u32 passes_done = 0;
	u32 passes_total = 0;
	Microsoft::WRL::ComPtr<ID3D12Fence> fence;
	u64 fence_value = 0;
	HANDLE fence_event = nullptr;

	Array<Result> results;
};

static Module* g = nullptr;
static char s_last_log[512];	// last message of the SDK

static void logCallback(const char*, const char*, const char*, const char*, const char* message, void*) {
	copyString(Span(s_last_log), message);
	// drop the line break
	for (u32 i = (u32)stringLength(s_last_log); i > 0 && (s_last_log[i - 1] == '\n' || s_last_log[i - 1] == '\r'); --i) s_last_log[i - 1] = 0;
}

// message + the last error of the SDK, if there is one
static void setStatusLocked(const char* message, bool with_log) {
	if (with_log && s_last_log[0]) {
		StaticString<512> text(message, ": ", s_last_log);
		copyString(Span(g->status), text);
	}
	else {
		copyString(Span(g->status), message);
	}
}

static void fail(const char* message) {
	MutexGuard lock(g->mutex);
	setStatusLocked(message, true);
	g->state = (i32)State::FAILED;
}

static void endSession() {
	if (g->in_session) {
		g->profiler.EndSession();
		g->in_session = false;
	}
	g->in_pass = false;
}

bool isCompiledIn() { return true; }

State getState() {
	if (!g) return State::UNAVAILABLE;
	return (State)(i32)g->state;
}

void getStatus(char* out, u32 size) {
	if (size == 0) return;
	if (!g) {
		copyString(Span(out, size), "Nsight Perf SDK is built in, not initialized.");
		return;
	}
	MutexGuard lock(g->mutex);
	copyString(Span(out, size), g->status);
}

static void addMetric(const char* name, const char* suffix) {
	g->metric_offsets.push(g->metric_names.size());
	for (const char* c = name; *c; ++c) g->metric_names.push(*c);
	for (const char* c = suffix; *c; ++c) g->metric_names.push(*c);
	g->metric_names.push(0);
}

bool init(void* device_ptr) {
	if (!g) {
		IAllocator& allocator = getGlobalAllocator();
		g = LUMIX_NEW(allocator, Module)(allocator);
	}
	if (g->initialized) return true;

	MutexGuard lock(g->mutex);
	ID3D12Device* device = (ID3D12Device*)device_ptr;
	s_last_log[0] = 0;

	auto failLocked = [&](const char* message) {
		setStatusLocked(message, true);
		g->state = (i32)State::FAILED;
		return false;
	};

	if (!device) return failLocked("Nsight Perf SDK: no D3D12 device");

	if (!g->log_callback_set) {
		g->log_callback_set = true;
		nv::perf::UserLogEnableCustom(&logCallback, nullptr);
	}

	if (!nv::perf::InitializeNvPerf()) return failLocked("Nsight Perf SDK: failed to load nvperf_grfx_host.dll (it must be next to the executable)");
	if (!nv::perf::D3D12LoadDriver()) return failLocked("Nsight Perf SDK: failed to load the driver");
	if (!nv::perf::D3D12IsNvidiaDevice(device)) return failLocked("Nsight Perf SDK: the GPU is not an NVIDIA GPU");
	if (!nv::perf::profiler::D3D12IsGpuSupported(device)) return failLocked("Nsight Perf SDK: the GPU is not supported");

	g->device_index = nv::perf::D3D12GetNvperfDeviceIndex(device);
	if (g->device_index == ~size_t(0)) return failLocked("Nsight Perf SDK: failed to get the device index");

	const nv::perf::DeviceIdentifiers ids = nv::perf::GetDeviceIdentifiers(g->device_index);
	if (!ids.pChipName) return failLocked("Nsight Perf SDK: unrecognized GPU");
	copyString(Span(g->chip_name), ids.pChipName);

	ByteVector scratch;
	const size_t scratch_size = nv::perf::D3D12CalculateMetricsEvaluatorScratchBufferSize(g->chip_name);
	if (!scratch_size) return failLocked("Nsight Perf SDK: no metrics for this GPU");
	scratch.resize(scratch_size);
	NVPW_MetricsEvaluator* evaluator = nv::perf::D3D12CreateMetricsEvaluator(scratch.data(), scratch.size(), g->chip_name);
	if (!evaluator) return failLocked("Nsight Perf SDK: failed to create the metrics evaluator");
	g->evaluator = nv::perf::MetricsEvaluator(evaluator, static_cast<ByteVector&&>(scratch));

	// the default rollup is what makes a base name a valid metric
	g->metric_names.clear();
	g->metric_offsets.clear();
	for (const char* name : nv::perf::EnumerateCounters(g->evaluator)) addMetric(name, ".sum");
	for (const char* name : nv::perf::EnumerateRatios(g->evaluator)) addMetric(name, ".ratio");
	for (const char* name : nv::perf::EnumerateThroughputs(g->evaluator)) addMetric(name, ".avg.pct_of_peak_sustained_elapsed");
	const char* names = g->metric_names.begin();
	sort(g->metric_offsets.begin(), g->metric_offsets.end(), [names](u32 a, u32 b) { return strcmp(names + a, names + b) < 0; });

	if (!g->fence) {
		if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g->fence)))) return failLocked("Nsight Perf SDK: failed to create a fence");
		g->fence_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
	}

	g->initialized = true;
	g->state = (i32)State::IDLE;
	StaticString<128> ready("Nsight Perf SDK: ready (", g->chip_name, ")");
	copyString(Span(g->status), ready);
	return true;
}

void shutdown() {
	if (!g) return;
	endSession();
	if (g->fence_event) CloseHandle(g->fence_event);
	IAllocator& allocator = getGlobalAllocator();
	LUMIX_DELETE(allocator, g);
	g = nullptr;
}

u32 getMetricCount() {
	return g ? g->metric_offsets.size() : 0;
}

const char* getMetricName(u32 index) {
	// the list does not change after init
	if (!g || index >= (u32)g->metric_offsets.size()) return "";
	return g->metric_names.begin() + g->metric_offsets[index];
}

bool request(const char* metric, u32 max_depth) {
	if (!g) return false;
	MutexGuard lock(g->mutex);
	const State state = (State)(i32)g->state;
	if (!g->initialized || (state != State::IDLE && state != State::DONE && state != State::FAILED)) return false;
	copyString(Span(g->metric), metric);
	g->max_depth = max_depth < 1 ? 1 : max_depth;
	g->results.clear();
	g->passes_done = 0;
	g->passes_total = 0;
	s_last_log[0] = 0;
	setStatusLocked("Nsight Perf SDK: waiting for the next frame", false);
	g->state = (i32)State::REQUESTED;
	return true;
}

void getProgress(u32* done, u32* total) {
	*done = 0;
	*total = 0;
	if (!g) return;
	MutexGuard lock(g->mutex);
	*done = g->passes_done;
	*total = g->passes_total;
}

u32 getResultCount() {
	if (!g) return 0;
	MutexGuard lock(g->mutex);
	return g->results.size();
}

bool getResult(u32 index, char* path, u32 path_size, double* value) {
	if (!g || path_size == 0) return false;
	MutexGuard lock(g->mutex);
	if (index >= (u32)g->results.size()) return false;
	copyString(Span(path, path_size), g->results[index].path);
	*value = g->results[index].value;
	return true;
}

bool isRecordingFrame() { return g && (State)(i32)g->state == State::RECORDING; }

void frameBegin(void* queue_ptr) {
	if (!g || (State)(i32)g->state != State::REQUESTED) return;
	ID3D12CommandQueue* queue = (ID3D12CommandQueue*)queue_ptr;
	char metric[160];
	u32 max_depth;
	{
		MutexGuard lock(g->mutex);
		copyString(Span(metric), g->metric);
		max_depth = g->max_depth;
		s_last_log[0] = 0;
	}

	if (!nv::perf::ToMetricEvalRequest(g->evaluator, metric, g->eval_request)) {
		fail("Nsight Perf SDK: unknown metric");
		return;
	}

	nv::perf::MetricsConfigBuilder builder;
	NVPW_RawCounterConfig* raw_config = nv::perf::profiler::D3D12CreateRawCounterConfig(g->chip_name);
	if (!raw_config || !builder.Initialize(g->evaluator, raw_config, g->chip_name)) {
		fail("Nsight Perf SDK: failed to create the counter configuration");
		return;
	}
	if (!builder.AddMetrics(&g->eval_request, 1, true)) {
		fail("Nsight Perf SDK: this metric can't be collected");
		return;
	}
	g->config = nv::perf::CounterConfiguration();
	if (!nv::perf::CreateConfiguration(builder, g->config)) {
		fail("Nsight Perf SDK: failed to schedule this metric");
		return;
	}

	nv::perf::profiler::SessionOptions options;
	options.maxNumRanges = MAX_RANGES;
	if (!g->profiler.BeginSession(queue, options)) {
		fail("Nsight Perf SDK: failed to begin a session");
		return;
	}
	g->in_session = true;

	// the Frame range of the queue is level 1, profile blocks are the levels below it
	const uint16_t nesting_levels = (uint16_t)(max_depth + 1);
	if (!g->profiler.EnqueueCounterCollection(g->config, nesting_levels)) {
		endSession();
		fail("Nsight Perf SDK: failed to enqueue the collection");
		return;
	}

	MutexGuard lock(g->mutex);
	g->passes_done = 0;
	g->passes_total = (u32)(g->config.numPasses * nesting_levels);
	setStatusLocked("Nsight Perf SDK: recording", false);
	g->state = (i32)State::RECORDING;
}

void pushRange(void* list, const char* name) {
	nv::perf::profiler::D3D12PushRange((ID3D12GraphicsCommandList*)list, name);
}

void popRange(void* list) {
	nv::perf::profiler::D3D12PopRange((ID3D12GraphicsCommandList*)list);
}

void beforeSubmit(void*) {
	if (!g || (State)(i32)g->state != State::RECORDING) return;
	if (g->profiler.AllPassesSubmitted()) return;
	if (!g->profiler.BeginPass() || !g->profiler.PushRange("Frame")) {
		endSession();
		fail("Nsight Perf SDK: failed to begin a pass");
		return;
	}
	g->in_pass = g->profiler.IsInPass();
}

static void waitForGPU(ID3D12CommandQueue* queue) {
	++g->fence_value;
	queue->Signal(g->fence.Get(), g->fence_value);
	if (g->fence->GetCompletedValue() < g->fence_value) {
		g->fence->SetEventOnCompletion(g->fence_value, g->fence_event);
		WaitForSingleObject(g->fence_event, WAIT_GPU_MS);
	}
}

static void evaluate(const ByteVector& image) {
	if (!nv::perf::MetricsEvaluatorSetDeviceAttributes(g->evaluator, image.data(), image.size())) {
		fail("Nsight Perf SDK: failed to set device attributes");
		return;
	}
	const size_t num_ranges = nv::perf::CounterDataGetNumRanges(image.data());
	Array<Result> results(getGlobalAllocator());
	for (size_t i = 0; i < num_ranges; ++i) {
		Result& result = results.emplace();
		result.value = 0;
		if (!nv::perf::EvaluateToGpuValues(g->evaluator, image.data(), image.size(), i, 1, &g->eval_request, &result.value)) {
			fail("Nsight Perf SDK: failed to evaluate the counters");
			return;
		}
		copyString(Span(result.path), nv::perf::profiler::CounterDataGetRangeName(image.data(), i, '/').c_str());
	}

	MutexGuard lock(g->mutex);
	g->results = static_cast<Array<Result>&&>(results);
	setStatusLocked("Nsight Perf SDK: done", false);
	g->state = (i32)State::DONE;
}

void afterSubmit(void* queue_ptr) {
	if (!g || !g->in_pass) return;
	ID3D12CommandQueue* queue = (ID3D12CommandQueue*)queue_ptr;
	g->in_pass = false;

	if (!g->profiler.PopRange() || !g->profiler.EndPass()) {
		endSession();
		fail("Nsight Perf SDK: failed to end a pass");
		return;
	}

	// the SDK limits how many passes can be in flight, and the counters can only be decoded when the GPU is done with them
	waitForGPU(queue);

	nv::perf::profiler::DecodeResult decoded = {};
	if (!g->profiler.DecodeCounters(decoded)) {
		endSession();
		fail("Nsight Perf SDK: failed to decode the counters");
		return;
	}
	{
		MutexGuard lock(g->mutex);
		if (decoded.onePassDecoded && g->passes_done < g->passes_total) ++g->passes_done;
	}
	if (decoded.allStatisticalSamplesCollected) {
		evaluate(decoded.counterDataImage);
		endSession();
	}
}

} // namespace Lumix::gpu::nvperf

#else // no SDK

namespace Lumix::gpu::nvperf {

bool isCompiledIn() { return false; }
State getState() { return State::UNAVAILABLE; }
void getStatus(char* out, u32 size) {
	if (size == 0) return;
	const char* msg = "Nsight Perf SDK is not built in: put it into external/nsight_perf (NVIDIA GPUs, Windows, DX12) and regenerate the solution.";
	u32 i = 0;
	for (; msg[i] && i + 1 < size; ++i) out[i] = msg[i];
	out[i] = 0;
}
bool init(void*) { return false; }
void shutdown() {}
u32 getMetricCount() { return 0; }
const char* getMetricName(u32) { return ""; }
bool request(const char*, u32) { return false; }
void getProgress(u32* done, u32* total) { *done = 0; *total = 0; }
u32 getResultCount() { return 0; }
bool getResult(u32, char*, u32, double*) { return false; }
bool isRecordingFrame() { return false; }
void frameBegin(void*) {}
void pushRange(void*, const char*) {}
void popRange(void*) {}
void beforeSubmit(void*) {}
void afterSubmit(void*) {}

} // namespace Lumix::gpu::nvperf

#endif
