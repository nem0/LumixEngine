#include "composite_texture.h"
#include "core/crt.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "core/log.h"
#include "core/math.h"
#include "core/os.h"
#include "core/stream.h"
#include "core/string.h"
#include "editor/asset_browser.h"
#include "editor/editor_asset.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "editor/text_filter.h"
#include "renderer/draw_stream.h"
#include "renderer/texture.h"
#include "renderer/renderer.h"
#include "stb/stb_image.h"
#include <math.h>
#include <stb/stb_image_resize2.h>

namespace Lumix {

enum class CompositeTexture::NodeType : u32 {
	OUTPUT,
	INPUT,
	INVERT,
	COLOR,
	SPLIT,
	MERGE,
	FLIP,
	GAMMA,
	CONTRAST,
	BRIGHTNESS,
	GRAYSCALE,
	MULTIPLY,
	MIX,
	GRADIENT,
	VALUE_NOISE,
	CONSTANT,
	RESIZE,
	CIRCLE,
	CELLULAR_NOISE,
	SPLAT,
	GRADIENT_NOISE,
	WAVE_NOISE,
	CURVE,
	SET_ALPHA,
	CROP,
	SHARPEN,
	STATIC_SWITCH,
	STEP,
	SPLATTER,
	GRADIENT_MAP,
	TRANSLATE,
	CIRCULAR_SPLATTER,
	PIXEL_PROCESSOR,
	PIXEL_COLOR,
	PIXEL_X,
	PIXEL_Y,
	PIXEL_CTX_W,
	PIXEL_CTX_H,
	DIVIDE,
	MIN,
	MAX,
	SQUARE,
	TRIANGLE,
	BLUR,
	CHECKERBOARD,
	WARP,
	TWIRL,
	NORMALMAP
};

enum { OUTPUT_FLAG = 1 << 31 };

CompositeTexture::Image::Image(IAllocator& allocator)
	: pixels(allocator)
{}

CompositeTexture::Image::Image(u32 w, u32 h, u32 channels, IAllocator& allocator)
	: w(w)
	, h(h)
	, channels(channels)
	, pixels(allocator)
{
	pixels.resize(w * h * channels);
}

void CompositeTexture::Image::init(u32 _w, u32 _h, u32 _channels) {
	w = _w;
	h = _h;
	channels = _channels;
	pixels.resize(w * h * channels);
}


Vec4 CompositeTexture::Image::sample(i32 x, i32 y) {
	x = clamp(x, 0, w - 1);
	y = clamp(y, 0, h - 1);
	Vec4 res;
	memcpy(&res, &pixels[(x + y * w) * channels], channels * sizeof(float));
	return res;
}

Vec4 CompositeTexture::Image::sampleWrap(i32 x, i32 y) {
	x = x < 0 ? -i32(-x % w) + w : x % w;
	y = y < 0 ? -i32(-y % h) + h : y % h;
	Vec4 res;
	memcpy(&res, &pixels[(x + y * w) * channels], channels * sizeof(float));
	return res;
}

Vec4 CompositeTexture::Image::sampleWrap(float x, float y) {
	const i32 ix = i32(x);
	const i32 iy = i32(y);
	const float tx = x - ix;
	const float ty = y - iy;
	Vec4 v00 = sampleWrap(ix, iy);
	Vec4 v10 = sampleWrap(ix + 1, iy);
	Vec4 v01 = sampleWrap(ix, iy + 1);
	Vec4 v11 = sampleWrap(ix + 1, iy + 1);
	return lerp(
		lerp(v00, v10, tx),
		lerp(v01, v11, tx),
		ty);
}

Vec4 CompositeTexture::Image::sample(float x, float y) {
	const i32 ix = i32(x);
	const i32 iy = i32(y);
	const float tx = x - ix;
	const float ty = y - iy;
	Vec4 v00 = sample(ix, iy);
	Vec4 v10 = sample(ix + 1, iy);
	Vec4 v01 = sample(ix, iy + 1);
	Vec4 v11 = sample(ix + 1, iy + 1);
	return lerp(
		lerp(v00, v10, tx),
		lerp(v01, v11, tx),
		ty);
}

void CompositeTexture::Image::setPixel(u32 x, u32 y, const Vec4& color) {
	ASSERT(x < w);
	ASSERT(y < h);
	memcpy(&pixels[(x + y * w) * channels], &color, sizeof(float) * channels);
}

OutputMemoryStream CompositeTexture::Image::asU8() const {
	OutputMemoryStream res(pixels.getAllocator());
	res.resize(w * h * channels);
	for (u32 i = 0; i < u32(w * h * channels); ++i) {
		res[i] = u8(clamp(pixels[i] * 255.f, 0.f, 255.f) + 0.5f);
	}
	return static_cast<OutputMemoryStream&&>(res);
}

void CompositeTexture::Node::inputSlot() {
	ImGuiEx::Pin(m_id | (m_input_counter << 16), true);
	++m_input_counter;
}

void CompositeTexture::Node::outputSlot() {
	ImGuiEx::Pin(m_id | (m_output_counter << 16) | OUTPUT_FLAG, false);
	++m_output_counter;
}

void CompositeTexture::Node::nodeTitle(const char* title) {
	ImGuiEx::BeginNodeTitleBar();
	ImGui::TextUnformatted(title);
	if (m_generate_duration >= 0) {
		ImGui::SameLine();
		ImGui::Text(" - %d ms", u32(m_generate_duration * 1000));
	}
	
	ImGuiEx::EndNodeTitleBar();
}

bool CompositeTexture::Node::generate() {
	if (!m_dirty && !m_outputs.empty()) return true;

	m_error = "";
	m_outputs.clear();
	os::Timer timer;
	bool res = generateInternal();
	m_generate_duration = timer.getTimeSinceStart();
	if (res) m_dirty = false;
	return res;
}

void CompositeTexture::Node::markDirty() {
	if (m_preview) {
		Renderer* renderer = (Renderer*)m_resource->m_app.getEngine().getSystemManager().getSystem("renderer");
		renderer->getEndFrameDrawStream().destroy(m_preview);
		m_preview = gpu::INVALID_TEXTURE;
	}
	m_dirty = true;
	for (const Link& link : m_resource->m_links) {
		if (link.getFromNode() != m_id) continue;
		CompositeTexture::Node* n = m_resource->getNodeByID(link.getToNode());
		n->markDirty();
	}
}

bool CompositeTexture::Node::nodeGUI() {
	m_input_counter = 0;
	m_output_counter = 0;
	ImGuiEx::BeginNode(m_id, m_pos, &m_selected);
	bool res = gui();

	if (m_error.length() > 0) {
		ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0xff, 0, 0, 0xff));
	}
	else if (!m_reachable) {
		ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetColorU32(ImGuiCol_TitleBg));
	}
	ImGuiEx::EndNode();
	if (m_error.length() > 0) {
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 p = ImGui::GetItemRectMax() - ImGui::GetStyle().FramePadding;
		dl->AddText(p, IM_COL32(0xff, 0, 0, 0xff), ICON_FA_EXCLAMATION_TRIANGLE);

		ImGui::PopStyleColor();
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", m_error.c_str());
	}
	else if (!m_reachable) {
		ImGui::PopStyleColor();
	}
	if (res) {
		markDirty();
 		generate();
	}
	return res;
}

struct CompositeTexture::Node::Input {
	CompositeTexture::Node* node;
	u32 output_idx;

	operator bool() const { return node; }

	CompositeTexture::ValueResult getValue(const CompositeTexture::PixelContext& ctx) const {
		CompositeTexture::ValueResult res = node->getValue(ctx);
		if (res.isValid()) node->m_error = "";
		return res;
	}
};

static void markReachable(CompositeTexture::Node* node, CompositeTexture& texture) {
	if (!node) return;
	node->m_reachable = true;

	for (const CompositeTexture::Link& link : texture.m_links) {
		if (link.getToNode() != node->m_id) continue;

		CompositeTexture::Node* from_node = texture.getNodeByID(link.getFromNode());
		markReachable(from_node, texture);
	}
}

static void markReachable(CompositeTexture& texture) {
	for (CompositeTexture::Node* n : texture.m_nodes) {
		n->m_reachable = false;
	}
	markReachable(texture.m_nodes[0], texture);
}

void CompositeTexture::deleteUnreachable() {
	markReachable(*this);
	for (i32 i = m_nodes.size() - 1; i > 0; --i) { // we really don't want to delete node 0 (output)
		Node* node = m_nodes[i];
		if (!node->m_reachable) {
			for (i32 j = m_links.size() - 1; j >= 0; --j) {
				if (m_links[j].getFromNode() == node->m_id || m_links[j].getToNode() == node->m_id) {
					m_links.erase(j);
				}
			}

			LUMIX_DELETE(m_allocator, node);
			m_nodes.swapAndPop(i);
		}
	}	
}


void CompositeTexture::deleteSelectedNodes() {
	for (i32 i = m_nodes.size() - 1; i > 0; --i) { // we really don't want to delete node 0 (output)
		Node* node = m_nodes[i];
		if (node->m_selected) {
			for (i32 j = m_links.size() - 1; j >= 0; --j) {
				if (m_links[j].getFromNode() == node->m_id || m_links[j].getToNode() == node->m_id) {
					m_links.erase(j);
				}
			}

			LUMIX_DELETE(m_allocator, node);
			m_nodes.swapAndPop(i);
		}
	}
}

CompositeTexture::Node* CompositeTexture::getNodeByID(u16 id) const {
	for (Node* node : m_nodes) {
		if (node->m_id == id) return node;
	}
	return nullptr;
}

CompositeTexture::ValueResult CompositeTexture::Node::getInputValue(u32 pin_idx, const CompositeTexture::PixelContext& ctx) {
	const Input input = getInput(pin_idx);
	if (!input) return errorValue("Missing input");
	return input.getValue(ctx);
}

bool CompositeTexture::Node::generateInput(u32 pin_idx) {
	const Input input = getInput(pin_idx);
	if (!input) return error("Invalid input");
	return input.node->generate();
}

CompositeTexture::Image& CompositeTexture::Node::getInputImage(u32 pin_idx) const {
	const Input input = getInput(pin_idx);
	ASSERT(input);
	return input.node->m_outputs[input.output_idx];
}

CompositeTexture::Node::Input CompositeTexture::Node::getInput(u32 pin_idx) const {
	for (const Link& link : m_resource->m_links) {
		if (link.getToNode() != m_id) continue;
		if (link.getToPin() != pin_idx) continue;

		Input res;
		res.output_idx = link.getFromPin();
		res.node = m_resource->getNodeByID(link.getFromNode());
		return res;
	}

	return {};
}

namespace {

struct SplitNode final : CompositeTexture::Node {
	SplitNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::SPLIT; }
	
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	bool generateInternal() override {
		m_outputs.clear();
		if (!generateInput(0)) return false;
		
		CompositeTexture::Image& input = getInputImage(0);

		for (u32 ch = 0; ch < input.channels; ++ch) {
			CompositeTexture::Image& o = m_outputs.emplace(input.w, input.h, 1, m_allocator);
			
			float* dst = o.pixels.begin();
			const float* src = input.pixels.begin();
			for (u32 i = 0; i < input.w * input.h; ++i) {
				dst[i] = src[i * input.channels + ch];
			}
		}
		return true;
	}

	bool gui() override {
		nodeTitle("Split");
		inputSlot();
		outputSlot(); ImGui::TextUnformatted("R");
		outputSlot(); ImGui::TextUnformatted("G");
		outputSlot(); ImGui::TextUnformatted("B");
		outputSlot(); ImGui::TextUnformatted("A");
		return false;
	}
};

struct MergeNode final : CompositeTexture::Node {
	MergeNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::MERGE; }
	
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	bool generateInternal() override {
		const Input inputs[] = { getInput(0), getInput(1), getInput(2), getInput(3) };
		u32 channels_count = 0;
		for (u32 i = 0; i < 4; ++i) {
			if (!inputs[i]) break;
			if (!generateInput(0)) return error("Invalid input");
			++channels_count;
		}
		
		for (u32 i = channels_count; i < 4; ++i) {
			if (inputs[i]) return error("Missing input");	
		}
		if (channels_count == 0) return error("Missing inputs");
		
		const CompositeTexture::Image& r = getInputImage(0);
		if (r.channels != 1) return error("Input must have only one channel");

		CompositeTexture::Image& out = m_outputs.emplace(r.w, r.h, channels_count, m_allocator);

		float* dst = out.pixels.data();
		const float* first_src = r.pixels.data();
		for (u32 i = 0; i < r.w * r.h; ++i) {
			dst[i * channels_count] = first_src[i];
		}

		for (u32 i = 1; i < channels_count; ++i) {
			const CompositeTexture::Image& p = getInputImage(i);
			if (p.channels != 1) return error("Input must have only one channel");
			if (p.w != r.w || p.h != r.h) return error("Inputs must have matching sizes");
			
			const float* src = p.pixels.data();
			for (u32 j = 0; j < p.w * p.h; ++j) {
				dst[j * channels_count + i] = src[j];
			}
		}

		return true;
	}

	bool gui() override {
		nodeTitle("Merge");
		outputSlot();
		inputSlot(); ImGui::TextUnformatted("R");
		inputSlot(); ImGui::TextUnformatted("G");
		inputSlot(); ImGui::TextUnformatted("B");
		inputSlot(); ImGui::TextUnformatted("A");
		return false;
	}
};

struct ConstantNode final : CompositeTexture::Node {
	ConstantNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::CONSTANT; }

	bool hasInputPins() const override { return false; }
	bool hasOutputPins() const override { return true; }

	void serialize(OutputMemoryStream& blob) const override {
		blob.write(value);
	}
	
	void deserialize(InputMemoryStream& blob) override {
		blob.read(value);
	}
	
	CompositeTexture::ValueResult getValue(const CompositeTexture::PixelContext& ctx) override { return value; }
	bool generateInternal() override { return error("Invalid context"); }

	bool gui() override {
		nodeTitle("Constant");
		outputSlot();
		return ImGui::DragFloat("Value", &value, 0.01f, -FLT_MAX, FLT_MAX);
	}

	float value = 1.f;
};

struct ColorNode final : CompositeTexture::Node {
	ColorNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::COLOR; }

	bool hasInputPins() const override { return false; }
	bool hasOutputPins() const override { return true; }

	void serialize(OutputMemoryStream& blob) const override {
		blob.write(color);
	}
	
	void deserialize(InputMemoryStream& blob) override {
		blob.read(color);
	}
	
	CompositeTexture::ValueResult getValue(const CompositeTexture::PixelContext& ctx) override {
		return color;
	}

	bool generateInternal() override {
		CompositeTexture::Image& out = m_outputs.emplace(4, 4, 4, m_allocator);
		for (u32 i = 0; i < 16; ++i) {
			memcpy(&out.pixels[i * 4], &color, sizeof(color));
		}
		return true;
	}

	bool gui() override {
		nodeTitle("Color");
		outputSlot();
		return ImGui::ColorPicker4("##color", &color.x);
	}

	Vec4 color = Vec4(1);
};

struct FlipNode final : CompositeTexture::Node {
	FlipNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::FLIP; }

	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }
	
	void serialize(OutputMemoryStream& blob) const override {
		blob.write(horizontal);
	}

	void deserialize(InputMemoryStream& blob) override {
		blob.read(horizontal);
	}
	
	bool generateInternal() override {
		if (!generateInput(0)) return false;

		CompositeTexture::Image& in = getInputImage(0);
		CompositeTexture::Image& out = m_outputs.emplace(in.w, in.h, in.channels, m_allocator);

		const float* src = in.pixels.data();
		float* dst = out.pixels.data();
		if (horizontal) {
			for (u32 j = 0; j < in.h; ++j) {
				for (u32 i = 0; i < in.w; ++i) {
					memcpy(&dst[(i + j * in.w) * in.channels], &src[(in.w - i - 1 + j * in.w) * in.channels], sizeof(float) * in.channels);
				}
			}
			return true;
		}

		for (u32 j = 0; j < in.h; ++j) {
			for (u32 i = 0; i < in.w; ++i) {
				memcpy(&dst[(i + j * in.w) * in.channels], &src[(i + (in.h - j - 1) * in.w) * in.channels], sizeof(float) * in.channels);
			}
		}
		return true;
	}
	
	bool gui() override {
		nodeTitle("Flip");
		inputSlot();
		bool res = ImGui::Checkbox("Horizontal", &horizontal);
		ImGui::SameLine();
		outputSlot();
		return res;
	}

	bool horizontal = false;
};

template <CompositeTexture::NodeType TYPE>
struct PixelNode final : CompositeTexture::Node {
	PixelNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return TYPE; }
	bool hasInputPins() const override { return false; }
	bool hasOutputPins() const override { return true; }
	void serialize(OutputMemoryStream& blob) const override {}
	void deserialize(InputMemoryStream& blob) override {}
	
	CompositeTexture::ValueResult getValue(const CompositeTexture::PixelContext& ctx) override {
		if constexpr (TYPE == CompositeTexture::NodeType::PIXEL_COLOR) return ctx.color;
		if constexpr (TYPE == CompositeTexture::NodeType::PIXEL_X) return (float)ctx.x;
		if constexpr (TYPE == CompositeTexture::NodeType::PIXEL_Y) return (float)ctx.y;
		if constexpr (TYPE == CompositeTexture::NodeType::PIXEL_CTX_W) return (float)ctx.image->w;
		if constexpr (TYPE == CompositeTexture::NodeType::PIXEL_CTX_H) return (float)ctx.image->h;
	}

	bool generateInternal() override { return error("Invalid context"); }

	bool gui() override {
		outputSlot();
		if constexpr (TYPE == CompositeTexture::NodeType::PIXEL_COLOR) ImGui::TextUnformatted("Pixel color");
		if constexpr (TYPE == CompositeTexture::NodeType::PIXEL_X) ImGui::TextUnformatted("Pixel X");
		if constexpr (TYPE == CompositeTexture::NodeType::PIXEL_Y) ImGui::TextUnformatted("Pixel Y");
		if constexpr (TYPE == CompositeTexture::NodeType::PIXEL_CTX_W) ImGui::TextUnformatted("Pixel context width");
		if constexpr (TYPE == CompositeTexture::NodeType::PIXEL_CTX_H) ImGui::TextUnformatted("Pixel context height");
		return false;
	}
};


struct PixelProcessorNode final : CompositeTexture::Node {
	PixelProcessorNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::PIXEL_PROCESSOR; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }
	void serialize(OutputMemoryStream& blob) const override {}
	void deserialize(InputMemoryStream& blob) override {}

	bool generateInternal() override {
		if (!generateInput(0)) return false;

		CompositeTexture::Node::Input i1 = getInput(1);
		if (!i1) return error("Invalid input");

		CompositeTexture::Image& in = getInputImage(0);
		CompositeTexture::Image& out = m_outputs.emplace(in.w, in.h, in.channels, m_allocator);

		CompositeTexture::PixelContext ctx;
		ctx.image = &in;
		for (u32 j = 0; j < in.h; ++j) {
			ctx.y = j;
			for (u32 i = 0; i < in.w; ++i) {
				ctx.x = i;
				ctx.color = Vec4(0);
				memcpy(&ctx.color, &in.pixels[(i + j * in.w) * in.channels], in.channels * sizeof(float));
				CompositeTexture::ValueResult r = i1.getValue(ctx);
				if (!r.isValid()) return error("Invalid pixel");
				
				memcpy(&out.pixels[(i + j * out.w) * out.channels], &r.value, out.channels * sizeof(float));
			}
		}
		return true;
	}

	bool gui() override {
		nodeTitle("Pixel processor");
		inputSlot();
		outputSlot();
		ImGui::TextUnformatted("Image");
		inputSlot();
		ImGui::TextUnformatted("Pixel");
		return false;
	}
};


struct RandomPixelsNode final : CompositeTexture::Node {
	RandomPixelsNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::VALUE_NOISE; }
	bool hasInputPins() const override { return false; }
	bool hasOutputPins() const override { return true; }

	void serialize(OutputMemoryStream& blob) const override {
		blob.write(w);
		blob.write(h);
		blob.write(seed);
	}

	void deserialize(InputMemoryStream& blob) override {
		blob.read(w);
		blob.read(h);
		blob.read(seed);
	}

	bool generateInternal() override {
		CompositeTexture::Image& out = m_outputs.emplace(w, h, 1, m_allocator); 
		
		RandomGenerator rng(seed);
		for (u32 i = 0; i < w * h; ++i) {
			out.pixels[i] = rng.randFloat();
		}
		return true;
	}

	bool gui() override {
		nodeTitle("Random pixels");
		outputSlot();
		bool res = ImGui::DragInt("Width", (i32*)&w, 1, 1, 9000);
		ImGui::DragInt("Height", (i32*)&h, 1, 1, 9000) || res;
		ImGui::DragInt("Seed", (i32*)&seed) || res;
		return res;
	}

	u32 w = 256;
	u32 h = 256;
	u32 seed = 521288629;
};

struct GradientNode final : CompositeTexture::Node {
	GradientNode(IAllocator& allocator) : Node(allocator) {}
	
	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::GRADIENT; }
	bool hasInputPins() const override { return false; }
	bool hasOutputPins() const override { return true; }

	void serialize(OutputMemoryStream& blob) const override {
		blob.write(size);
	}

	void deserialize(InputMemoryStream& blob) override {
		blob.read(size);
	}
	
	bool generateInternal() override {
		CompositeTexture::Image& out = m_outputs.emplace(size, 1, 1, m_allocator); 
		
		for (u32 i = 0; i < size; ++i) {
			out.pixels[i] = float(i) / (size - 1);
		}
		return true;
	}
	
	bool gui() override {
		nodeTitle("Gradient");
		bool res = ImGui::DragInt("Size (px)", (i32*)&size, 1, 2, 1024);
		ImGui::SameLine();
		outputSlot();
		return res;
	}

	u32 size = 256;
};

struct GammaNode final : CompositeTexture::Node {
	GammaNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::GAMMA; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	void serialize(OutputMemoryStream& blob) const override {
		blob.write(gamma);
	}

	void deserialize(InputMemoryStream& blob) override {
		blob.read(gamma);
	}

	CompositeTexture::ValueResult getValue(const CompositeTexture::PixelContext& ctx) override {
		CompositeTexture::ValueResult a = getInputValue(0, ctx);
		if (!a.isValid()) return errorValue("Error");

		for (u32 i = 0; i < a.channels; ++i) {
			a.value[i] = powf(a.value[i], 1 / gamma);
		}
		return a;
	}

	bool generateInternal() override {
		if (!generateInput(0)) return false;

		CompositeTexture::Image& in = getInputImage(0);
		CompositeTexture::Image& out = m_outputs.emplace(in.w, in.h, in.channels, m_allocator); 

		for (u32 i = 0, c = (u32)in.pixels.size(); i < c; ++i) {
			const bool is_alpha = in.channels == 4 && i % 4 == 3;
			if (is_alpha) {
				out.pixels[i] = in.pixels[i];
				continue;
			}

			out.pixels[i] = powf(in.pixels[i], 1 / gamma);
		}
		return true;
	}
	
	bool gui() override {
		nodeTitle("Gamma");
		inputSlot();
		ImGui::SetNextItemWidth(150);
		bool res = ImGui::DragFloat("##v", &gamma);
		ImGui::SameLine();
		outputSlot();
		return res;
	}

	float gamma = 2.2f;
};

template <CompositeTexture::NodeType TYPE>
struct MathNode final : CompositeTexture::Node {
	MathNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return TYPE; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	template <typename T>
	auto op(T a, T b) {
		if constexpr (TYPE == CompositeTexture::NodeType::DIVIDE) return a / b;
		if constexpr (TYPE == CompositeTexture::NodeType::MAX) return maximum(a, b);
		if constexpr (TYPE == CompositeTexture::NodeType::MIN) return minimum(a, b);
	}

	CompositeTexture::ValueResult getValue(const CompositeTexture::PixelContext& ctx) override {
		CompositeTexture::ValueResult a = getInputValue(0, ctx);
		if (!a.isValid()) return a;

		CompositeTexture::ValueResult b = getInputValue(1, ctx);
		if (!b.isValid()) return b;

		if (a.isFloat() && b.isFloat()) return op(a.value.x, b.value.x);
		ASSERT(a.channels == b.channels && a.channels == 4);
		return op(a.value, b.value);
	}

	bool generateInternal() override { return error("Invalid context"); }

	bool gui() override {
		if constexpr (TYPE == CompositeTexture::NodeType::DIVIDE) nodeTitle("Divide");
		if constexpr (TYPE == CompositeTexture::NodeType::MAX) nodeTitle("Max");
		if constexpr (TYPE == CompositeTexture::NodeType::MIN) nodeTitle("Min");
		
		ImGui::BeginGroup();
		inputSlot(); ImGui::TextUnformatted("A");
		inputSlot(); ImGui::TextUnformatted("B");
		ImGui::EndGroup();
		ImGui::SameLine();
		outputSlot();
		return false;
	}
};

struct MultiplyNode final : CompositeTexture::Node {
	MultiplyNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::MULTIPLY; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	CompositeTexture::ValueResult getValue(const CompositeTexture::PixelContext& ctx) override {
		CompositeTexture::ValueResult a = getInputValue(0, ctx);
		if (!a.isValid()) return errorValue("Invalid input");

		CompositeTexture::ValueResult b = getInputValue(1, ctx);
		if (!b.isValid()) return errorValue("Invalid input");

		if (a.isFloat() && b.isFloat()) return a.value.x * b.value.x;
		return a.value * b.value;
	}

	bool generateInternal() override {
		if (!generateInput(0)) return false;
		if (!generateInput(1)) return false;

		CompositeTexture::Image& in0 = getInputImage(0);
		CompositeTexture::Image& in1 = getInputImage(1);
		if (in0.channels != in1.channels) return error("Number of channel does not match");
		if (in0.w != in1.w) return error("Width does not match");
		if (in0.h != in1.h) return error("Height does not match");
		
		CompositeTexture::Image& out = m_outputs.emplace(in0.w, in0.h, in0.channels, m_allocator); 

		for (u32 i = 0, c = (u32)in0.pixels.size(); i < c; ++i) {
			out.pixels[i] = in0.pixels[i] * in1.pixels[i];
		}
		return true;
	}

	bool gui() override {
		nodeTitle("Multiply");
		ImGui::BeginGroup();
		inputSlot(); ImGui::TextUnformatted("A");
		inputSlot(); ImGui::TextUnformatted("B");
		ImGui::EndGroup();
		ImGui::SameLine();
		outputSlot();
		return false;
	}
};

struct ResizeNode final : CompositeTexture::Node {
	ResizeNode(IAllocator& allocator) : Node(allocator) {}
	
	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::RESIZE; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }
	
	void serialize(OutputMemoryStream& blob) const override {
		blob.write(type);
		blob.write(size);
		blob.write(scale);
	}

	void deserialize(InputMemoryStream& blob) override {
		blob.read(type);
		blob.read(size);
		blob.read(scale);
	}

	bool generateInternal() override {
		if (!generateInput(0)) return false;

		CompositeTexture::Image& in = getInputImage(0);
		
		const u32 w = type == Type::PIXELS ? size.x : u32(in.w * scale.x * 0.01f + 0.5f);
		const u32 h = type == Type::PIXELS ? size.y : u32(in.h * scale.y * 0.01f + 0.5f);
		CompositeTexture::Image& out = m_outputs.emplace(w, h, in.channels, m_allocator); 
		
		return nullptr != stbir_resize_float_linear(in.pixels.data(), in.w, in.h, 0, out.pixels.data(), w, h, 0, (stbir_pixel_layout)out.channels);
	}	

	bool gui() override {
		nodeTitle("Resize");
		inputSlot();
		ImGui::BeginGroup();
		bool res = ImGui::Combo("##type", (int*)&type, "Pixels\0Percent\0");
		switch (type) {
			case Type::PERCENT:
				res = ImGui::DragFloat("Width", &scale.x, 1, 0, FLT_MAX) || res;
				res = ImGui::DragFloat("Height", &scale.y, 1, 0, FLT_MAX) || res;
				break;
			case Type::PIXELS:
				res = ImGui::DragInt("Width", (i32*)&size.x, 1, 0, 999999) || res;
				res = ImGui::DragInt("Height", (i32*)&size.y, 1, 0, 999999) || res;
				break;
		}
		ImGui::EndGroup();
		ImGui::SameLine();
		outputSlot();
		return res;
	}

	enum class Type : int {
		PIXELS,
		PERCENT
	};

	Type type = Type::PIXELS;
	IVec2 size = IVec2(100, 100);
	Vec2 scale = Vec2(50.f);
};

static Vec2 floor(Vec2 p) { return { floorf(p.x), floorf(p.y) }; }
static Vec2 sin(Vec2 p) { return { sinf(p.x), sinf(p.y) }; }
static Vec2 fract(Vec2 p) { return p - floor(p); }
static Vec2 hash(Vec2 p) {
	p = Vec2(dot(p, Vec2(127.1f, 311.7f)), dot(p, Vec2(269.5f, 183.3f)));
	return fract(sin(p) * 18.5453f);
}

struct WaveNoiseNode final : CompositeTexture::Node {
	WaveNoiseNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::WAVE_NOISE; }

	bool hasInputPins() const override { return false; }
	bool hasOutputPins() const override { return true; }
	
	static float mix(float a, float b, float t) {
		return a * (1 - t) + b * t;
	}

	// https://www.shadertoy.com/view/tldSRj
	float noise(Vec2 p) const {
		Vec2 i = floor(p);
		Vec2 f = fract(p);
		f = f * f * (f * -2.f + 3.f);
		return mix(mix(sinf(dot(p, hash(i + Vec2(0.f, 0.f)))),
		               sinf(dot(p, hash(i + Vec2(1.f, 0.f)))), f.x),
		               mix(sinf(dot(p, hash(i + Vec2(0.f, 1.f)))),
		                   sinf(dot(p, hash(i + Vec2(1.f, 1.f)))), f.x), f.y);
	}

	bool gui() override {
		nodeTitle("Wave noise");
		outputSlot();
		bool res = ImGui::DragFloat("Scale", &scale, 0.01f, FLT_MIN, FLT_MAX);
		res = ImGui::DragFloat("Offset", &offset, 0.01f, FLT_MIN, FLT_MAX) || res;
		res = ImGui::DragInt("Width", (i32*)&w, 1, 1, 999999) || res;
		res = ImGui::DragInt("Height", (i32*)&h, 1, 1, 999999) || res;
		return res;
	}

	bool generateInternal() override {
		CompositeTexture::Image& out = m_outputs.emplace(w, h, 1, m_allocator); 

		for (u32 j = 0; j < h; ++j) {
			const float v = j / float(h - 1);
			for (u32 i = 0; i < w; ++i) {
				float u = i / float(w - 1);
				float d = noise(Vec2(u, v) * scale + offset) * 0.5f + 0.5f;
				out.pixels[i + j * w] = d;
			}
		}
		return true;
	}

	void serialize(OutputMemoryStream& blob) const override {
		blob.write(w);
		blob.write(h);
		blob.write(scale);
		blob.write(offset);
	}

	void deserialize(InputMemoryStream& blob) override {
		blob.read(w);
		blob.read(h);
		blob.read(scale);
		blob.read(offset);
	}

	u32 w = 256;
	u32 h = 256;
	float scale = 4;
	float offset = 0;
};

struct GradientNoiseNode final : CompositeTexture::Node {
	GradientNoiseNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::GRADIENT_NOISE; }

	bool hasInputPins() const override { return false; }
	bool hasOutputPins() const override { return true; }

	// https://github.com/tuxalin/procedural-tileable-shaders/blob/master/gradientNoise.glsl
	static Vec4 yyww(const Vec4& v) { return {v.y, v.y, v.w, v.w}; }
	static Vec4 xzxz(const Vec4& v) { return {v.x, v.z, v.x, v.z}; }
	static Vec4 xyxy(Vec2 v) { return {v.x, v.y, v.x, v.y}; }
	static Vec4 mod(const Vec4& a, const Vec4& b) {
		return {
			fmodf(a.x, b.x),
			fmodf(a.y, b.y),
			fmodf(a.z, b.z),
			fmodf(a.w, b.w)
		};
	}
	
	struct UVec4 {
		UVec4(u32 x, u32 y, u32 z, u32 w) : x(x), y(y), z(z), w(w) {}
		UVec4(const Vec4& rhs) : x((u32)rhs.x), y((u32)rhs.y), z((u32)rhs.z), w((u32)rhs.w) {}
		u32 x, y, z, w;
		UVec4 operator^(const UVec4& rhs) const { return {x ^ rhs.x, y ^ rhs.y, z ^ rhs.z, w ^ rhs.w}; }
		UVec4 operator<<(u32 v) const { return {x << v, y << v, z << v, w << v}; }
		UVec4 operator+(u32 v) const { return {x + v, y + v, z + v, w + v}; }
		UVec4 operator^(u32 v) const { return {x ^ v, y ^ v, z ^ v, w ^ v}; }
		UVec4 operator*(u32 v) const { return {x * v, y * v, z * v, w * v}; }
		UVec4 operator*(const UVec4& rhs) const { return {x * rhs.x, y * rhs.y, z * rhs.z, w * rhs.w}; }
		UVec4 operator+(const UVec4& rhs) const { return {x + rhs.x, y + rhs.y, z + rhs.z, w + rhs.w}; }

		UVec4 xzxz() const { return { x, z, x, z }; }
		UVec4 yyww() const { return { y, y, w, w }; }

		Vec4 asVec4() const { return { (float)x, (float)y, (float)z, (float)w }; }
	};

	static UVec4 ihash1D(UVec4 q) {
		// hash by Hugo Elias, Integer Hash - I, 2017
		q = q * 747796405u + 2891336453u;
		q = (q << 13u) ^ q;
		return q * (q * q * 15731u + 789221u) + 1376312589u;
	}

	static void multiHash2D(Vec4 cell, Vec4& hashX, Vec4& hashY) {
		UVec4 i = UVec4(cell);
		UVec4 hash0 = ihash1D(ihash1D(i.xzxz()) + i.yyww());
		UVec4 hash1 = ihash1D(hash0 ^ 1933247u);
		hashX = hash0.asVec4() * (1.0 / float(0xffFFffFF));
		hashY = hash1.asVec4() * (1.0 / float(0xffFFffFF));
	}

	static void smultiHash2D(Vec4 cell, Vec4& hashX, Vec4& hashY)
	{
		multiHash2D(cell, hashX, hashY);
		hashX = hashX * Vec4(2.f) - Vec4(1.f); 
		hashY = hashY * Vec4(2.f) - Vec4(1.f);
	}

	static Vec2 noiseInterpolate(Vec2 x) { 
		Vec2 x2 = x * x;
		return x2 * x * (x * (x * 6.0 - 15.0) + 10.0); 
	}

	float gradientNoise(Vec2 pos, Vec2 scale) {
		pos = pos * scale;
		Vec4 i = xyxy(floor(pos)) + Vec4(0, 0, 1, 1);
		Vec4 f = (xyxy(pos) - xyxy(i.xy())) - Vec4(0, 0, 1, 1);
		i = mod(i, xyxy(scale));

		Vec4 hashX, hashY;
		smultiHash2D(i, hashX, hashY);

		Vec4 gradients = hashX * xzxz(f) + hashY * yyww(f);
		Vec2 u = noiseInterpolate(f.xy());
		Vec2 g = lerp(gradients.xz(), gradients.yw(), u.x);
		return (1.4142135623730950f * lerp(g.x, g.y, u.y)) * 0.5f + 0.5f;
	}

	bool gui() override {
		nodeTitle("Gradient noise");
		outputSlot();
		bool res = ImGui::DragFloat("Scale", &scale, 0.01f, FLT_MIN, FLT_MAX);
		res = ImGui::DragInt("Width", (i32*)&w, 1, 1, 999999) || res;
		res = ImGui::DragInt("Height", (i32*)&h, 1, 1, 999999) || res;
		return res;
	}

	bool generateInternal() override {
		CompositeTexture::Image& out = m_outputs.emplace(w, h, 1, m_allocator); 

		for (u32 j = 0; j < h; ++j) {
			const float v = j / float(h);
			for (u32 i = 0; i < w; ++i) {
				float u = i / float(w);
				float d = gradientNoise(Vec2(u, v), Vec2(scale));
				out.pixels[i + j * w] = d;
			}
		}
		return true;
	}

	void serialize(OutputMemoryStream& blob) const override {
		blob.write(w);
		blob.write(h);
		blob.write(scale);
	}

	void deserialize(InputMemoryStream& blob) override {
		blob.read(w);
		blob.read(h);
		blob.read(scale);
	}

	u32 w = 256;
	u32 h = 256;
	float scale = 4;
};

struct CellularNoiseNode final : CompositeTexture::Node {
	CellularNoiseNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::CELLULAR_NOISE; }

	bool hasInputPins() const override { return false; }
	bool hasOutputPins() const override { return true; }
	
	// https://www.shadertoy.com/view/MslGD8
	Vec2 voronoi(Vec2 x) const {
		Vec2 n = floor(x);
		Vec2 f = fract(x);
	
		Vec3 m = Vec3(8.0);
		for (i32 j = -1; j <= 1; j++) {
			for (i32 i = -1; i <= 1; i++) {
				Vec2 g = Vec2(float(i), float(j));
				Vec2 o = hash(n + g);
				Vec2 r = g - f + (sin(o * 2 * PI + Vec2(offset)) * 0.5f + Vec2(0.5f));
				float d = dot(r, r);
				if (d < m.x) m = Vec3(d, o.x, o.y);
			}
		}
	
		return Vec2(sqrtf(m.x), m.y + m.z);
	}

	bool gui() override {
		nodeTitle("Cellular noise");
		outputSlot();
		bool res = ImGui::DragFloat("Scale", &scale, 0.01f, FLT_MIN, FLT_MAX);
		res = ImGui::DragFloat("Offset", &offset, 0.01f, FLT_MIN, FLT_MAX) || res;
		res = ImGui::DragInt("Width", (i32*)&w, 1, 1, 999999) || res;
		res = ImGui::DragInt("Height", (i32*)&h, 1, 1, 999999) || res;
		return res;
	}

	bool generateInternal() override {
		CompositeTexture::Image& out = m_outputs.emplace(w, h, 1, m_allocator); 

		for (u32 j = 0; j < h; ++j) {
			const float v = j / float(h - 1);
			for (u32 i = 0; i < w; ++i) {
				float u = i / float(w - 1);
				float d = voronoi(Vec2(u, v) * scale).x;
				out.pixels[i + j * w] = d;
			}
		}
		return true;
	}

	void serialize(OutputMemoryStream& blob) const override {
		blob.write(w);
		blob.write(h);
		blob.write(scale);
		blob.write(offset);
	}

	void deserialize(InputMemoryStream& blob) override {
		blob.read(w);
		blob.read(h);
		blob.read(scale);
		blob.read(offset);
	}

	u32 w = 256;
	u32 h = 256;
	float scale = 4;
	float offset = 0;
};


struct SetAlphaNode final : CompositeTexture::Node {
	SetAlphaNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::SET_ALPHA; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	CompositeTexture::ValueResult getValue(const CompositeTexture::PixelContext& ctx) override {
		CompositeTexture::ValueResult rgb = getInputValue(0, ctx);
		CompositeTexture::ValueResult a = getInputValue(1, ctx);
		if (!rgb.isValid() || !a.isValid()) return errorValue("Invalid input");
		if (rgb.channels < 3) return errorValue("First input must have at least 3 channels");
		if (a.channels != 1) return errorValue("Second input must have only 1 channel");
		
		return Vec4(rgb.value.xyz(), a.value.x);
	}

	
	bool generateInternal() override {
		if (!generateInput(0)) return false;
		if (!generateInput(1)) return false;

		CompositeTexture::Image& rgb = getInputImage(0);
		CompositeTexture::Image& a = getInputImage(1);
		if (rgb.channels < 3) return error("First input must have at least 3 channels");
		if (a.channels != 1) return error("Second input must have only 1 channel");
		if (rgb.w != a.w) return error("Width does not match");
		if (rgb.h != a.h) return error("Height does not match");
		
		CompositeTexture::Image& out = m_outputs.emplace(rgb.w, rgb.h, 4, m_allocator); 

		for (u32 i = 0; i < out.w * out.h; ++i) {
			memcpy(&out.pixels[i * 4], &rgb.pixels[i * rgb.channels], rgb.channels * sizeof(float));
			out.pixels[i * 4 + 3] = a.pixels[i];
		}

		return true;
	}

	bool gui() override {
		nodeTitle("Set alpha");
		ImGui::BeginGroup();
		inputSlot(); ImGui::TextUnformatted("RGB");
		inputSlot(); ImGui::TextUnformatted("A");
		ImGui::EndGroup();
		ImGui::SameLine();
		outputSlot();
		return false;
	}
};

struct TranslateNode final : CompositeTexture::Node {
	TranslateNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::TRANSLATE; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	void serialize(OutputMemoryStream& blob) const override {
		blob.write(x);
		blob.write(y);
		blob.write(wrap);
	}

	void deserialize(InputMemoryStream& blob) override {
		blob.read(x);
		blob.read(y);
		blob.read(wrap);
	}

	bool generateInternal() override {
		if (!generateInput(0)) return false;

		CompositeTexture::Image& in = getInputImage(0);
		CompositeTexture::Image& out = m_outputs.emplace(in.w, in.h, in.channels, m_allocator); 
		
		const i32 tx = x < 0 ? -i32(-x % out.w) : x % out.w; 
		const i32 ty = y < 0 ? -i32(-y % out.h) : y % out.h;

		if (wrap) {
			for (i32 j = 0; j < (i32)in.h; ++j) {
				const i32 src_y = (j + ty + out.h) % out.h;
				for (u32 i = 0; i < (i32)in.w; ++i) {
					const u32 src_x = (i + tx + out.w) % out.w;
					memcpy(&out.pixels[(i + j * out.w) * out.channels]
						, &in.pixels[(src_x + src_y * in.w) * in.channels]
						, in.channels * sizeof(float));
				}
			}
		}
		else {
			for (i32 j = 0; j < (i32)in.h; ++j) {
				const u32 src_y = clamp(j + ty, 0, out.h - 1);
				for (i32 i = 0; i < (i32)in.w; ++i) {
					const u32 src_x = clamp(i + tx, 0, out.w - 1);
					memcpy(&out.pixels[(i + j * out.w) * out.channels]
						, &in.pixels[(src_x + src_y * in.w) * in.channels]
						, in.channels * sizeof(float));
				}
			}
		}

		return true;
	}

	bool gui() override {
		nodeTitle("Translate");
		inputSlot();
		outputSlot();
		bool res = ImGui::DragInt("X", &x, 1, INT_MIN, INT_MAX);
		res = ImGui::DragInt("Y", &y, 1, INT_MIN, INT_MAX) || res;
		res = ImGui::Checkbox("Wrap", &wrap) || res;
		return res;
	}

	i32 x = 0, y = 0;
	bool wrap = true;
};


struct CropNode final : CompositeTexture::Node {
	CropNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::CROP; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	void serialize(OutputMemoryStream& blob) const override {
		blob.write(x);
		blob.write(y);
		blob.write(w);
		blob.write(h);
	}

	void deserialize(InputMemoryStream& blob) override {
		blob.read(x);
		blob.read(y);
		blob.read(w);
		blob.read(h);
	}
	
	bool generateInternal() override {
		if (!generateInput(0)) return false;

		CompositeTexture::Image& in = getInputImage(0);
		if (x + w > in.w) return error("Out of bounds access");
		if (y + h > in.h) return error("Out of bounds access");

		CompositeTexture::Image& out = m_outputs.emplace(w, h, in.channels, m_allocator); 
		
		for (u32 j = 0; j < h; ++j) {
			memcpy(&out.pixels[j * w * out.channels]
				, &in.pixels[(x + (y + j) * in.w) * in.channels]
				, w * in.channels * sizeof(float));
		}
		return true;
	}

	bool gui() override {
		nodeTitle("Crop");
		inputSlot();
		ImGui::BeginGroup();
		bool res = ImGui::DragInt("X", (i32*)&x, 1, 0, 999999);
		res = ImGui::DragInt("Y", (i32*)&y, 1, 0, 999999) || res;
		res = ImGui::DragInt("Width", (i32*)&w, 1, 1, 999999) || res;
		res = ImGui::DragInt("Height", (i32*)&h, 1, 1, 999999) || res;
		ImGui::EndGroup();
		ImGui::SameLine();
		outputSlot();
		return res;
	}

	u32 x = 0, y = 0;
	u32 w = 256, h = 256;
};

struct StaticSwitchNode final : CompositeTexture::Node {
	StaticSwitchNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::STATIC_SWITCH; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }
	void serialize(OutputMemoryStream& blob) const override { blob.write(m_is_on); }
	void deserialize(InputMemoryStream& blob) override { blob.read(m_is_on); }

	CompositeTexture::ValueResult getValue(const CompositeTexture::PixelContext& ctx) override {
		return getInputValue(m_is_on ? 0 : 1, ctx);
	}

	bool generateInternal() override {
		if (!generateInput(m_is_on ? 0 : 1)) return false;

		CompositeTexture::Image& in = getInputImage(m_is_on ? 0 : 1);
		CompositeTexture::Image& out = m_outputs.emplace(in.w, in.h, in.channels, m_allocator);
		memcpy(out.pixels.begin(), in.pixels.begin(), in.pixels.byte_size());
		return true;
	}
	
	bool gui() override {
		nodeTitle("Switch");
		ImGui::BeginGroup();
		inputSlot(); ImGui::TextUnformatted("On");
		inputSlot(); ImGui::TextUnformatted("Off");
		ImGui::EndGroup();
		ImGui::SameLine();
		bool res = ImGui::Checkbox("##cb", &m_is_on);
		ImGui::SameLine();
		outputSlot();
		return res;
	}

	bool m_is_on = true;
};

struct StepNode final : CompositeTexture::Node {
	StepNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::STEP; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }
	void serialize(OutputMemoryStream& blob) const override { blob.write(m_value); }
	void deserialize(InputMemoryStream& blob) override { blob.read(m_value); }

	CompositeTexture::ValueResult getValue(const CompositeTexture::PixelContext& ctx) override {
		CompositeTexture::ValueResult arg0 = getInputValue(0, ctx);
		if (!arg0.isValid()) return errorValue("Invalid input");
		
		for (u32 ch = 0; ch < arg0.channels; ++ch) {
			arg0.value[ch] = arg0.value[ch] < m_value ? 0.f : 1.f;
		}
		return arg0;
	}

	bool generateInternal() override {
		if (!generateInput(0)) return false;

		CompositeTexture::Image& in = getInputImage(0);
		CompositeTexture::Image& out = m_outputs.emplace(in.w, in.h, in.channels, m_allocator); 
		
		for (i32 j = 0; j < (i32)in.h; ++j) {
			for (i32 i = 0; i < (i32)in.w; ++i) {
				for (u32 ch = 0; ch < in.channels; ++ch) {
					float v = in.pixels[(i + j * in.w) * in.channels + ch];
					v = v < m_value ? 0.f : 1.f;
					out.pixels[(i + j * out.w) * out.channels + ch] = v;
				}
			}
		}
		return true;
	}
	
	bool gui() override {
		nodeTitle("Step");
		inputSlot();
		outputSlot();
		return ImGui::DragFloat("Value", &m_value);
	}

	float m_value = 1;
};


struct GradientMapNode final : CompositeTexture::Node {
	GradientMapNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::GRADIENT_MAP; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	void serialize(OutputMemoryStream& blob) const override {
		blob.write(m_count);
		blob.write(m_keys);
		blob.write(m_values);
	}

	void deserialize(InputMemoryStream& blob) override {
		blob.read(m_count);
		blob.read(m_keys);
		blob.read(m_values);
	}
	
	CompositeTexture::ValueResult getValue(const CompositeTexture::PixelContext& ctx) override {
		CompositeTexture::ValueResult arg = getInputValue(0, ctx);
		if (!arg.isValid()) return errorValue("Invalid input");
		if (arg.channels != 1) return errorValue("Input must have only 1 channel");

		for (i32 k = 1; k < m_count; ++k) {
			if (arg.value.x <= m_keys[k]) {
				float t = (arg.value.x - m_keys[k - 1]) / (m_keys[k] - m_keys[k - 1]);
				return lerp(m_values[k - 1], m_values[k], t);
			}
		}
		return m_values[m_count - 1];
	}

	bool generateInternal() override {
		if (!generateInput(0)) return false;

		CompositeTexture::Image& in = getInputImage(0);
		if (in.channels != 1) return error("Input must have only 1 channel");

		CompositeTexture::Image& out = m_outputs.emplace(in.w, in.h, 4, m_allocator); 
		
		for (u32 i = 0, c = (u32)out.pixels.size(); i < c; i += out.channels) {
			const float v = minimum(in.pixels[i / 4], m_keys[m_count - 1]);
			
			for (i32 k = 1; k < m_count; ++k) {
				if (v <= m_keys[k]) {
					float t = (v - m_keys[k - 1]) / (m_keys[k] - m_keys[k - 1]);
					const Vec4 color = lerp(m_values[k - 1], m_values[k], t);
					memcpy(&out.pixels[i], &color, sizeof(color));
					break;
				}
			}
		}
		return true;
	}

	bool gui() override {
		nodeTitle("Gradient map");
		inputSlot();
		outputSlot();
		ImGui::SetNextItemWidth(150);
		bool res = ImGuiEx::Gradient4("##g", lengthOf(m_keys), &m_count, m_keys, &m_values[0].x);
		m_keys[0] = 0;
		m_keys[m_count - 1] = 1;
		return res;
	}

	i32 m_count = 2;
	float m_keys[8] = { 0, 1 };
	Vec4 m_values[8] = { Vec4(0, 0, 0, 1), Vec4(1, 1, 1, 1) };
};

static void blit(CompositeTexture::Image& dst, CompositeTexture::Image& src, i32 dst_x, i32 dst_y) {
	if (dst_x >= (i32)dst.w) return;
	if (dst_y >= (i32)dst.h) return;
	ASSERT(dst.channels == src.channels);
		
	for (i32 y = maximum(0, -dst_y); y < (i32)src.h && y + dst_y < (i32)dst.h; ++y) {
		for (i32 x = maximum(0, -dst_x); x < (i32)src.w && x + dst_x < (i32)dst.w; ++x) {
			i32 src_pixel = (x + y * src.w) * src.channels;
			float alpha = src.channels < 4 ? 1.f : src.pixels[src_pixel + 3];
			for (u32 ch = 0; ch < dst.channels; ++ch) {
				float& dst_p = dst.pixels[(x + dst_x + (y + dst_y) * dst.w) * dst.channels + ch];
				dst_p = lerp(dst_p, src.pixels[src_pixel + ch], alpha);
			}
		}
	}
}

struct CircularSplatterNode final : CompositeTexture::Node {
	CircularSplatterNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::CIRCULAR_SPLATTER; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	void serialize(OutputMemoryStream& blob) const override {
		blob.write(count);
		blob.write(radius);
		blob.write(radius_step);
		blob.write(radius_spread);
		blob.write(angle_step);
		blob.write(angle_spread);
	}

	void deserialize(InputMemoryStream& blob) override {
		blob.read(count);
		blob.read(radius);
		blob.read(radius_step);
		blob.read(radius_spread);
		blob.read(angle_step);
		blob.read(angle_spread);
	}

	bool generateInternal() override {
		if (!generateInput(0)) return false;
		if (!generateInput(1)) return false;

		CompositeTexture::Image& bg = getInputImage(0);
		CompositeTexture::Image& pattern = getInputImage(1);
		if (bg.channels != pattern.channels) return error("Inputs must have the same number of channels");

		CompositeTexture::Image& out = m_outputs.emplace(bg.w, bg.h, bg.channels, m_allocator); 
		memcpy(out.pixels.data(), bg.pixels.data(), bg.pixels.byte_size());
		
		for (u32 i = 0; i < count; ++i) {
			const float angle = i * angle_step + randFloat(-angle_spread, angle_spread);
			const float r = radius + radius_step * i + randFloat(-radius_spread, radius_spread);

			float x = out.w * 0.5f + r * cosf(angle) - pattern.w * 0.5f;
			float y = out.h * 0.5f + r * sinf(angle) - pattern.h * 0.5f;

			blit(out, pattern, u32(x + 0.5f), u32(y + 0.5f));
		}
		return true;
	}

	bool gui() override {
		nodeTitle("Circular splatter");
		inputSlot();
		outputSlot();
		ImGui::TextUnformatted("Background");
		inputSlot();
		ImGui::TextUnformatted("Pattern");
		bool res = ImGui::DragInt("Count", (i32*)&count, 1, 1, 999999);
		res = ImGui::DragFloat("Radius", &radius, 1, 0, FLT_MAX) || res;
		res = ImGui::DragFloat("Radius step", &radius_step, 1, -FLT_MAX, FLT_MAX) || res;
		res = ImGuiEx::InputAngle("Angle step", &angle_step) || res;
		res = ImGuiEx::InputAngle("Angle spread", &angle_spread) || res;
		res = ImGui::DragFloat("Radius spread", &radius_spread, 1, -FLT_MAX, FLT_MAX) || res;
		return res;
	}

	u32 count = 10;
	float radius = 100;
	float radius_step = 0;
	float radius_spread = 0;
	float angle_spread= 0;
	float angle_step = 0;
};

struct SplatterNode final : CompositeTexture::Node {
	SplatterNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::SPLATTER; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	void serialize(OutputMemoryStream& blob) const override {
		blob.write(x_count);
		blob.write(y_count);
		blob.write(x_spread);
		blob.write(y_spread);
	}

	void deserialize(InputMemoryStream& blob) override {
		blob.read(x_count);
		blob.read(y_count);
		blob.read(x_spread);
		blob.read(y_spread);
	}

	bool generateInternal() override {
		if (!generateInput(0)) return false;
		if (!generateInput(1)) return false;

		CompositeTexture::Image& bg = getInputImage(0);
		CompositeTexture::Image& pattern = getInputImage(1);
		if (bg.channels != pattern.channels) return error("Inputs must have the same number of channels");

		CompositeTexture::Image& out = m_outputs.emplace(bg.w, bg.h, bg.channels, m_allocator); 
		memcpy(out.pixels.data(), bg.pixels.data(), bg.pixels.byte_size());
		
		for (u32 j = 0; j < y_count; ++j) {
			for (u32 i = 0; i < x_count; ++i) {
				i32 x = i32(((float)i / x_count) * out.w);
				i32 y = i32(((float)j / y_count) * out.h);
				x += rand(0, 2 * x_spread) - x_spread;
				y += rand(0, 2 * y_spread) - y_spread;
				blit(out, pattern, x, y);
			}
		}
		return true;
	}

	bool gui() override {
		nodeTitle("Splatter");
		inputSlot();
		outputSlot();
		ImGui::TextUnformatted("Background");
		inputSlot();
		ImGui::TextUnformatted("Pattern");
		bool res = ImGui::DragInt("X count", (i32*)&x_count, 1, 1, 999999);
		res = ImGui::DragInt("Y count", (i32*)&y_count, 1, 1, 999999) || res;
		res = ImGui::DragInt("X spread", (i32*)&x_spread, 1, 1, 999999) || res;
		res = ImGui::DragInt("Y spread", (i32*)&y_spread, 1, 1, 999999) || res;
		return res;
	}

	u32 x_count = 10;
	u32 y_count = 10;
	u32 x_spread = 0;
	u32 y_spread = 0;
};

struct SharpenNode final : CompositeTexture::Node {
	SharpenNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::SHARPEN; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	bool generateInternal() override {
		if (!generateInput(0)) return false;

		CompositeTexture::Image& in = getInputImage(0);
		CompositeTexture::Image& out = m_outputs.emplace(in.w, in.h, in.channels, m_allocator); 
		
		float inv = 1/9.f;
		const float conv_mtx[] = {
			-inv, -inv, -inv,
			-inv, 17 * inv, -inv,
			-inv, -inv, -inv
		};

		for (i32 j = 0; j < (i32)in.h; ++j) {
			for (i32 i = 0; i < (i32)in.w; ++i) {
				for (u32 ch = 0; ch < in.channels; ++ch) {
					float v = 0;
					for (i32 cj = -1; cj <= 1; ++cj) {
						for (i32 ci = -1; ci <= 1; ++ci) {
							const u32 x = clamp(i + ci, 0, in.w - 1);
							const u32 y = clamp(j + cj, 0, in.h - 1);
							v += in.pixels[(x + y * in.w) * in.channels + ch] * conv_mtx[(ci + 1) + (cj + 1) * 3];
						}
					}
					out.pixels[(i + j * out.w) * out.channels + ch] = v;
				}
			}
		}
		return true;
	}

	bool gui() override {
		nodeTitle("Sharpen");
		inputSlot();
		outputSlot();
		ImGui::TextUnformatted(" ");
		return false;
	}
};

struct CurveNode final : CompositeTexture::Node {
	CurveNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::CURVE; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	void serialize(OutputMemoryStream& blob) const override {
		blob.write(point_count);
		blob.write(points, sizeof(points));
	}

	void deserialize(InputMemoryStream& blob) override {
		blob.read(point_count);
		blob.read(points, sizeof(points));
	}
	
	CompositeTexture::ValueResult getValue(const CompositeTexture::PixelContext& ctx) override {
		CompositeTexture::ValueResult r = getInputValue(0, ctx);
		if (!r.isValid()) return errorValue("Invalid input");

		for (u32 i = 0; i < r.channels; ++i) {
			r.value[i] = eval(r.value[i]);
		}
		return r;
	}

	bool generateInternal() override {
		if (!generateInput(0)) return false;

		CompositeTexture::Image& in = getInputImage(0);
		CompositeTexture::Image& out = m_outputs.emplace(in.w, in.h, in.channels, m_allocator); 
		
		for (u32 i = 0; i < (u32)in.pixels.size(); ++i) {
			float v = in.pixels[i];
			v = eval(v);
			out.pixels[i] = v;
		}
		return true;
	}
	
	static float cubicInterpolate(float y0, float y1, float y2, float y3, float t) {
		#if 0 // cubic
			float a0 = y3 - y2 - y0 + y1;
			float a1 = y0 - y1 - a0;
			float a2 = y2 - y0;
			float a3 = y1;
		#else // catmull rom
			float a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
			float a1 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
			float a2 = -0.5f * y0 + 0.5f * y2;
			float a3 = y1;
		#endif

		float t2 = t * t;
		return a0 * t * t2 + a1 * t2 + a2 * t + a3;
	}

	float eval(float x) {
		auto p = [&](i32 idx){
			if (idx < 0) return points[0].y - (points[1].y - points[0].y);
			if (idx >= (i32)point_count) return points[point_count - 1].y - (points[point_count - 2].y - points[point_count - 1].y);
			return points[idx].y;
		};
		for (i32 j = 1; j < (i32)point_count; ++j) {
			if (points[j].x >= x) {
				const float t = (x - points[j - 1].x) / (points[j].x - points[j - 1].x);
				return cubicInterpolate(p(j - 2), p(j - 1), p(j), p(j + 1), t);
			}
		}
		return points[0].y;
	}

	static float len(ImVec2 p) {
		return sqrtf(p.x * p.x + p.y * p.y);
	}

	static ImVec2 mix(ImVec2 a, ImVec2 b, ImVec2 t) {
		return {
			a.x * (1 - t.x) + b.x * t.x,
			a.y * (1 - t.y) + b.y * t.y
		};
	}

	bool curve() {
		const ImU32 color_border = ImGui::GetColorU32(ImGuiCol_Border);
		const ImU32 color = ImGui::GetColorU32(ImGuiCol_PlotLines);
		const ImU32 color_hovered = ImGui::GetColorU32(ImGuiCol_PlotLinesHovered);
		ImGui::InvisibleButton("curve", ImVec2(210, 210));
		const bool is_hovered = ImGui::IsItemHovered();
		ImVec2 mp = ImGui::GetMousePos();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		
		ImVec2 from = ImGui::GetItemRectMin() + ImVec2(5, 5);
		ImVec2 to = from + ImGui::GetItemRectSize() - ImVec2(10, 10);
		swap(to.y, from.y);

		ImVec2 prev_p = points[0];
		for (u32 i = 1; i < 51; ++i) {
			float x = i / float(50);
			float y = clamp(eval(x), 0.f, 1.f);
			ImVec2 p(x, y);
			ImVec2 a = mix(from, to, prev_p);
			ImVec2 b = mix(from, to, p);
			dl->AddLine(a, b, color);
			prev_p = p;
		}
		dl->AddRect(from - ImVec2(5, -5), to + ImVec2(5, -5), color_border);

		i32 hovered_point = -1;
		bool changed = false;
		if (ImGui::IsMouseReleased(0)) dragged_point = -1;
		for (u32 i = 0; i < point_count; ++i) {
			ImVec2 center = mix(from, to, points[i]);
			const bool is_point_hovered = is_hovered && len(mp - center) < 5;
			if (is_point_hovered) hovered_point = i;
			dl->AddCircle(center, 5, is_point_hovered ? color_hovered : color);
			if (is_point_hovered && ImGui::IsMouseClicked(0)) dragged_point = i;
			if (ImGui::IsMouseDragging(0) && dragged_point == i) {
				points[i] = points[i] + ImGui::GetMouseDragDelta() / (to - from);
				changed = true;
				if (i > 0 && points[i].x < points[i - 1].x) {
					swap(points[i], points[i - 1]);
					--dragged_point;
				}
				if (i < point_count - 1 && points[i].x > points[i + 1].x) {
					++dragged_point;
					swap(points[i], points[i + 1]);
				}
				ImGui::ResetMouseDragDelta();
			}
		}

		if (is_hovered && ImGui::IsMouseDoubleClicked(0)) {
			if (hovered_point >= 0) {
				if (point_count > 2) {
					memmove(points + hovered_point, points + hovered_point + 1, (point_count - hovered_point - 1) * sizeof(points[0]));
					--point_count;
					changed = true;
				}
			}
			else if (point_count < lengthOf(points)) {
				ImVec2 t = (mp - from) / (to - from);
				for (u32 i = 0; i < point_count; ++i) {
					if (t.x < points[i].x) {
						memmove(points + i + 1, points + i, (point_count - i) * sizeof(points[0]));
						points[i] = t;
						++point_count;
						changed = true;
						break;
					}
				}
			}
		}

		points[0].x = 0;
		points[point_count - 1].x = 1;
		for (u32 i = 0; i < point_count; ++i) {
			points[i].y = clamp(points[i].y, 0.f, 1.f);
		}
		return changed;
	}

	bool gui() override {
		nodeTitle("Curve");
		inputSlot();
		bool res = curve();
		ImGui::SameLine();
		outputSlot();
		return res;
	}

	ImVec2 points[16] = { ImVec2(0, 0), ImVec2(1, 1) };
	u32 point_count = 2;
	i32 dragged_point = -1;
};

struct TwirlNode final : CompositeTexture::Node {
	TwirlNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::TWIRL; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	void serialize(OutputMemoryStream& blob) const override {
		blob.write(intensity);
	}

	void deserialize(InputMemoryStream& blob) override {
		blob.read(intensity);
	}

	bool generateInternal() override {
		if (!generateInput(0)) return false;

		CompositeTexture::Image& in = getInputImage(0);

		CompositeTexture::Image& out = m_outputs.emplace(in.w, in.h, in.channels, m_allocator);
		
		for (i32 j = 0; j < (i32)out.h; ++j) {
			for (i32 i = 0; i < (i32)out.w; ++i) {
				const float tx = (i - out.w * 0.5f) / (out.w * 0.5f);
				const float ty = (j - out.h * 0.5f) / (out.h * 0.5f);
				const float r = sqrtf(tx * tx + ty * ty) * intensity;
				const float s = sinf(r);
				const float c = cosf(r);
				float x = tx * c + ty * s;
				float y = tx * -s + ty * c;

				x = x * out.w * 0.5f + out.w * 0.5f;
				y = y * out.h * 0.5f + out.h * 0.5f;

				const Vec4 p = in.sampleWrap(x, y);
				out.setPixel(i, j, p);
			}
		}

		return true;
	}

	bool gui() override {
		nodeTitle("Twirl");
		inputSlot();
		outputSlot();
		return ImGui::DragFloat("Intensity", &intensity, 0.1f, -FLT_MAX, FLT_MAX);
	}

	float intensity = 1.f;
};

struct NormalmapNode final : CompositeTexture::Node {
	NormalmapNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::NORMALMAP; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	void serialize(OutputMemoryStream& blob) const override {
		blob.write(intensity);
	}

	void deserialize(InputMemoryStream& blob) override {
		blob.read(intensity);
	}

	bool generateInternal() override {
		if (!generateInput(0)) return false;

		CompositeTexture::Image& in = getInputImage(0);
		if (in.channels != 1) return error("Input must have only 1 channel");

		CompositeTexture::Image& out = m_outputs.emplace(in.w, in.h, 2, m_allocator);
		for (i32 j = 0; j < (i32)out.h; ++j) {
			for (i32 i = 0; i < (i32)out.w; ++i) {
				const float dx = clamp((in.sampleWrap(i + 1, j) - in.sampleWrap(i - 1, j)).x * intensity, -1.f, 1.f);
				const float dy = clamp((in.sampleWrap(i, j + 1) - in.sampleWrap(i, j - 1)).x * intensity, -1.f, 1.f);
				
				out.setPixel(i, j, Vec4(dx * 0.5f + 0.5f, dy * 0.5f + 0.5f, 0, 0));
			}
		}

		return true;
	}

	bool gui() override {
		nodeTitle("Normalmap");
		inputSlot();
		outputSlot();
		
		return ImGui::DragFloat("Intensity", &intensity, 0.1f, 0, FLT_MAX);
	}

	float intensity = 1.f;
};

struct WarpNode final : CompositeTexture::Node {
	WarpNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::WARP; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	void serialize(OutputMemoryStream& blob) const override {
		blob.write(intensity);
	}

	void deserialize(InputMemoryStream& blob) override {
		blob.read(intensity);
	}

	bool generateInternal() override {
		if (!generateInput(0)) return false;
		if (!generateInput(1)) return false;

		CompositeTexture::Image& in0 = getInputImage(0);
		CompositeTexture::Image& in1 = getInputImage(1);
		if (in0.w != in1.w) return error("Width does not match");
		if (in0.h != in1.h) return error("Height does not match");
		if (in1.channels != 1) return error("Second input must have only 1 channel");

		CompositeTexture::Image& out = m_outputs.emplace(in0.w, in0.h, in0.channels, m_allocator);
		for (i32 j = 0; j < (i32)out.h; ++j) {
			for (i32 i = 0; i < (i32)out.w; ++i) {
				const float dx = (in1.sample(i + 1, j) - in1.sample(i - 1, j)).x;
				const float dy = (in1.sample(i, j + 1) - in1.sample(i, j - 1)).x;
				
				const Vec4 p = in0.sample(i + dx * intensity, j + dy * intensity);
				out.setPixel(i, j, p);
			}
		}

		return true;
	}

	bool gui() override {
		nodeTitle("Warp");
		inputSlot();
		outputSlot();
		ImGui::TextUnformatted("Source");
		inputSlot();
		ImGui::TextUnformatted("Pattern");
		
		return ImGui::DragFloat("Intensity", &intensity, 1.f, -FLT_MAX, FLT_MAX);
	}

	float intensity = 1000.f;
};

struct BlurNode final : CompositeTexture::Node {
	BlurNode(IAllocator& allocator)
		: Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::BLUR; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	void serialize(OutputMemoryStream& blob) const override {
		blob.write(iterations);
	}

	void deserialize(InputMemoryStream& blob) override {
		blob.read(iterations);
	}

	bool generateInternal() override {
		if (!generateInput(0)) return false;

		CompositeTexture::Image& in = getInputImage(0);
		CompositeTexture::Image& out = m_outputs.emplace(in.w, in.h, in.channels, m_allocator);

		Array<float> tmp(m_resource->m_allocator);
		tmp.resize(out.pixels.size());

		for (u32 iter = 0; iter < iterations; ++iter) {
			if (in.w > 1) {
				CompositeTexture::Image& src = iter == 0 ? in : out;
				for (u32 j = 0; j < out.h; ++j) {
					for (u32 ch = 0; ch < out.channels; ++ch) {
						const u32 idx = j * src.w * src.channels + ch;
						const float* f = &src.pixels[idx];
						tmp[idx] = (f[0] * 2 + f[src.channels]) / 3;
					}

					for (u32 i = 1; i < out.w - 1; ++i) {
						u32 idx = (i + j * src.w) * src.channels;
						for (u32 ch = 0; ch < out.channels; ++ch) {
							const float* f = &src.pixels[idx + ch];
							tmp[idx + ch] = (f[-(i32)src.channels] + *f + f[src.channels]) / 3;
						}
					}

					for (u32 ch = 0; ch < out.channels; ++ch) {
						const u32 idx = (out.w - 1 + j * src.w) * src.channels + ch;
						const float* f = &src.pixels[idx];
						tmp[idx] = (f[0] * 2 + f[-(i32)src.channels]) / 3;
					}
				}
			} else {
				CompositeTexture::Image& src = iter == 0 ? in : out;
				memcpy(tmp.data(), src.pixels.data(), tmp.byte_size());
			}

			if (in.h > 1) {
				const i32 line_offset = out.w * out.channels;
				for (u32 i = 0; i < out.w; ++i) {
					for (u32 ch = 0; ch < out.channels; ++ch) {
						const u32 idx = i * out.channels + ch;
						const float* f = &tmp[idx];
						out.pixels[idx] = (f[0] * 2 + f[line_offset]) / 3;
					}

					for (u32 j = 1; j < out.h - 1; ++j) {
						u32 idx = (i + j * out.w) * out.channels;
						for (u32 ch = 0; ch < out.channels; ++ch) {
							const float* f = &tmp[idx + ch];
							out.pixels[idx + ch] = (f[-line_offset] + *f + f[line_offset]) / 3;
						}
					}

					for (u32 ch = 0; ch < out.channels; ++ch) {
						const u32 idx = (i + (out.h - 1) * out.w) * out.channels + ch;
						const float* f = &tmp[idx];
						out.pixels[idx] = (f[0] * 2 + f[-line_offset]) / 3;
					}
				}
			} else {
				memcpy(out.pixels.data(), tmp.data() , tmp.byte_size());
			}
		}

		return true;
	}

	bool gui() override {
		nodeTitle("Blur");
		inputSlot();
		outputSlot();
		bool res = ImGui::DragInt("Iterations", (i32*)&iterations, 1, 1, 999999);
		return res;
	}

	u32 iterations = 4;
};

struct CheckerboardNode final : CompositeTexture::Node {
	CheckerboardNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::CHECKERBOARD; }
	bool hasInputPins() const override { return false; }
	bool hasOutputPins() const override { return true; }

	void serialize(OutputMemoryStream& blob) const override {
		blob.write(w);
		blob.write(h);
		blob.write(size);
	}

	void deserialize(InputMemoryStream& blob) override {
		blob.read(w);
		blob.read(h);
		blob.read(size);
	}

	bool generateInternal() override {
		CompositeTexture::Image& out = m_outputs.emplace(w, h, 1, m_allocator);
		for (u32 j = 0; j < h; ++j) {
			for (u32 i = 0; i < w; ++i) {
				u32 color = ((i / size) + (j / size)) % 2;
				out.pixels[i + j * out.w] = (float)color;
			}
		}
		return true;
	}

	bool gui() override {
		nodeTitle("Checkerboard");
		ImGui::BeginGroup();
		bool res = ImGui::DragInt("Width", (i32*)&w, 1, 1, INT_MAX);
		res = ImGui::DragInt("Height", (i32*)&h, 1, 1, INT_MAX) || res;
		res = ImGui::DragInt("Size", (i32*)&size, 1, 1, INT_MAX) || res;
		ImGui::EndGroup();
		ImGui::SameLine();
		outputSlot();
		return res;
	}

	u32 w = 256;
	u32 h = 256;
	u32 size = 16;
};

struct TriangleNode final : CompositeTexture::Node {
	TriangleNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::TRIANGLE; }
	bool hasInputPins() const override { return false; }
	bool hasOutputPins() const override { return true; }

	void serialize(OutputMemoryStream& blob) const override {
		blob.write(w);
		blob.write(h);
		blob.write(power);
	}

	void deserialize(InputMemoryStream& blob) override {
		blob.read(w);
		blob.read(h);
		blob.read(power);
	}

	bool generateInternal() override {
		CompositeTexture::Image& out = m_outputs.emplace(w, h, 1, m_allocator);
		const float mx = SQRT3 * 0.5f;
		const float my = 0.5f;
		for (u32 j = 0; j < h; ++j) {
			for (u32 i = 0; i < w; ++i) {
				Vec2 v(i / float(w - 1) - 0.5f
					, j / float(h - 1) - 0.5f);
				
				float d = 2 * maximum(v.y, mx * v.x - my * v.y, mx * -v.x - my * v.y);
				d = powf(d, power);
				out.pixels[i + j * w] = d;
			}
		}
		return true;
	}
	
	bool gui() override {
		nodeTitle("Triangle");
		ImGui::BeginGroup();
		bool res = ImGui::DragInt("Width", (i32*)&w, 1, 1, 999999);
		res = ImGui::DragInt("Height", (i32*)&h, 1, 1, 999999) || res;
		res = ImGui::DragFloat("Power", &power, 0.1f, FLT_MIN, FLT_MAX) || res;
		ImGui::EndGroup();
		ImGui::SameLine();
		outputSlot();
		return res;
	}

	u32 w = 256;
	u32 h = 256;
	float power = 1.f;
};

struct SquareNode final : CompositeTexture::Node {
	SquareNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::SQUARE; }
	bool hasInputPins() const override { return false; }
	bool hasOutputPins() const override { return true; }

	void serialize(OutputMemoryStream& blob) const override {
		blob.write(w);
		blob.write(h);
		blob.write(power);
	}

	void deserialize(InputMemoryStream& blob) override {
		blob.read(w);
		blob.read(h);
		blob.read(power);
	}

	bool generateInternal() override {
		CompositeTexture::Image& out = m_outputs.emplace(w, h, 1, m_allocator);
		for (u32 j = 0; j < h; ++j) {
			for (u32 i = 0; i < w; ++i) {
				Vec2 v(i / float(w - 1) - 0.5f
					, j / float(h - 1) - 0.5f);
				float d = powf(maximum(fabsf(v.x), fabsf(v.y)) * 2, power);
				out.pixels[i + j * w] = d;
			}
		}
		return true;
	}
	
	bool gui() override {
		nodeTitle("Square");
		ImGui::BeginGroup();
		bool res = ImGui::DragInt("Width", (i32*)&w, 1, 1, 999999);
		res = ImGui::DragInt("Height", (i32*)&h, 1, 1, 999999) || res;
		res = ImGui::DragFloat("Power", &power, 0.1f, FLT_MIN, FLT_MAX) || res;
		ImGui::EndGroup();
		ImGui::SameLine();
		outputSlot();
		return res;
	}

	u32 w = 256;
	u32 h = 256;
	float power = 1.f;
};

struct CircleNode final : CompositeTexture::Node {
	CircleNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::CIRCLE; }
	bool hasInputPins() const override { return false; }
	bool hasOutputPins() const override { return true; }

	void serialize(OutputMemoryStream& blob) const override {
		blob.write(w);
		blob.write(h);
		blob.write(power);
	}

	void deserialize(InputMemoryStream& blob) override {
		blob.read(w);
		blob.read(h);
		blob.read(power);
	}

	bool generateInternal() override {
		CompositeTexture::Image& out = m_outputs.emplace(w, h, 1, m_allocator);
		for (u32 j = 0; j < h; ++j) {
			for (u32 i = 0; i < w; ++i) {
				Vec2 v(i / float(w - 1) - 0.5f
					, j / float(h - 1) - 0.5f);
				float d = powf(length(v) * 2, power);
				out.pixels[i + j * w] = d;
			}
		}
		return true;
	}
	
	bool gui() override {
		nodeTitle("Circle");
		ImGui::BeginGroup();
		bool res = ImGui::DragInt("Width", (i32*)&w, 1, 1, 999999);
		res = ImGui::DragInt("Height", (i32*)&h, 1, 1, 999999) || res;
		res = ImGui::DragFloat("Power", &power, 0.1f, FLT_MIN, FLT_MAX) || res;
		ImGui::EndGroup();
		ImGui::SameLine();
		outputSlot();
		return res;
	}

	u32 w = 256;
	u32 h = 256;
	float power = 1.f;
};

struct GrayscaleNode final : CompositeTexture::Node {
	GrayscaleNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::GRAYSCALE; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }
	
	CompositeTexture::ValueResult getValue(const CompositeTexture::PixelContext& ctx) override {
		CompositeTexture::ValueResult r = getInputValue(0, ctx);
		if (!r.isValid()) return errorValue("Invalid input");
		if (r.channels < 3) return errorValue("Input must have at least 3 channels");
			
		float grayscale = r.value.x * 0.299f + r.value.y * 0.587f + r.value.z * 0.114f;
		r.value.x = r.value.y = r.value.z = grayscale;
		return r;
	}

	bool generateInternal() override {
		if (!generateInput(0)) return false;

		CompositeTexture::Image& in = getInputImage(0);
		if (in.channels < 3) return error("Input must have at least 3 channels");
		CompositeTexture::Image& out = m_outputs.emplace(in.w, in.h, in.channels, m_allocator); 
		
		for (u32 i = 0, c = (u32)in.pixels.size(); i < c; i += in.channels) {
			Vec3 v(in.pixels[i], in.pixels[i + 1], in.pixels[i + 2]);
			float grayscale = v.x * 0.299f + v.y * 0.587f + v.z * 0.114f;
			out.pixels[i] = out.pixels[i + 1] = out.pixels[i + 2] = grayscale;
			if (in.channels > 3) out.pixels[i + 3] = in.pixels[i +  3];
		}
		return true;
	}

	bool gui() override {
		nodeTitle("Grayscale");
		inputSlot();
		ImGui::TextUnformatted(" ");
		ImGui::SameLine();
		outputSlot();
		return false;
	}
};

struct MixNode final : CompositeTexture::Node {
	MixNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::MIX; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	void serialize(OutputMemoryStream& blob) const override {
		blob.write(alpha);
	}

	void deserialize(InputMemoryStream& blob) override {
		blob.read(alpha);
	}

	CompositeTexture::ValueResult getValue(const CompositeTexture::PixelContext& ctx) override {
		CompositeTexture::ValueResult a = getInputValue(0, ctx);
		CompositeTexture::ValueResult b = getInputValue(1, ctx);

		if (!a.isValid() || !b.isValid()) return errorValue("Invalid input");
		if (a.channels != b.channels) return errorValue("Inputs must have the same number of channels");

		a.value = lerp(a.value, b.value, alpha);
		return a;
	}	

	bool generateInternal() override {
		if (!generateInput(0)) return false;
		if (!generateInput(1)) return false;

		CompositeTexture::Image& in0 = getInputImage(0);
		CompositeTexture::Image& in1 = getInputImage(1);
		if (in0.channels != in1.channels) return error("Number of channel does not match");
		if (in0.w != in1.w) return error("Width does not match");
		if (in0.h != in1.h) return error("Height does not match");
		
		CompositeTexture::Image& out = m_outputs.emplace(in0.w, in0.h, in0.channels, m_allocator); 

		for (u32 i = 0, c = (u32)out.pixels.size(); i < c; ++i) {
			const float a = in0.pixels[i];
			const float b = in1.pixels[i];
			out.pixels[i] = a * (1 - alpha) + b * alpha;
		}
		return true;
	}

	bool gui() override {
		nodeTitle("Mix");
		ImGui::BeginGroup();
		inputSlot(); ImGui::TextUnformatted("A");
		inputSlot(); ImGui::TextUnformatted("B");
		ImGui::EndGroup();
		ImGui::SameLine();
		ImGui::SetNextItemWidth(150);
		ImGui::SliderFloat("##alpha", &alpha, 0.f, 1.f);
		ImGui::SameLine();
		outputSlot();
		return false;
	}

	float alpha = 0.5f;
};

struct BrightnessNode final : CompositeTexture::Node {
	BrightnessNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::BRIGHTNESS; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	void serialize(OutputMemoryStream& blob) const override {
		blob.write(brightness);
	}

	void deserialize(InputMemoryStream& blob) override {
		blob.read(brightness);
	}

	bool generateInternal() override {
		if (!generateInput(0)) return false;

		CompositeTexture::Image& in = getInputImage(0);
		CompositeTexture::Image& out = m_outputs.emplace(in.w, in.h, in.channels, m_allocator); 
		
		for (u32 i = 0, c = (u32)in.pixels.size(); i < c; ++i) {
			const bool is_alpha = in.channels == 4 && i % 4 == 3;
			out.pixels[i] = in.pixels[i] + (is_alpha ? 0 : brightness);
		}
		return true;
	}

	bool gui() override {
		nodeTitle("Brightness");
		inputSlot();
		ImGui::SetNextItemWidth(150);
		bool res = ImGui::SliderFloat("##v", &brightness, -1.f, 1.f);
		ImGui::SameLine();
		outputSlot();
		return res;
	}

	float brightness = 0;
};

struct ContrastNode final : CompositeTexture::Node {
	ContrastNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::CONTRAST; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	void serialize(OutputMemoryStream& blob) const override {
		blob.write(contrast);
	}

	void deserialize(InputMemoryStream& blob) override {
		blob.read(contrast);
	}

	bool generateInternal() override {
		if (!generateInput(0)) return false;

		CompositeTexture::Image& in = getInputImage(0);
		CompositeTexture::Image& out = m_outputs.emplace(in.w, in.h, in.channels, m_allocator); 
		
		const float factor = 259 * (contrast + 255) / (255 * (259 - contrast));
		for (u32 i = 0, c = (u32)in.pixels.size(); i < c; ++i) {
			const bool is_alpha = in.channels == 4 && i % 4 == 3;
			out.pixels[i] = is_alpha ? in.pixels[i] : factor * (in.pixels[i] - 0.5f) + 0.5f;
		}
		return true;
	}

	bool gui() override {
		nodeTitle("Contrast");
		inputSlot();
		ImGui::SetNextItemWidth(150);
		bool res = ImGui::SliderFloat("##v", &contrast, -255.f, 255.f);
		ImGui::SameLine();
		outputSlot();
		return res;
	}

	float contrast = 0;
};

struct InvertNode final : CompositeTexture::Node {
	InvertNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::INVERT; }

	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }
	
	CompositeTexture::ValueResult getValue(const CompositeTexture::PixelContext& ctx) override {
		CompositeTexture::ValueResult r = getInputValue(0, ctx);
		if (!r.isValid()) return errorValue("Invalid input");
		for (u32 i = 0; i < r.channels; ++i) {
			r.value[i] = 1 - r.value[i];
		}
		return r;
	}

	bool generateInternal() override {
		if (!generateInput(0)) return false;

		CompositeTexture::Image& in = getInputImage(0);
		CompositeTexture::Image& out = m_outputs.emplace(in.w, in.h, in.channels, m_allocator); 
		
		for (u32 i = 0, c = (u32)in.pixels.size(); i < c; ++i) {
			out.pixels[i] = 1 - in.pixels[i];
		}
		return true;
	}

	bool gui() override {
		nodeTitle("Invert");
		inputSlot();
		ImGui::TextUnformatted(" ");
		ImGui::SameLine();
		outputSlot();
		return false;
	}
};

struct SplatNode final : CompositeTexture::Node {
	SplatNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::SPLAT; }

	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	CompositeTexture::ValueResult getValue(const CompositeTexture::PixelContext& ctx) override {
		CompositeTexture::ValueResult r = getInputValue(0, ctx);
		if (!r.isValid()) return errorValue("Invalid input");
		if (r.channels != 1) return errorValue("Input must have only 1 channel");
		
		return Vec4(r.value.x);
	}

	bool generateInternal() override {
		if (!generateInput(0)) return false;

		CompositeTexture::Image& in = getInputImage(0);
		if (in.channels != 1) return error("Input must have only 1 channel");

		CompositeTexture::Image& out = m_outputs.emplace(in.w, in.h, 4, m_allocator); 
		
		for (u32 i = 0, c = in.w * in.h; i < c; ++i) {
			out.pixels[i * 4 + 0] = in.pixels[i];
			out.pixels[i * 4 + 1] = in.pixels[i];
			out.pixels[i * 4 + 2] = in.pixels[i];
			out.pixels[i * 4 + 3] = in.pixels[i];
		}
		return true;
	}

	bool gui() override {
		nodeTitle("Splat");
		inputSlot();
		ImGui::TextUnformatted(" ");
		ImGui::SameLine();
		outputSlot();
		return false;
	}
};

struct InputNode final : CompositeTexture::Node {
	InputNode(IAllocator& allocator) : Node(allocator) {}
	
	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::INPUT; }

	bool hasInputPins() const override { return false; }
	bool hasOutputPins() const override { return true; }
	
	void serialize(OutputMemoryStream& blob) const override {
		blob.writeString(m_texture);
	}
	
	void deserialize(InputMemoryStream& blob) override {
		m_texture = blob.readString();
	}

	bool generateInternal() override {
		if (m_texture.isEmpty()) return error("Missing texture");
		i32 w, h, cmp;
		OutputMemoryStream file_content(m_resource->m_allocator);
		FileSystem& fs = m_resource->m_app.getEngine().getFileSystem();
		file_content.clear();
		if (!fs.getContentSync(m_texture, file_content)) return error("Failed to read file");

		stbi_uc* pixels = stbi_load_from_memory(file_content.data(), (i32)file_content.size(), &w, &h, &cmp, 0);
		if (!pixels) return error("Failed to load file");

		CompositeTexture::Image& out = m_outputs.emplace(w, h, cmp, m_allocator); 
		for (u32 i = 0; i < u32(w * h * cmp); ++i) {
			out.pixels[i] = pixels[i] / 255.f;
		}
		free(pixels);
		return true;
	}

	bool gui() override {
		nodeTitle("Input");
		outputSlot(); 
		return m_resource->m_app.getAssetBrowser().resourceInput("Source", m_texture, Texture::TYPE, 150);
	}

	Path m_texture;
};

struct OutputNode final : CompositeTexture::Node {
	OutputNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::OUTPUT; }

	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return false; }
	
	void serialize(OutputMemoryStream& blob) const override {
		blob.write(m_output_type);
		blob.write(m_layers_count);
		blob.write(m_channels_count);
	}
	
	void deserialize(InputMemoryStream& blob) override {
		blob.read(m_output_type);
		blob.read(m_layers_count);
		blob.read(m_channels_count);
	}

	bool generateInternal() override {
		switch(m_output_type) {
			case OutputType::SIMPLE: return generateInput(0);
			case OutputType::ARRAY:
				for (u32 i = 0; i < m_layers_count; ++i) {
					if (!generateInput(i)) return false;
				}
				return true;
			case OutputType::CUBEMAP:
				for (u32 i = 0; i < 6; ++i) {
					if (!generateInput(i)) return false;
				}
				return true;
		}
		ASSERT(false);
		return false;
	};

	bool gui() override {
		nodeTitle("Output");
		switch(m_output_type) {
			case OutputType::SIMPLE:
				inputSlot(); ImGui::TextUnformatted("Color");
				break;
			case OutputType::ARRAY:
				for (u32 i = 0; i < m_layers_count; ++i) {
					inputSlot(); ImGui::Text("Layer %d", i);
				}
				if (m_layers_count > 0 && !getInput(m_layers_count - 1)) --m_layers_count;
				inputSlot(); ImGui::TextUnformatted("New layer");
				if (getInput(m_layers_count)) ++m_layers_count;
				break;
			case OutputType::CUBEMAP:
				inputSlot(); ImGui::TextUnformatted("X+");
				inputSlot(); ImGui::TextUnformatted("X-");
				inputSlot(); ImGui::TextUnformatted("Y+");
				inputSlot(); ImGui::TextUnformatted("Y-");
				inputSlot(); ImGui::TextUnformatted("Z+");
				inputSlot(); ImGui::TextUnformatted("Z-");
				break;
		}

		u32 old_pin_count;
		switch(m_output_type) {
			case OutputType::SIMPLE: old_pin_count = 1; break;
			case OutputType::CUBEMAP: old_pin_count = 6; break;
			case OutputType::ARRAY: old_pin_count = m_layers_count; break;
		}
		bool res = ImGui::Combo("Type", (i32*)&m_output_type, "Simple\0Array\0Cubemap\0");
		if (res) {
			u32 new_pin_count;
			switch(m_output_type) {
				case OutputType::SIMPLE: new_pin_count = 1; break;
				case OutputType::CUBEMAP: new_pin_count = 6; break;
				case OutputType::ARRAY: new_pin_count = m_layers_count; break;
			}
			if (new_pin_count < old_pin_count) {
				for (i32 i = m_resource->m_links.size() - 1; i >= 0; --i) {
					CompositeTexture::Link& link = m_resource->m_links[i];
					if (link.getToNode() == m_id && link.getToPin() >= new_pin_count) {
						m_resource->m_links.erase(i);
					}
				}
			}
		}
		i32 c = m_channels_count - 1;
		if (ImGui::Combo("##t", &c, "R\0RG\0RGB\0RGBA\0")) {
			m_channels_count = c + 1;
			return true;
		}
		return res;
	}

	enum class OutputType : u32 {
		SIMPLE,
		ARRAY,
		CUBEMAP
	};

	OutputType m_output_type = OutputType::SIMPLE;
	u32 m_layers_count = 1;
	u32 m_channels_count = 4;
};

CompositeTexture::Node* createNode(CompositeTexture::NodeType type, CompositeTexture& resource, IAllocator& allocator) {
	CompositeTexture::Node* node = nullptr;
	switch (type) {
		case CompositeTexture::NodeType::OUTPUT: node = LUMIX_NEW(allocator, OutputNode)(allocator); break;
		case CompositeTexture::NodeType::INPUT: node = LUMIX_NEW(allocator, InputNode)(allocator); break;
		case CompositeTexture::NodeType::FLIP: node = LUMIX_NEW(allocator, FlipNode)(allocator); break;
		case CompositeTexture::NodeType::INVERT: node = LUMIX_NEW(allocator, InvertNode)(allocator); break;
		case CompositeTexture::NodeType::COLOR: node = LUMIX_NEW(allocator, ColorNode)(allocator); break;
		case CompositeTexture::NodeType::SPLIT: node = LUMIX_NEW(allocator, SplitNode)(allocator); break;
		case CompositeTexture::NodeType::MERGE: node = LUMIX_NEW(allocator, MergeNode)(allocator); break;
		case CompositeTexture::NodeType::GAMMA: node = LUMIX_NEW(allocator, GammaNode)(allocator); break;
		case CompositeTexture::NodeType::CROP: node = LUMIX_NEW(allocator, CropNode)(allocator); break;
		case CompositeTexture::NodeType::CONTRAST: node = LUMIX_NEW(allocator, ContrastNode)(allocator); break;
		case CompositeTexture::NodeType::BRIGHTNESS: node = LUMIX_NEW(allocator, BrightnessNode)(allocator); break;
		case CompositeTexture::NodeType::RESIZE: node = LUMIX_NEW(allocator, ResizeNode)(allocator); break;
		case CompositeTexture::NodeType::SPLAT: node = LUMIX_NEW(allocator, SplatNode)(allocator); break;
		case CompositeTexture::NodeType::CELLULAR_NOISE: node = LUMIX_NEW(allocator, CellularNoiseNode)(allocator); break;
		case CompositeTexture::NodeType::GRADIENT_NOISE: node = LUMIX_NEW(allocator, GradientNoiseNode)(allocator); break;
		case CompositeTexture::NodeType::WAVE_NOISE: node = LUMIX_NEW(allocator, WaveNoiseNode)(allocator); break;
		case CompositeTexture::NodeType::BLUR: node = LUMIX_NEW(allocator, BlurNode)(allocator); break;
		case CompositeTexture::NodeType::NORMALMAP: node = LUMIX_NEW(allocator, NormalmapNode)(allocator); break;
		case CompositeTexture::NodeType::TWIRL: node = LUMIX_NEW(allocator, TwirlNode)(allocator); break;
		case CompositeTexture::NodeType::WARP: node = LUMIX_NEW(allocator, WarpNode)(allocator); break;
		case CompositeTexture::NodeType::CHECKERBOARD: node = LUMIX_NEW(allocator, CheckerboardNode)(allocator); break;
		case CompositeTexture::NodeType::TRIANGLE: node = LUMIX_NEW(allocator, TriangleNode)(allocator); break;
		case CompositeTexture::NodeType::SQUARE: node = LUMIX_NEW(allocator, SquareNode)(allocator); break;
		case CompositeTexture::NodeType::CIRCLE: node = LUMIX_NEW(allocator, CircleNode)(allocator); break;
		case CompositeTexture::NodeType::SET_ALPHA: node = LUMIX_NEW(allocator, SetAlphaNode)(allocator); break;
		case CompositeTexture::NodeType::CURVE: node = LUMIX_NEW(allocator, CurveNode)(allocator); break;
		case CompositeTexture::NodeType::GRAYSCALE: node = LUMIX_NEW(allocator, GrayscaleNode)(allocator); break;
		case CompositeTexture::NodeType::CONSTANT: node = LUMIX_NEW(allocator, ConstantNode)(allocator); break;
		case CompositeTexture::NodeType::MULTIPLY: node = LUMIX_NEW(allocator, MultiplyNode)(allocator); break;
		case CompositeTexture::NodeType::MIX: node = LUMIX_NEW(allocator, MixNode)(allocator); break;
		case CompositeTexture::NodeType::GRADIENT: node = LUMIX_NEW(allocator, GradientNode)(allocator); break;
		case CompositeTexture::NodeType::VALUE_NOISE: node = LUMIX_NEW(allocator, RandomPixelsNode)(allocator); break;
		case CompositeTexture::NodeType::SHARPEN: node = LUMIX_NEW(allocator, SharpenNode)(allocator); break;
		case CompositeTexture::NodeType::GRADIENT_MAP: node = LUMIX_NEW(allocator, GradientMapNode)(allocator); break;
		case CompositeTexture::NodeType::CIRCULAR_SPLATTER: node = LUMIX_NEW(allocator, CircularSplatterNode)(allocator); break;
		case CompositeTexture::NodeType::PIXEL_PROCESSOR: node = LUMIX_NEW(allocator, PixelProcessorNode)(allocator); break;
		case CompositeTexture::NodeType::SPLATTER: node = LUMIX_NEW(allocator, SplatterNode)(allocator); break;
		case CompositeTexture::NodeType::TRANSLATE: node = LUMIX_NEW(allocator, TranslateNode)(allocator); break;
		case CompositeTexture::NodeType::STATIC_SWITCH: node = LUMIX_NEW(allocator, StaticSwitchNode)(allocator); break;
		case CompositeTexture::NodeType::STEP: node = LUMIX_NEW(allocator, StepNode)(allocator); break;
		case CompositeTexture::NodeType::PIXEL_COLOR: node = LUMIX_NEW(allocator, PixelNode<CompositeTexture::NodeType::PIXEL_COLOR>)(allocator); break;
		case CompositeTexture::NodeType::PIXEL_X: node = LUMIX_NEW(allocator, PixelNode<CompositeTexture::NodeType::PIXEL_X>)(allocator); break;
		case CompositeTexture::NodeType::PIXEL_Y: node = LUMIX_NEW(allocator, PixelNode<CompositeTexture::NodeType::PIXEL_Y>)(allocator); break;
		case CompositeTexture::NodeType::PIXEL_CTX_W: node = LUMIX_NEW(allocator, PixelNode<CompositeTexture::NodeType::PIXEL_CTX_W>)(allocator); break;
		case CompositeTexture::NodeType::PIXEL_CTX_H: node = LUMIX_NEW(allocator, PixelNode<CompositeTexture::NodeType::PIXEL_CTX_H>)(allocator); break;
		case CompositeTexture::NodeType::DIVIDE: node = LUMIX_NEW(allocator, MathNode<CompositeTexture::NodeType::DIVIDE>)(allocator); break;
		case CompositeTexture::NodeType::MAX: node = LUMIX_NEW(allocator, MathNode<CompositeTexture::NodeType::MAX>)(allocator); break;
		case CompositeTexture::NodeType::MIN: node = LUMIX_NEW(allocator, MathNode<CompositeTexture::NodeType::MIN>)(allocator); break;
	}
	if (!node) return nullptr;
	node->m_resource = &resource;
	return node;
}

} // anonymous namespace

CompositeTexture::CompositeTexture(StudioApp& app, IAllocator& allocator)
	: m_allocator(allocator)
	, m_app(app)
	, m_nodes(allocator)
	, m_links(allocator)
{}

CompositeTexture::~CompositeTexture() {
	clear();
}

void CompositeTexture::initDefault() {
	CompositeTexture::Node* output_node = addNode(CompositeTexture::NodeType::OUTPUT);
	CompositeTexture::Node* const_node = addNode(CompositeTexture::NodeType::COLOR);
	CompositeTexture::Link& link = m_links.emplace();
	const_node->m_pos = ImVec2(100, 100);
	output_node->m_pos = ImVec2(300, 100);
	link.from = const_node->m_id;
	link.to = output_node->m_id;
}

void CompositeTexture::clear() {
	m_links.clear();
	Renderer* renderer = (Renderer*)m_app.getEngine().getSystemManager().getSystem("renderer");
	for (Node* n : m_nodes) {
		if (n->m_preview) renderer->getEndFrameDrawStream().destroy(n->m_preview);
		LUMIX_DELETE(m_allocator, n);
	}
	m_nodes.clear();
	m_node_id_generator = 1;
}

bool CompositeTexture::loadSync(FileSystem& fs, const Path& path) {
	clear();

	OutputMemoryStream data(m_allocator);
	if (!fs.getContentSync(path, data)) return false;

	InputMemoryStream blob(data);
	deserialize(blob);
	return true;
}

bool CompositeTexture::save(FileSystem& fs, const Path& path) {
	OutputMemoryStream blob(m_allocator);
	serialize(blob);
	return fs.saveContentSync(path, blob);
}

struct CompositeTextureHeader {
	static constexpr u32 MAGIC = '_LTC';
	u32 magic = MAGIC;
	u32 version = 0;
};

void CompositeTexture::serialize(OutputMemoryStream& blob) {
	CompositeTextureHeader header;
	blob.write(header);
	blob.write(m_node_id_generator);
	blob.write(m_nodes.size());
	for (const Node* node : m_nodes) {
		blob.write(node->getType());
		blob.write(node->m_id);
		blob.write(node->m_pos);
		node->serialize(blob);
	}
	blob.write(m_links.size());
	for (const Link& link : m_links) {
		blob.write(link.from);
		blob.write(link.to);
	}
}

void CompositeTexture::initTerrainAlbedo() {
	OutputNode* onode = (OutputNode*)addNode(NodeType::OUTPUT);
	InputNode* inode0 = (InputNode*)addNode(NodeType::INPUT);
	InputNode* inode1 = (InputNode*)addNode(NodeType::INPUT);
	onode->m_layers_count = 2;
	onode->m_output_type = OutputNode::OutputType::ARRAY;
	inode0->m_texture = "textures/common/red.tga";
	inode1->m_texture = "textures/common/green.tga";
	link(inode0, 0, onode, 0);
	link(inode1, 0, onode, 1);
}

void CompositeTexture::link(Node* from, u32 from_pin, Node* to, u32 to_pin) {
	Link& link = m_links.emplace();
	link.from = from->m_id | (from_pin << 16);
	link.to = to->m_id | (to_pin << 16);
}

void CompositeTexture::initTerrainNormal() {
	OutputNode* onode = (OutputNode*)addNode(NodeType::OUTPUT);
	InputNode* inode0 = (InputNode*)addNode(NodeType::INPUT);
	InputNode* inode1 = (InputNode*)addNode(NodeType::INPUT);
	onode->m_layers_count = 2;
	onode->m_output_type = OutputNode::OutputType::ARRAY;
	inode0->m_texture = "textures/common/default_normal.tga";
	inode1->m_texture = "textures/common/default_normal.tga";
	link(inode0, 0, onode, 0);
	link(inode1, 0, onode, 1);
}

void CompositeTexture::removeArrayLayer(u32 idx) {
	OutputNode* node = (OutputNode*)m_nodes[0];
	const Node::Input input = node->getInput(idx);
	if (!input) return;
	m_links.eraseItems([&](const Link& link){ return link.getFromNode() == input.node->m_id; });
	LUMIX_DELETE(m_allocator, input.node);
	m_nodes.eraseItem(input.node);
	--node->m_layers_count;
	for (Link& link : m_links) {
		if (link.getToNode() == node->m_id && link.getToPin() > idx) {
			link.to = link.getToNode() | ((link.getToPin() - 1) << 16);
		}
	}

}

void CompositeTexture::addArrayLayer(const Path& path) {
	OutputNode* node = (OutputNode*)m_nodes[0];
	if (node->m_output_type != OutputNode::OutputType::ARRAY) return;
	InputNode* inode = (InputNode*)addNode(NodeType::INPUT);
	inode->m_texture = path;
	Link& link = m_links.emplace();
	link.from = inode->m_id;
	link.to = node->m_id | (node->m_layers_count << 16);
	++node->m_layers_count;
}

static void colorLinks(Array<CompositeTexture::Link>& links) {
	const ImU32 colors[] = {
		IM_COL32(0x20, 0x20, 0xA0, 0xFF),
		IM_COL32(0x20, 0xA0, 0x20, 0xFF),
		IM_COL32(0x20, 0xA0, 0xA0, 0xFF),
		IM_COL32(0xA0, 0x20, 0x20, 0xFF),
		IM_COL32(0xA0, 0x20, 0xA0, 0xFF),
		IM_COL32(0xA0, 0xA0, 0x20, 0xFF),
		IM_COL32(0xA0, 0xA0, 0xA0, 0xFF),
	};
	
	for (i32 i = 0, c = links.size(); i < c; ++i) {
		CompositeTexture::Link& l = links[i];
		l.color = colors[i % lengthOf(colors)];
	}
}

static void copy(CompositeTexture::Image& dst, const CompositeTexture::Image& src) {
	dst.init(src.w, src.h, src.channels);
	memcpy(dst.pixels.data(), src.pixels.data(), src.pixels.byte_size());
}

bool CompositeTexture::generate(Result* result) {
	OutputNode* node = (OutputNode*)m_nodes[0];
	if (!node->generate()) return false;

	switch(node->m_output_type) {
		case OutputNode::OutputType::SIMPLE: {
			result->is_cubemap = false;
			Image& pd = result->layers.emplace(m_allocator);
			copy(pd, node->getInputImage(0));
			break;
		}
		case OutputNode::OutputType::CUBEMAP: {
			result->is_cubemap = true;
			for (u32 i = 0; i < 6; ++i) {
				Image& pd = result->layers.emplace(m_allocator);
				copy(pd, node->getInputImage(i));
			}
			break;
		}
		case OutputNode::OutputType::ARRAY: {
			result->is_cubemap = false;
			for (u32 i = 0; i < node->m_layers_count; ++i) {
				Image& pd = result->layers.emplace(m_allocator);
				copy(pd, node->getInputImage(i));
			}
			break;
		}
	}

	for (Image& pd : result->layers) {
		if (pd.channels != node->m_channels_count) {
			Array<float> tmp(m_allocator);
			const u32 n = node->m_channels_count;
			tmp.resize(pd.w * pd.h * n);
			for (u32 i = 0; i < pd.w * pd.h; ++i) {
				for (u32 ch = 0; ch < n; ++ch) {
					tmp[i * n + ch] = ch < pd.channels ? pd.pixels[i * pd.channels + ch] : ((pd.channels == 1 && ch < 3) ? pd.pixels[i * pd.channels] : 1.f);
				}
			}
			pd.pixels = tmp.move();
			pd.channels = n;
		}
	}

	colorLinks(m_links);
	markReachable(*this);
	return true;
}


bool CompositeTexture::deserialize(InputMemoryStream& blob) {
	CompositeTextureHeader header;
	blob.read(header);
	if (header.magic != CompositeTextureHeader::MAGIC) return false;
	if (header.version > 0) return false;
	blob.read(m_node_id_generator);
	u32 count;
	blob.read(count);
	for (u32 i = 0; i < count; ++i) {
		NodeType type;
		blob.read(type);
		Node* node = addNode(type);
		blob.read(node->m_id);
		blob.read(node->m_pos);
		node->deserialize(blob);
	}
	blob.read(count);
	for (u32 i = 0; i < count; ++i) {
		Link& link = m_links.emplace();
		blob.read(link.from);
		blob.read(link.to);
	}
	colorLinks(m_links);
	markReachable(*this);
	CompositeTexture::Result img(m_allocator);
	generate(&img);
	return true;
}

CompositeTexture::Node* CompositeTexture::addNode(CompositeTexture::NodeType type) {
	CompositeTexture::Node* node = createNode(type, *this, m_allocator);
	if (!node) return nullptr;
	node->m_id = ++m_node_id_generator;
	m_nodes.push(node);
	return node;
}

u32 CompositeTexture::getLayersCount() const {
	OutputNode* node = (OutputNode*)m_nodes[0];
	switch(node->m_output_type) {
		case OutputNode::OutputType::SIMPLE: return 1;
		case OutputNode::OutputType::CUBEMAP: return 6;
		case OutputNode::OutputType::ARRAY: return node->m_layers_count;
	}
	ASSERT(false);
	return 1;
}

struct CompositeTextureEditorImpl : CompositeTextureEditor, NodeEditor {
	CompositeTextureEditorImpl(const Path& path, StudioApp& app, IAllocator& allocator)
		: NodeEditor(allocator)
		, m_allocator(allocator)
		, m_app(app)
		, m_resource(app, m_allocator)
	{
		FileSystem& fs = m_app.getEngine().getFileSystem();
		m_path = path;
		m_loading_handle = fs.getContent(path, makeDelegate<&CompositeTextureEditorImpl::onLoaded>(this));
	}

	~CompositeTextureEditorImpl() {
		FileSystem& fs = m_app.getEngine().getFileSystem();
		if (m_loading_handle.isValid()) fs.cancel(m_loading_handle);
	}

	struct INodeTypeVisitor {
		virtual bool beginCategory(const char* category) { return true; }
		virtual void endCategory() {}
		virtual INodeTypeVisitor& visitType(const char* label, CompositeTexture::NodeType type, char shortcut = 0) = 0;
	};

	void visitNodeTypes(INodeTypeVisitor& visitor) {
		if (visitor.beginCategory("Generate")) {
			visitor
				.visitType("Checkerboard", CompositeTexture::NodeType::CHECKERBOARD)
				.visitType("Circle", CompositeTexture::NodeType::CIRCLE, 'O')
				.visitType("Circular splatter", CompositeTexture::NodeType::CIRCULAR_SPLATTER)
				.visitType("Gradient", CompositeTexture::NodeType::GRADIENT)
				.visitType("Grid splatter", CompositeTexture::NodeType::SPLATTER)
				.visitType("Square", CompositeTexture::NodeType::SQUARE)
				.visitType("Triangle", CompositeTexture::NodeType::TRIANGLE)
				.endCategory();
		}
		if (visitor.beginCategory("Image")) {
			visitor
				.visitType("Crop", CompositeTexture::NodeType::CROP)
				.visitType("Flip", CompositeTexture::NodeType::FLIP, 'F')
				.visitType("Input", CompositeTexture::NodeType::INPUT)
				.visitType("Resize", CompositeTexture::NodeType::RESIZE, 'R')
				.visitType("Translate", CompositeTexture::NodeType::TRANSLATE)
				.endCategory();
		}
		if (visitor.beginCategory("Math")) {
			visitor
				.visitType("Curve", CompositeTexture::NodeType::CURVE, 'C')
				.visitType("Divide", CompositeTexture::NodeType::DIVIDE, 'D')
				.visitType("Invert", CompositeTexture::NodeType::INVERT, 'I')
				.visitType("Min", CompositeTexture::NodeType::MIN)
				.visitType("Mix", CompositeTexture::NodeType::MIX)
				.visitType("Max", CompositeTexture::NodeType::MAX)
				.visitType("Multiply", CompositeTexture::NodeType::MULTIPLY, 'M')
				.visitType("Step", CompositeTexture::NodeType::STEP)
				.endCategory();
		}
		if (visitor.beginCategory("Misc")) {
			visitor
				.visitType("Blur", CompositeTexture::NodeType::BLUR, 'B')
				.visitType("Color", CompositeTexture::NodeType::COLOR, '4')
				.visitType("Constant", CompositeTexture::NodeType::CONSTANT, '1')
				.visitType("Merge", CompositeTexture::NodeType::MERGE)
				.visitType("Normalmap", CompositeTexture::NodeType::NORMALMAP)
				.visitType("Set alpha", CompositeTexture::NodeType::SET_ALPHA)
				.visitType("Splat", CompositeTexture::NodeType::SPLAT, 'S')
				.visitType("Split", CompositeTexture::NodeType::SPLIT)
				.visitType("Static switch", CompositeTexture::NodeType::STATIC_SWITCH, 'W')
				.visitType("Twirl", CompositeTexture::NodeType::TWIRL)
				.visitType("Warp", CompositeTexture::NodeType::WARP)
				.endCategory();
		}
		if (visitor.beginCategory("Noise")) {
			visitor
				.visitType("Cell noise", CompositeTexture::NodeType::CELLULAR_NOISE)
				.visitType("Gradient noise", CompositeTexture::NodeType::GRADIENT_NOISE)
				.visitType("Value noise", CompositeTexture::NodeType::VALUE_NOISE)
				.visitType("Wave noise", CompositeTexture::NodeType::WAVE_NOISE)
				.endCategory();
		}
		if (visitor.beginCategory("Pixel")) {
			visitor
				.visitType("Color", CompositeTexture::NodeType::PIXEL_COLOR)
				.visitType("Context width", CompositeTexture::NodeType::PIXEL_CTX_W, 'W')
				.visitType("Context height", CompositeTexture::NodeType::PIXEL_CTX_H, 'H')
				.visitType("Processor", CompositeTexture::NodeType::PIXEL_PROCESSOR)
				.visitType("X", CompositeTexture::NodeType::PIXEL_X, 'X')
				.visitType("Y", CompositeTexture::NodeType::PIXEL_Y, 'Y')
				.endCategory();
		}

		visitor.visitType("Brightness", CompositeTexture::NodeType::BRIGHTNESS)
			.visitType("Contrast", CompositeTexture::NodeType::CONTRAST)
			.visitType("Gamma", CompositeTexture::NodeType::GAMMA)
			.visitType("Gradient map", CompositeTexture::NodeType::GRADIENT_MAP, 'G')
			.visitType("Grayscale", CompositeTexture::NodeType::GRAYSCALE)
			.visitType("Sharpen", CompositeTexture::NodeType::SHARPEN);
			
	}

	void onCanvasClicked(ImVec2 pos, i32 hovered_link) override {
		struct : INodeTypeVisitor {
			INodeTypeVisitor& visitType(const char* label, CompositeTexture::NodeType type, char shortcut) override {
				if (!n && shortcut && os::isKeyDown((os::Keycode)shortcut)) {
					n = win->m_resource.addNode(type);
				}
				return *this;
			}
			CompositeTextureEditorImpl* win;
			CompositeTexture::Node* n = nullptr;
		} visitor;
		visitor.win = this;
		visitNodeTypes(visitor);
		if (visitor.n) {
			visitor.n->m_pos = pos;
			if (hovered_link >= 0) splitLink(m_resource.m_nodes.back(), m_resource.m_links, hovered_link);
			pushUndo(NO_MERGE_UNDO);
		}	
	}

	void pushUndo(u32 tag) override {
		m_dirty = true;
		if (tag == NO_MERGE_UNDO) {
			for (CompositeTexture::Node* n : m_resource.m_nodes) {
				n->m_dirty = true;
			}
			m_resource.m_nodes[0]->generate();
		}
		SimpleUndoRedo::pushUndo(tag);
	}

	void onLinkDoubleClicked(NodeEditorLink& link, ImVec2 pos) override {}
	
	void onContextMenu(ImVec2 pos) override {
		m_node_filter.gui("Filter", 150, ImGui::IsWindowAppearing());
		
		if (m_node_filter.isActive()) {
			struct : INodeTypeVisitor {
				bool beginCategory(const char* _category) override {
					category = _category;
					category.append(" / ");
					return true;
				}
				void endCategory() override { category = ""; }

				INodeTypeVisitor& visitType(const char* _label, CompositeTexture::NodeType type, char shortcut) override {
					StaticString<128> label(category, _label);
					if (shortcut) label.append(" (LMB + ", shortcut, ")");
					if (!node && win->m_node_filter.pass(label) && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::MenuItem(label))) {
						node = win->m_resource.addNode(type);
						ImGui::CloseCurrentPopup();
					}
					return *this;
				}
				StaticString<64> category;
				CompositeTextureEditorImpl* win;
				CompositeTexture::Node* node = nullptr;
			} visitor;
			visitor.win = this;
			visitNodeTypes(visitor);
			if (visitor.node) {
				visitor.node->m_pos = pos;
				pushUndo(NO_MERGE_UNDO);
			}		
		}
		else {
			struct : INodeTypeVisitor {
				bool beginCategory(const char* category) override { return ImGui::BeginMenu(category); }
				void endCategory() override { ImGui::EndMenu(); }

				INodeTypeVisitor& visitType(const char* _label, CompositeTexture::NodeType type, char shortcut) override {
					StaticString<64> label(_label);
					if (shortcut) label.append(" (LMB + ", shortcut, ")");
					if (!node && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::MenuItem(label))) {
						node = win->m_resource.addNode(type);
						ImGui::CloseCurrentPopup();
					}
					return *this;
				}
				CompositeTextureEditorImpl* win;
				CompositeTexture::Node* node = nullptr;
			} visitor;
			visitor.win = this;
			visitNodeTypes(visitor);
			if (visitor.node) {
				visitor.node->m_pos = pos;
				pushUndo(NO_MERGE_UNDO);
			}
		}
	}
	
	void deserialize(InputMemoryStream& blob) override {
		m_resource.clear();
		m_resource.deserialize(blob);
	}
	
	void serialize(OutputMemoryStream& blob) override { m_resource.serialize(blob); }

	void deleteUnreachable() {
		m_resource.deleteUnreachable();
		pushUndo(NO_MERGE_UNDO);
	}

	void deleteSelectedNodes() override {
		if (m_is_any_item_active) return;
		m_resource.deleteSelectedNodes();
		pushUndo(NO_MERGE_UNDO);
	}

	void onLoaded(Span<const u8> data, bool success) {
		m_loading_handle = FileSystem::AsyncHandle::invalid();
		if (!success) {
			logError("Failed to load ", m_path);
			return;
		}
		
		InputMemoryStream blob(data);
		m_resource.deserialize(blob);
		pushUndo(NO_MERGE_UNDO);
		m_dirty = false;
	}
	
	void exportAs() {
		char path[MAX_PATH];
		if (!os::getSaveFilename(Span(path), "TGA Image\0*.tga\0", "tga")) return;

		CompositeTexture::Result img(m_allocator);
		if (!m_resource.generate(&img)) {
			logError("Could not generate ", path);
			return;
		}
	
		if (img.is_cubemap) {
			logError("Could not export ", path, " because it's a cubemap");
			return;
		}
	
		if (img.layers.size() != 1) {
			logError("Could not export ", path, " because it's an array");
			return;
		}

		if (img.layers[0].channels != 4) {
			logError("Could not export ", path, " because it's does not have 4 channels");
			return;
		}

		os::OutputFile file;
		if (!file.open(path)) {
			logError("Could not save ", path);
			return;
		}

		OutputMemoryStream pixel8 = img.layers[0].asU8();
		bool res = Texture::saveTGA(&file, img.layers[0].w, img.layers[0].h, gpu::TextureFormat::RGBA8, pixel8.data(), true, Path(path), m_allocator);
		file.close();

		if (!res) {
			logError("Could not save ", path);
		}
	}

	void saveAs(const Path& path) {
		FileSystem& fs = m_app.getEngine().getFileSystem();
		OutputMemoryStream blob(m_allocator);
		m_resource.serialize(blob);
		if (!fs.saveContentSync(path, blob)) {
			logError("Failed to save ", path);
			return;
		}

		m_path = path;
		m_dirty = false;
	}

	void previewGUI() {
		if (!m_show_preview) return;
		
		for (CompositeTexture::Node* n : m_resource.m_nodes) {
			if (n->m_selected) {
				m_preview_node_id = n->m_id;
				break;
			}
		}

		CompositeTexture::Node* preview_node = m_resource.getNodeByID(m_preview_node_id);
		if (!preview_node) return;

		ImVec2 p = ImGui::GetItemRectMin();
		if (preview_node->m_preview && preview_node->m_dirty) {
			Renderer* renderer = (Renderer*)m_resource.m_app.getEngine().getSystemManager().getSystem("renderer");
			renderer->getEndFrameDrawStream().destroy(preview_node->m_preview);
			preview_node->m_preview = gpu::INVALID_TEXTURE;
		}

		if (!preview_node->m_preview) {
			if (preview_node->m_dirty) preview_node->generate();
			if (!preview_node->m_outputs.empty()) {
				Renderer* renderer = (Renderer*)m_resource.m_app.getEngine().getSystemManager().getSystem("renderer");
				if (renderer) {
					const CompositeTexture::Image& pd = preview_node->m_outputs[0];
					gpu::TextureFormat format = gpu::TextureFormat::RGBA32F;
					switch (pd.channels) {
						case 1: format = gpu::TextureFormat::R32F; break;
						case 2: format = gpu::TextureFormat::RG32F; break;
						case 3: format = gpu::TextureFormat::RGB32F; break;
						case 4: format = gpu::TextureFormat::RGBA32F; break;
						default: ASSERT(false);
					}
					Renderer::MemRef mem = renderer->copy(pd.pixels.begin(), pd.pixels.byte_size());
					preview_node->m_preview = renderer->createTexture(pd.w
					, pd.h
					, 1
					, format
					, gpu::TextureFlags::SRGB | gpu::TextureFlags::NO_MIPS
					, mem
					, "composite texture");
				}
			}
		}
		if (preview_node->m_preview && !preview_node->m_outputs.empty()) {
			ImGui::SetCursorScreenPos(p);
			const CompositeTexture::Image& pd = preview_node->m_outputs[0];
			ImVec2 size((float)pd.w, (float)pd.h);
			ImGui::Image(preview_node->m_preview, size);
		}
	}

	void save() override { saveAs(m_path); }

	void menu() override {
		CommonActions& actions = m_app.getCommonActions();
		if (m_app.checkShortcut(actions.undo)) undo();
		else if (m_app.checkShortcut(actions.redo)) redo();
		
		if (ImGui::BeginMenu("Graph")) {
			if (ImGui::MenuItem("Export")) exportAs();
			if (menuItem(actions.undo, canUndo())) undo();
			if (menuItem(actions.redo, canRedo())) redo();
			if (ImGui::MenuItem(ICON_FA_BRUSH "Clear")) deleteUnreachable();
			ImGui::Checkbox("Preview", &m_show_preview);
			ImGui::EndMenu();
		}
	}

	bool isDirty() const override { return m_dirty; }

	void doUndo() override { NodeEditor::undo(); }
	void doRedo() override { NodeEditor::redo(); }

	void gui() override {
		if (m_loading_handle.isValid()) {
			ImGui::TextUnformatted("Loading...");
			return;
		}

		nodeEditorGUI(m_resource.m_nodes, m_resource.m_links);
		previewGUI();
	}
	
	IAllocator& m_allocator;
	StudioApp& m_app;
	Path m_path;
	CompositeTexture m_resource;
	u16 m_preview_node_id = 0xffFF;
	FileSystem::AsyncHandle m_loading_handle = FileSystem::AsyncHandle::invalid();
	bool m_show_preview = true;
	bool m_dirty = false;
	TextFilter m_node_filter;
};

UniquePtr<CompositeTextureEditor> CompositeTextureEditor::open(const Path& path, StudioApp& app, IAllocator& allocator) {
	return UniquePtr<CompositeTextureEditorImpl>::create(allocator, Path(path), app, allocator);
}

} // namespace Lumix
