#include "gpu_counters_ui.h"
#include "core/allocator.h"
#include "core/array.h"
#include "editor/profiler_ui.h"
#include "editor/text_filter.h"
#include "core/string.h"
#include "renderer/gpu/gpu.h"
#include "renderer/gpu/gpu_nvperf.h"
#include <imgui/imgui.h>
#include <stdio.h>
#include <string.h>

namespace Lumix {

namespace {

struct GPUCountersUI final : ProfilerUI::Tab {
	explicit GPUCountersUI(IAllocator& allocator) : m_matches(allocator), m_csv(allocator) {}

	const char* getName() const override { return "HW counters"; }

	void onGUI() override {

		using namespace gpu::nvperf;
		char status[512];
		getStatus(status, sizeof(status));

		if (!isCompiledIn()) {
			ImGui::TextWrapped("%s", status);
			return;
		}

		if (getState() == State::UNAVAILABLE) {
			ImGui::TextWrapped("Reads the hardware counters of the GPU (NVIDIA, through the Nsight Perf SDK) for every profile block of one frame.");
			ImGui::TextWrapped("The counters are used by one tool at a time, so Nsight, PIX or another profiler must not be attached. "
				"Counters need profiling permissions (admin rights or NVIDIA Control Panel / Developer / Manage GPU Performance Counters).");
			if (ImGui::Button("Enable")) init(gpu::getDX12Device());
			ImGui::TextWrapped("%s", status);
			return;
		}
		if (getState() == State::FAILED && getMetricCount() == 0) {
			// failed to initialize
			ImGui::TextWrapped("%s", status);
			if (ImGui::Button("Retry")) init(gpu::getDX12Device());
			return;
		}

		const State state = getState();
		const bool busy = state == State::REQUESTED || state == State::RECORDING;

		// the metric to record, it can be typed (any name the SDK knows, e.g. with another rollup) or picked from the list
		ImGui::SetNextItemWidth(-1);
		ImGui::InputTextWithHint("##metric", "metric, e.g. sm__throughput.avg.pct_of_peak_sustained_elapsed", m_metric, sizeof(m_metric));
		if (m_filter.gui("Filter", -1)) m_matches_dirty = true;
		if (m_matches_dirty) updateMatches();

		// metrics that pass the filter, a click fills the metric above
		if (ImGui::BeginChild("##metrics", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 8), ImGuiChildFlags_Borders)) {
			ImGuiListClipper clipper;
			clipper.Begin((int)m_matches.size());
			while (clipper.Step()) {
				for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
					const char* name = getMetricName(m_matches[i]);
					if (ImGui::Selectable(name, equalStrings(name, m_metric))) {
						copyString(Span(m_metric), name);
					}
				}
			}
		}
		ImGui::EndChild();
		ImGui::TextDisabled("%d of %d metrics", (int)m_matches.size(), (int)getMetricCount());

		ImGui::SetNextItemWidth(100);
		ImGui::SliderInt("Max block depth", &m_max_depth, 1, 10);
		ImGui::SetItemTooltip("Every nesting level of profile blocks needs one more frame to record.");

		ImGui::BeginDisabled(busy || m_metric[0] == 0);
		if (ImGui::Button("Record")) {
			copyString(Span(m_selected_metric), m_metric);
			request(m_metric, (u32)m_max_depth);
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (busy) {
			u32 done, total;
			getProgress(&done, &total);
			ImGui::Text("Recording %u / %u frames, the app stalls until it's done.", done, total);
		}
		else {
			ImGui::TextDisabled("Records the next frames, each with a different nesting level.");
		}

		if (state == State::FAILED) {
			ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", status);
		}

		resultsGui();
	}

	void resultsGui() {
		using namespace gpu::nvperf;
		const u32 count = getResultCount();
		if (count == 0) return;

		ImGui::Text("%s", m_selected_metric);
		ImGui::SameLine();
		if (ImGui::SmallButton("Copy")) {
			m_csv.clear();
			for (u32 i = 0; i < count; ++i) {
				char path[256];
				double value;
				if (!getResult(i, path, sizeof(path), &value)) continue;
				char line[320];
				const int len = snprintf(line, sizeof(line), "%s, %.17g\n", path, value);
				for (int j = 0; j < len && j < (int)sizeof(line) - 1; ++j) m_csv.push(line[j]);
			}
			m_csv.push(0);
			ImGui::SetClipboardText(m_csv.begin());
		}

		if (ImGui::BeginTable("##gpu_counters", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Borders, ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 16))) {
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Block", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 120);
			ImGui::TableHeadersRow();
			for (u32 i = 0; i < count; ++i) {
				char path[256];
				double value;
				if (!getResult(i, path, sizeof(path), &value)) continue;
				// "Frame/geom pass/cluster cull": indent by depth, show the leaf
				u32 depth = 0;
				const char* leaf = path;
				for (const char* c = path; *c; ++c) {
					if (*c == '/') {
						++depth;
						leaf = c + 1;
					}
				}
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::Indent(depth * 14.f);
				ImGui::TextUnformatted(leaf);
				ImGui::Unindent(depth * 14.f);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path);
				ImGui::TableNextColumn();
				ImGui::Text("%.4g", value);
			}
			ImGui::EndTable();
		}
	}

	void updateMatches() {
		m_matches_dirty = false;
		m_matches.clear();
		const u32 count = gpu::nvperf::getMetricCount();
		for (u32 i = 0; i < count; ++i) {
			if (!m_filter.isActive() || m_filter.pass(gpu::nvperf::getMetricName(i))) m_matches.push(i);
		}
	}

	char m_metric[128] = "sm__throughput.avg.pct_of_peak_sustained_elapsed";
	TextFilter m_filter;
	char m_selected_metric[128] = "";
	int m_max_depth = 5;
	Array<u32> m_matches;
	Array<char> m_csv;
	bool m_matches_dirty = true;
};

GPUCountersUI* g_ui = nullptr;

} // namespace

void registerGPUCountersUI(StudioApp& app) {
	if (g_ui) return;
	IAllocator& allocator = app.getAllocator();
	g_ui = LUMIX_NEW(allocator, GPUCountersUI)(allocator);
	app.getProfilerUI().addTab(*g_ui);
}

void unregisterGPUCountersUI(StudioApp& app) {
	if (!g_ui) return;
	app.getProfilerUI().removeTab(*g_ui);
	app.getAllocator().deleteObject(g_ui);
	g_ui = nullptr;
}

} // namespace Lumix
