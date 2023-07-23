#include "composite_texture.h"
#include "engine/crt.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/log.h"
#include "engine/math.h"
#include "engine/os.h"
#include "engine/stream.h"
#include "engine/string.h"
#include "editor/asset_browser.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "renderer/texture.h"
#include "stb/stb_image.h"
#include <math.h>
#include <stb/stb_image_resize.h>

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
	RANDOM_PIXELS,
	CONSTANT,
	RESIZE,
	CIRCLE,
	CELLULAR_NOISE,
	SPLAT,
	SIMPLEX,
	WAVE_NOISE,
	CURVE,
	SET_ALPHA,
	CUT,
	SHARPEN,
	STATIC_SWITCH,
	STEP,
	SPLATTER,
	GRADIENT_MAP
};

enum { OUTPUT_FLAG = 1 << 31 };

void CompositeTexture::Node::inputSlot() {
	ImGuiEx::Pin(m_id | (m_input_counter << 16), true);
	++m_input_counter;
}

void CompositeTexture::Node::outputSlot() {
	ImGuiEx::Pin(m_id | (m_output_counter << 16) | OUTPUT_FLAG, false);
	++m_output_counter;
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
		ImGui::PopStyleColor();
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", m_error.c_str());
	}
	else if (!m_reachable) {
		ImGui::PopStyleColor();
	}
	return res;
}

struct CompositeTexture::Node::Input {
	CompositeTexture::Node* node;
	u32 output_idx;

	operator bool() const { return node; }
	bool getPixelData(CompositeTexture::PixelData* data) const {
		return node->getPixelData(data, output_idx);
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

			LUMIX_DELETE(m_app.getAllocator(), node);
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

			LUMIX_DELETE(m_app.getAllocator(), node);
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

bool CompositeTexture::Node::getInputPixelData(u32 pin_idx, PixelData* pd) {
	const Input input = getInput(pin_idx);
	if (!input) return error("Missing input");
	return input.getPixelData(pd);
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

bool resize(CompositeTexture::PixelData* pd, u32 w, u32 h) {
	if (pd->w == w && pd->h == h) return true;

	OutputMemoryStream tmp(pd->pixels.getAllocator());
	tmp.resize(w * h * pd->channels);
	const i32 res = stbir_resize_uint8(pd->pixels.data(), pd->w, pd->h, 0, tmp.getMutableData(), w, h, 0, pd->channels);
	pd->pixels = tmp;
	pd->w = w;
	pd->h = h;
	return res == 1;
}

void makeSameSize(CompositeTexture::PixelData* a, CompositeTexture::PixelData* b) {
	if (a->w == b->w && a->h == b->h) return;

	if (a->w * a->h > b->w * b->h) {
		resize(b, a->w, a->h);
	}
	else {
		resize(a, b->w, b->h);
	}
}

struct SplitNode final : CompositeTexture::Node {
	SplitNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::SPLIT; }
	
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		CompositeTexture::PixelData tmp(m_resource->m_app.getAllocator());
		if (!getInputPixelData(0, &tmp)) return false;
		if (output_idx >= tmp.channels) return error("Not enough channels");

		data->channels = 1;
		data->w = tmp.w;
		data->h = tmp.h;
		data->pixels.resize(tmp.w * tmp.h);
		u8* dst = data->pixels.getMutableData();
		const u8* src = tmp.pixels.data();
		for (u32 i = 0; i < tmp.w * tmp.h; ++i) {
			dst[i] = src[i * tmp.channels + output_idx];
		}
		return true;
	}

	bool gui() override {
		ImGuiEx::NodeTitle("Split");
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

	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		const Input inputs[] = { getInput(0), getInput(1), getInput(2), getInput(3) };
		u32 channels_count = 0;
		for (u32 i = 0; i < 4; ++i) {
			if (!inputs[i]) break;
			++channels_count;
		}
		
		for (u32 i = channels_count; i < 4; ++i) {
			if (inputs[i]) return error("Missing input");	
		}

		CompositeTexture::PixelData first_pd(m_resource->m_app.getAllocator());
		if (!inputs[0].getPixelData(&first_pd)) return false;
		if (first_pd.channels != 1) return error("Incorrect number of channels");

		data->w = first_pd.w;
		data->h = first_pd.h;
		data->channels = channels_count;
		data->pixels.resize(channels_count * first_pd.w * first_pd.h);
		u8* dst = data->pixels.getMutableData();
		const u8* first_src = first_pd.pixels.data();
		for (u32 i = 0; i < first_pd.w * first_pd.h; ++i) {
			dst[i * channels_count] = first_src[i];
		}

		for (u32 i = 1; i < channels_count; ++i) {
			CompositeTexture::PixelData tmp(m_resource->m_app.getAllocator());
			if (!inputs[i].getPixelData(&tmp)) return false;
			if (tmp.channels != 1) return error("Incorrect number of channels");
			resize(&tmp, first_pd.w, first_pd.h);
			
			const u8* src = tmp.pixels.data();
			for (u32 j = 0; j < tmp.w * tmp.h; ++j) {
				dst[j * channels_count + i] = src[j];
			}
		}

		return true;
	}

	bool gui() override {
		ImGuiEx::NodeTitle("Merge");
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

	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		data->w = 4;
		data->h = 4;
		data->channels = 1;
		data->pixels.reserve(4 * 4);
		for (u32 i = 0; i < 16; ++i) {
			data->pixels.write(u8(value));
		}
		return true;
	}

	bool gui() override {
		ImGuiEx::NodeTitle("Constant");
		outputSlot();
		return ImGui::SliderInt("Value", &value, 0, 255);
	}

	i32 value = 0xff;
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

	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		data->w = 4;
		data->h = 4;
		data->channels = 4;
		data->pixels.reserve(4 * 4 * 4);
		const u8 u8color[] = { u8(color.x * 0xff + 0.5f), u8(color.y * 0xff + 0.5f), u8(color.z * 0xff + 0.5f), u8(color.w * 0xff + 0.5f) };
		for (u32 i = 0; i < 16; ++i) {
			data->pixels.write(u8color, sizeof(u8color));
		}
		return true;
	}

	bool gui() override {
		ImGuiEx::NodeTitle("Color");
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
	
	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		if (!getInputPixelData(0, data)) return false;

		u8* ptr = data->pixels.getMutableData();
		if (horizontal) {
			for (u32 j = 0; j < data->h; ++j) {
				for (u32 i = 0; i < data->w / 2; ++i) {
					u32 tmp;
					u8* p0 = ptr + (i + j * data->w) * data->channels;
					u8* p1 = ptr + (data->w - i - 1 + j * data->w) * data->channels;
				
					memcpy(&tmp, p0, data->channels);
					memcpy(p0, p1, data->channels);
					memcpy(p1, &tmp, data->channels);
				}
			}
			return true;
		}

		for (u32 j = 0; j < data->h / 2; ++j) {
			for (u32 i = 0; i < data->w; ++i) {
				u32 tmp;
				u8* p0 = ptr + (i + j * data->w) * data->channels;
				u8* p1 = ptr + (i + (data->h - j - 1) * data->w) * data->channels;
				
				memcpy(&tmp, p0, data->channels);
				memcpy(p0, p1, data->channels);
				memcpy(p1, &tmp, data->channels);
			}
		}
		return true;
	}
	
	bool gui() override {
		ImGuiEx::NodeTitle("Flip");
		inputSlot();
		bool res = ImGui::Checkbox("Horizontal", &horizontal);
		ImGui::SameLine();
		outputSlot();
		return res;
	}

	bool horizontal = false;
};

struct RandomPixelsNode final : CompositeTexture::Node {
	RandomPixelsNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::RANDOM_PIXELS; }
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

	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		RandomGenerator rng(seed);
		data->w = w;
		data->h = h;
		data->channels = 1;
		data->pixels.resize(w * h);
		for (u32 i = 0; i < w * h; ++i) {
			data->pixels[i] = u8(rng.rand() % 256);
		}
		return true;
	}

	bool gui() override {
		ImGuiEx::NodeTitle("Random pixels");
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
	
	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		data->pixels.resize(size);
		for (u32 i = 0; i < size; ++i) {
			data->pixels[i] = 255 * i / (size - 1);
		}
		data->channels = 1;
		data->w = size;
		data->h = 1;
		return true;
	}
	
	bool gui() override {
		ImGuiEx::NodeTitle("Gradient");
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
	
	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		if (!getInputPixelData(0, data)) return false;

		for (u32 i = 0, c = (u32)data->pixels.size(); i < c; ++i) {
			const bool is_alpha = data->channels == 4 && i % 4 == 3;
			if (is_alpha) continue;

			float v = data->pixels[i] / 255.f;
			v = powf(v, 1 / gamma);
			v = clamp(v * 255.f, 0.f, 255.f);
			data->pixels[i] = u8(v + 0.5f);
		}
		return true;
	}
	
	bool gui() override {
		ImGuiEx::NodeTitle("Gamma");
		inputSlot();
		ImGui::SetNextItemWidth(150);
		bool res = ImGui::DragFloat("##v", &gamma);
		ImGui::SameLine();
		outputSlot();
		return res;
	}

	float gamma = 2.2f;
};

struct MultiplyNode final : CompositeTexture::Node {
	MultiplyNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::MULTIPLY; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }
	
	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		CompositeTexture::PixelData tmp(m_resource->m_app.getAllocator());
		if (!getInputPixelData(0, data)) return false;
		if (!getInputPixelData(1, &tmp)) return false;
		if (tmp.channels != data->channels) return error("Number of channel does not match");
		makeSameSize(&tmp, data);

		for (u32 i = 0, c = (u32)data->pixels.size(); i < c; ++i) {
			const float a = tmp.pixels[i];
			const float b = data->pixels[i];
			float v = (a * b) / 255.f;
			v = clamp(v, 0.f, 255.f);
			data->pixels[i] = u8(v + 0.5f);
		}
		return true;
	}

	bool gui() override {
		ImGuiEx::NodeTitle("Multiply");
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
	
	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		if (!getInputPixelData(0, data)) return false;
		
		const u32 w = type == Type::PIXELS ? size.x : u32(data->w * scale.x * 0.01f + 0.5f);
		const u32 h = type == Type::PIXELS ? size.y : u32(data->h * scale.y * 0.01f + 0.5f);
		if (data->w == w && data->h == h) return true;

		OutputMemoryStream tmp(m_resource->m_app.getAllocator());
		tmp.resize(w * h * data->channels);

		const i32 res = stbir_resize_uint8(data->pixels.data(), data->w, data->h, 0, tmp.getMutableData(), w, h, 0, data->channels);
		data->w = w;
		data->h = h;
		data->pixels = tmp;
		if (res != 1) return error("Failed to resize image");
		return true;
	}

	bool gui() override {
		ImGuiEx::NodeTitle("Resize");
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
		const float kF = magic;

		Vec2 i = floor(p);
		Vec2 f = fract(p);
		f = f * f * (f * -2.f + 3.f);
		return mix(mix(sinf(kF * dot(p, hash(i + Vec2(0.f, 0.f)))),
		               sinf(kF * dot(p, hash(i + Vec2(1.f, 0.f)))), f.x),
		               mix(sinf(kF * dot(p, hash(i + Vec2(0.f, 1.f)))),
		                   sinf(kF * dot(p, hash(i + Vec2(1.f, 1.f)))), f.x), f.y);
	}

	bool gui() override {
		ImGuiEx::NodeTitle("Wave noise");
		outputSlot();
		bool res = ImGui::DragFloat("Scale", &scale, 0.01f, FLT_MIN, FLT_MAX);
		res = ImGui::DragFloat("Magic", &magic, 0.01f, 1, 12) || res;
		res = ImGui::DragFloat("Offset", &offset, 0.01f, FLT_MIN, FLT_MAX) || res;
		res = ImGui::DragInt("Width", (i32*)&w, 1, 1, 999999) || res;
		res = ImGui::DragInt("Height", (i32*)&h, 1, 1, 999999) || res;
		return res;
	}
	
	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		data->w = w;
		data->h = h;
		data->channels = 1;
		data->pixels.resize(w * h);
		for (u32 j = 0; j < h; ++j) {
			const float v = j / float(h - 1);
			for (u32 i = 0; i < w; ++i) {
				float u = i / float(w - 1);
				float d = noise(Vec2(u, v) * scale + offset) * 0.5f + 0.5f;
				d = clamp(d * 255.f, 0.f, 255.f);
				data->pixels[i + j * w] = u8(d + 0.5f);
			}
		}
		return true;
	}

	void serialize(OutputMemoryStream& blob) const override {
		blob.write(w);
		blob.write(h);
		blob.write(scale);
		blob.write(magic);
		blob.write(offset);
	}

	void deserialize(InputMemoryStream& blob) override {
		blob.read(w);
		blob.read(h);
		blob.read(scale);
		blob.read(magic);
		blob.read(offset);
	}

	u32 w = 256;
	u32 h = 256;
	float scale = 4;
	float magic = 6.f;
	float offset = 0;
};

struct SimplexNode final : CompositeTexture::Node {
	SimplexNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::SIMPLEX; }

	bool hasInputPins() const override { return false; }
	bool hasOutputPins() const override { return true; }

	static Vec2 hash2(Vec2 p) { return hash(p) * 2 - Vec2(1); }

	static float step(float edge, float x) {
		return x < edge ? 0.f : 1.f;
	}

	// https://www.shadertoy.com/view/Msf3WH
	float noise(Vec2 p) {
		static const float K1 = (sqrtf(3) - 1) / 2;
		static const float K2 = (3 - sqrtf(3)) / 6;
	
		Vec2 i = floor(p + (p.x + p.y) * K1);
		Vec2 a = p - i + (i.x + i.y) * K2;
		float m = step(a.y, a.x);
		Vec2 o = Vec2(m, 1.f - m);
		Vec2 b = a - o + K2;
		Vec2 c = a - 1.f + 2.f * K2;
		Vec3 h = maximum(Vec3(0.5f) - Vec3(dot(a, a), dot(b, b), dot(c, c)), Vec3(0.f));
		Vec3 n = h * h * h * h * Vec3(dot(a, hash2(i)), dot(b, hash2(i + o)), dot(c, hash2(i + 1.f)));
		return dot(n, Vec3(70.f)) * 0.5f + 0.5f;
	}

	bool gui() override {
		ImGuiEx::NodeTitle("Simplex");
		outputSlot();
		bool res = ImGui::DragFloat("Scale", &scale, 0.01f, FLT_MIN, FLT_MAX);
		res = ImGui::DragInt("Width", (i32*)&w, 1, 1, 999999) || res;
		res = ImGui::DragInt("Height", (i32*)&h, 1, 1, 999999) || res;
		return res;
	}
	
	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		data->w = w;
		data->h = h;
		data->channels = 1;
		data->pixels.resize(w * h);
		for (u32 j = 0; j < h; ++j) {
			const float v = j / float(h - 1);
			for (u32 i = 0; i < w; ++i) {
				float u = i / float(w - 1);
				float d = noise(Vec2(u, v) * scale);
				d = clamp(d * 255.f, 0.f, 255.f);
				data->pixels[i + j * w] = u8(d + 0.5f);
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
		ImGuiEx::NodeTitle("Cellular noise");
		outputSlot();
		bool res = ImGui::DragFloat("Scale", &scale, 0.01f, FLT_MIN, FLT_MAX);
		res = ImGui::DragFloat("Offset", &offset, 0.01f, FLT_MIN, FLT_MAX) || res;
		res = ImGui::DragInt("Width", (i32*)&w, 1, 1, 999999) || res;
		res = ImGui::DragInt("Height", (i32*)&h, 1, 1, 999999) || res;
		return res;
	}
	
	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		data->w = w;
		data->h = h;
		data->channels = 1;
		data->pixels.resize(w * h);
		for (u32 j = 0; j < h; ++j) {
			const float v = j / float(h - 1);
			for (u32 i = 0; i < w; ++i) {
				float u = i / float(w - 1);
				float d = voronoi(Vec2(u, v) * scale).x;
				d = clamp(d * 255.f, 0.f, 255.f);
				data->pixels[i + j * w] = u8(d + 0.5f);
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

	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		CompositeTexture::PixelData rgb(m_resource->m_app.getAllocator());
		CompositeTexture::PixelData a(m_resource->m_app.getAllocator());
		
		if (!getInputPixelData(0, &rgb)) return false;
		if (!getInputPixelData(1, &a)) return false;
		if (rgb.channels < 3) return error("Input must have at least 3 channels");
		makeSameSize(&rgb, &a);
		data->w = rgb.w;
		data->h = rgb.h;
		data->channels = 4;
		data->pixels.resize(data->w * data->h * 4);
		
		for (u32 i = 0; i < data->w * data->h; ++i) {
			memcpy(&data->pixels[i * 4], &rgb.pixels[i * rgb.channels], rgb.channels);
			data->pixels[i * 4 + 3] = a.pixels[i * a.channels + a.channels - 1];
		}
		return true;
	}

	bool gui() override {
		ImGuiEx::NodeTitle("Set alpha");
		ImGui::BeginGroup();
		inputSlot(); ImGui::TextUnformatted("RGB");
		inputSlot(); ImGui::TextUnformatted("A");
		ImGui::EndGroup();
		ImGui::SameLine();
		outputSlot();
		return false;
	}
};

struct CutNode final : CompositeTexture::Node {
	CutNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::CUT; }
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
	
	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		if (!getInputPixelData(0, data)) return false;
		if (x + w > data->w) return error("Out of bounds access");
		if (y + h > data->h) return error("Out of bounds access");

		OutputMemoryStream tmp(m_resource->m_app.getAllocator());
		tmp.resize(w * h * data->channels);
		for (u32 j = 0; j < h; ++j) {
			for (u32 i = 0; i < w; ++i) {
				memcpy(&tmp[(i + j * w) * data->channels]
				, &data->pixels[((x + i) + (y + j) * data->w) * data->channels]
				, data->channels);
			}
		}
		data->pixels = static_cast<OutputMemoryStream&&>(tmp);
		data->w = w;
		data->h = h;
		return true;
	}

	bool gui() override {
		ImGuiEx::NodeTitle("Cut");
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

	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		if (m_is_on) return getInputPixelData(0, data);
		return getInputPixelData(1, data);
	}
	
	bool gui() override {
		ImGuiEx::NodeTitle("Switch");
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

	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		CompositeTexture::PixelData tmp(m_resource->m_app.getAllocator());
		if (!getInputPixelData(0, &tmp)) return false;

		data->w = tmp.w;
		data->h = tmp.h;
		data->channels = tmp.channels;
		data->pixels.resize(data->w * data->h * data->channels);

		for (i32 j = 0; j < (i32)data->h; ++j) {
			for (i32 i = 0; i < (i32)data->w; ++i) {
				for (u32 ch = 0; ch < data->channels; ++ch) {
					float v = tmp.pixels[(i + j * tmp.w) * tmp.channels + ch];
					v = v < m_value[ch] * 255.f ? 0.f : 255.f;
					data->pixels[(i + j * data->w) * data->channels + ch] = u8(v + 0.5f);
				}
			}
		}
		return true;
	}
	
	bool gui() override {
		ImGuiEx::NodeTitle("Step");
		inputSlot();
		outputSlot();
		ImGui::ColorEdit4("Value", &m_value.x);
		return false;
	}

	Vec4 m_value = Vec4(1, 1, 1, 1);
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

	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		CompositeTexture::PixelData input(m_resource->m_app.getAllocator());
		if (!getInputPixelData(0, &input)) return error("Invalid input");
		if (input.channels != 1) return error("Input must have only 1 channel");

		data->channels = 4;
		data->w = input.w;
		data->h = input.h;
		data->pixels.resize(data->channels * data->w * data->h);

		for (u32 i = 0, c = (u32)data->pixels.size(); i < c; i += data->channels) {
			const float v = input.pixels[i / 4] / 255.f;
			
			for (i32 k = 1; k < m_count; ++k) {
				if (v <= m_keys[k]) {
					float t = (v - m_keys[k - 1]) / (m_keys[k] - m_keys[k - 1]);
					const Vec4 color = lerp(m_values[k - 1], m_values[k], t);
					data->pixels[i + 0] = u8(clamp(color.x * 255.f, 0.f, 255.f) + 0.5f);
					data->pixels[i + 1] = u8(clamp(color.y * 255.f, 0.f, 255.f) + 0.5f);
					data->pixels[i + 2] = u8(clamp(color.z * 255.f, 0.f, 255.f) + 0.5f);
					data->pixels[i + 3] = u8(clamp(color.w * 255.f, 0.f, 255.f) + 0.5f);
					break;
				}
			}
		}
		return true;
	}
	
	bool gui() override {
		ImGuiEx::NodeTitle("Gradient map");
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


struct SplatterNode final : CompositeTexture::Node {
	SplatterNode(IAllocator& allocator) : Node(allocator) {}

	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::SPLATTER; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	void serialize(OutputMemoryStream& blob) const override {
		blob.write(w);
		blob.write(h);
		blob.write(x_count);
		blob.write(y_count);
		blob.write(x_spread);
		blob.write(y_spread);
	}

	void deserialize(InputMemoryStream& blob) override {
		blob.read(w);
		blob.read(h);
		blob.read(x_count);
		blob.read(y_count);
		blob.read(x_spread);
		blob.read(y_spread);
	}

	void blit(CompositeTexture::PixelData& dst, CompositeTexture::PixelData& src, i32 dst_x, i32 dst_y) {
		if (dst_x >= (i32)dst.w) return;
		if (dst_y >= (i32)dst.h) return;
		ASSERT(dst.channels == src.channels);
		
		for (i32 y = maximum(0, -dst_y); y < (i32)src.h && y + dst_y < (i32)dst.h; ++y) {
			for (i32 x = maximum(0, -dst_x); x < (i32)src.w && x + dst_x < (i32)dst.w; ++x) {
				i32 src_pixel = (x + y * src.w) * src.channels;
				float alpha = src.channels < 4 ? 1.f : src.pixels[src_pixel + 3] / 255.f;
				for (u32 ch = 0; ch < dst.channels; ++ch) {
					u8& dst_p = dst.pixels[(x + dst_x + (y + dst_y) * dst.w) * dst.channels + ch];
					dst_p = u8(src.pixels[src_pixel + ch] * alpha + dst_p * (1 - alpha) + 0.5f);
				}
			}
		}
	}

	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		if (!getInputPixelData(0, data)) return error("Missing input");
		
		CompositeTexture::PixelData pattern(m_resource->m_app.getAllocator());
		if (!getInputPixelData(1, &pattern)) return error("Missing input");

		for (u32 j = 0; j < y_count; ++j) {
			for (u32 i = 0; i < x_count; ++i) {
				i32 x = i32(((float)i / x_count) * w);
				i32 y = i32(((float)j / y_count) * h);
				x += rand(0, 2 * x_spread) - x_spread;
				y += rand(0, 2 * y_spread) - y_spread;
				blit(*data, pattern, x, y);
			}
		}
		return true;
	}
	
	bool gui() override {
		ImGuiEx::NodeTitle("Splatter");
		inputSlot();
		outputSlot();
		ImGui::TextUnformatted("Background");
		inputSlot();
		ImGui::TextUnformatted("Pattern");
		bool res = ImGui::DragInt("Width", (i32*)&w, 1, 1, 999999);
		res = ImGui::DragInt("Height", (i32*)&h, 1, 1, 999999) || res;
		res = ImGui::DragInt("X count", (i32*)&x_count, 1, 1, 999999) || res;
		res = ImGui::DragInt("Y count", (i32*)&y_count, 1, 1, 999999) || res;
		res = ImGui::DragInt("X spread", (i32*)&x_spread, 1, 1, 999999) || res;
		res = ImGui::DragInt("Y spread", (i32*)&y_spread, 1, 1, 999999) || res;
		return res;
	}

	u32 w = 256;
	u32 h = 256;
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
	
	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		CompositeTexture::PixelData tmp(m_resource->m_app.getAllocator());
		if (!getInputPixelData(0, &tmp)) return false;

		data->w = tmp.w;
		data->h = tmp.h;
		data->channels = tmp.channels;
		data->pixels.resize(data->w * data->h * data->channels);

		float inv = 1/9.f;
		const float conv_mtx[] = {
			-inv, -inv, -inv,
			-inv, 17 * inv, -inv,
			-inv, -inv, -inv
		};

		for (i32 j = 0; j < (i32)data->h; ++j) {
			for (i32 i = 0; i < (i32)data->w; ++i) {
				for (u32 ch = 0; ch < data->channels; ++ch) {
					float v = 0;
					for (i32 cj = -1; cj <= 1; ++cj) {
						for (i32 ci = -1; ci <= 1; ++ci) {
							const u32 x = clamp(i + ci, 0, tmp.w - 1);
							const u32 y = clamp(j + cj, 0, tmp.h - 1);
							v += tmp.pixels[(x + y * tmp.w) * tmp.channels + ch] * conv_mtx[(ci + 1) + (cj + 1) * 3];
						}
					}
					v = clamp(v, 0.f, 255.f);
					data->pixels[(i + j * data->w) * data->channels + ch] = u8(v + 0.5f);
				}
			}
		}
		return true;
	}
	
	bool gui() override {
		ImGuiEx::NodeTitle("Sharpen");
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

	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		if (!getInputPixelData(0, data)) return false;
		for (u32 i = 0; i < data->pixels.size(); ++i) {
			float v = data->pixels[i] / 255.f;
			v = eval(v);
			v = clamp(v * 255.f, 0.f, 255.f);
			data->pixels[i] = u8(v + 0.5f);
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
		ImGuiEx::NodeTitle("Curve");
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

	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		data->pixels.resize(w * h);
		data->channels = 1;
		data->w = w;
		data->h = h;

		for (u32 j = 0; j < h; ++j) {
			for (u32 i = 0; i < w; ++i) {
				Vec2 v(i / float(w - 1) - 0.5f
					, j / float(h - 1) - 0.5f);
				float d = powf(length(v) * 2, power);
				d = clamp(d * 255.f, 0.f, 255.f);
				data->pixels[i + j * w] = u8(d + 0.5f);
			}
		}
		return true;
	}
	
	bool gui() override {
		ImGuiEx::NodeTitle("Circle");
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
	
	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		if (!getInputPixelData(0, data)) return false;
		if (data->channels < 3) return error("Input must have at least 3 channels");

		for (u32 i = 0, c = (u32)data->pixels.size(); i < c; i += data->channels) {
			Vec3 v(data->pixels[i], data->pixels[i + 1], data->pixels[i + 2]);
			v *= 1/255.f;

			float grayscale = v.x * 0.299f + v.y * 0.587f + v.z * 0.114f;
			grayscale = clamp(grayscale * 255.f, 0.f, 255.f);
			data->pixels[i] = data->pixels[i + 1] = data->pixels[i + 2] = u8(grayscale + 0.5f);
		}
		return true;
	}

	bool gui() override {
		ImGuiEx::NodeTitle("Grayscale");
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

	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		if (!getInputPixelData(0, data)) return false;
		CompositeTexture::PixelData tmp(m_resource->m_allocator);
		if (!getInputPixelData(1, &tmp)) return false;
		if (tmp.channels != data->channels) return error("Number of channel does not match");
		makeSameSize(data, &tmp);

		for (u32 i = 0, c = (u32)data->pixels.size(); i < c; ++i) {
			float a = data->pixels[i] / 255.f;
			const float b = tmp.pixels[i] / 255.f;
			a = a * (1 - alpha) + b * alpha;
			a = clamp(a * 255.f, 0.f, 255.f);
			data->pixels[i] = u8(a + 0.5f);
		}
		return true;
	}

	bool gui() override {
		ImGuiEx::NodeTitle("Mix");
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

	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		if (!getInputPixelData(0, data)) return false;

		for (u32 i = 0, c = (u32)data->pixels.size(); i < c; ++i) {
			const bool is_alpha = data->channels == 4 && i % 4 == 3;
			if (is_alpha) continue;

			float v = data->pixels[i] / 255.f;
			v += brightness;
			v = clamp(v * 255.f, 0.f, 255.f);
			data->pixels[i] = u8(v + 0.5f);
		}
		return true;
	}

	bool gui() override {
		ImGuiEx::NodeTitle("Brightness");
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
	
	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		if (!getInputPixelData(0, data)) return false;

		const float factor = 259 * (contrast + 255) / (255 * (259 - contrast));
		for (u32 i = 0, c = (u32)data->pixels.size(); i < c; ++i) {
			const bool is_alpha = data->channels == 4 && i % 4 == 3;
			if (is_alpha) continue;

			float v = data->pixels[i] / 255.f;
			v = factor * (v - 0.5f) + 0.5f;
			v = clamp(v * 255.f, 0.f, 255.f);
			data->pixels[i] = u8(v + 0.5f);
		}
		return true;
	}
	
	
	bool gui() override {
		ImGuiEx::NodeTitle("Contrast");
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

	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		if (!getInputPixelData(0, data)) return false;

		u8* p = data->pixels.getMutableData();
		for (u32 i = 0, c = (u32)data->pixels.size(); i < c; ++i) {
			p[i] = 0xff - p[i];
		}
		return true;
	};
	
	bool gui() override {
		ImGuiEx::NodeTitle("Invert");
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

	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		CompositeTexture::PixelData tmp(m_resource->m_app.getAllocator());
		if (!getInputPixelData(0, &tmp)) return false;
		data->w = tmp.w;
		data->h = tmp.h;
		data->channels = 4;
		data->pixels.resize(data->w * data->h * data->channels); 

		for (u32 i = 0, c = tmp.w * tmp.h; i < c; ++i) {
			for (u32 channel = 0; channel < 4; ++channel) {
				data->pixels[i * 4 + channel] = tmp.pixels[i * tmp.channels + channel % tmp.channels];
			}
		}
		return true;
	};
	
	bool gui() override {
		ImGuiEx::NodeTitle("Splat");
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
		blob.writeString(m_texture.c_str());
	}
	
	void deserialize(InputMemoryStream& blob) override {
		m_texture = blob.readString();
	}

	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		if (m_texture.isEmpty()) return error("Missing texture");
		i32 w, h, cmp;
		OutputMemoryStream file_content(m_resource->m_allocator);
		FileSystem& fs = m_resource->m_app.getEngine().getFileSystem();
		file_content.clear();
		if (!fs.getContentSync(m_texture, file_content)) return error("Failed to read file");

		stbi_uc* pixels = stbi_load_from_memory(file_content.data(), (i32)file_content.size(), &w, &h, &cmp, 0);
		if (!pixels) return error("Failed to load file");

		data->w = w;
		data->h = h;
		data->channels = cmp;
		data->pixels.resize(w * h * cmp);
		memcpy(data->pixels.getMutableData(), pixels, w * h * cmp);
		free(pixels);
		return true;
	};
	
	bool gui() override {
		ImGuiEx::NodeTitle("Input");
		outputSlot(); 
		Span<char> span(m_texture.beginUpdate(), m_texture.capacity());
		bool res = m_resource->m_app.getAssetBrowser().resourceInput("Source", span, Texture::TYPE, 150);
		m_texture.endUpdate();
		return res;
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

	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override { return false; };

	bool gui() override {
		ImGuiEx::NodeTitle("Output");
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
		case CompositeTexture::NodeType::CUT: node = LUMIX_NEW(allocator, CutNode)(allocator); break;
		case CompositeTexture::NodeType::CONTRAST: node = LUMIX_NEW(allocator, ContrastNode)(allocator); break;
		case CompositeTexture::NodeType::BRIGHTNESS: node = LUMIX_NEW(allocator, BrightnessNode)(allocator); break;
		case CompositeTexture::NodeType::RESIZE: node = LUMIX_NEW(allocator, ResizeNode)(allocator); break;
		case CompositeTexture::NodeType::SPLAT: node = LUMIX_NEW(allocator, SplatNode)(allocator); break;
		case CompositeTexture::NodeType::CELLULAR_NOISE: node = LUMIX_NEW(allocator, CellularNoiseNode)(allocator); break;
		case CompositeTexture::NodeType::SIMPLEX: node = LUMIX_NEW(allocator, SimplexNode)(allocator); break;
		case CompositeTexture::NodeType::WAVE_NOISE: node = LUMIX_NEW(allocator, WaveNoiseNode)(allocator); break;
		case CompositeTexture::NodeType::CIRCLE: node = LUMIX_NEW(allocator, CircleNode)(allocator); break;
		case CompositeTexture::NodeType::SET_ALPHA: node = LUMIX_NEW(allocator, SetAlphaNode)(allocator); break;
		case CompositeTexture::NodeType::CURVE: node = LUMIX_NEW(allocator, CurveNode)(allocator); break;
		case CompositeTexture::NodeType::GRAYSCALE: node = LUMIX_NEW(allocator, GrayscaleNode)(allocator); break;
		case CompositeTexture::NodeType::CONSTANT: node = LUMIX_NEW(allocator, ConstantNode)(allocator); break;
		case CompositeTexture::NodeType::MULTIPLY: node = LUMIX_NEW(allocator, MultiplyNode)(allocator); break;
		case CompositeTexture::NodeType::MIX: node = LUMIX_NEW(allocator, MixNode)(allocator); break;
		case CompositeTexture::NodeType::GRADIENT: node = LUMIX_NEW(allocator, GradientNode)(allocator); break;
		case CompositeTexture::NodeType::RANDOM_PIXELS: node = LUMIX_NEW(allocator, RandomPixelsNode)(allocator); break;
		case CompositeTexture::NodeType::SHARPEN: node = LUMIX_NEW(allocator, SharpenNode)(allocator); break;
		case CompositeTexture::NodeType::GRADIENT_MAP: node = LUMIX_NEW(allocator, GradientMapNode)(allocator); break;
		case CompositeTexture::NodeType::SPLATTER: node = LUMIX_NEW(allocator, SplatterNode)(allocator); break;
		case CompositeTexture::NodeType::STATIC_SWITCH: node = LUMIX_NEW(allocator, StaticSwitchNode)(allocator); break;
		case CompositeTexture::NodeType::STEP: node = LUMIX_NEW(allocator, StepNode)(allocator); break;
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
	for (Node* n : m_nodes) {
		LUMIX_DELETE(m_app.getAllocator(), n);
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
	OutputMemoryStream blob(m_app.getAllocator());
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
	LUMIX_DELETE(m_app.getAllocator(), input.node);
	m_nodes.eraseItem(input.node);
	--node->m_layers_count;
	for (Link& link : m_links) {
		if (link.getToNode() == node->m_id && link.getToPin() > idx) {
			link.to = link.getToNode() | ((link.getToPin() - 1) << 16);
		}
	}

}

void CompositeTexture::addArrayLayer(const char* path) {
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

bool CompositeTexture::generate(Result* result) {
	for (Node* n : m_nodes) n->m_error = "";

	OutputNode* node = (OutputNode*)m_nodes[0];
	switch(node->m_output_type) {
		case OutputNode::OutputType::SIMPLE: {
			result->is_cubemap = false;
			PixelData& pd = result->layers.emplace(m_app.getAllocator());
			if (!node->getInputPixelData(0, &pd)) return false;
			break;
		}
		case OutputNode::OutputType::CUBEMAP: {
			result->is_cubemap = true;
			for (u32 i = 0; i < 6; ++i) {
				PixelData& pd = result->layers.emplace(m_app.getAllocator());
				const Node::Input input = node->getInput(i);
				if (!input) return false;
				if (!input.getPixelData(&pd)) return false;
			}
			break;
		}
		case OutputNode::OutputType::ARRAY: {
			result->is_cubemap = false;
			for (u32 i = 0; i < node->m_layers_count; ++i) {
				PixelData& pd = result->layers.emplace(m_app.getAllocator());
				const Node::Input input = node->getInput(i);
				if (!input) return false;
				if (!input.getPixelData(&pd)) return false;
			}
			break;
		}
	}

	for (PixelData& pd : result->layers) {
		if (pd.channels != node->m_channels_count) {
			OutputMemoryStream tmp(m_app.getAllocator());
			const u32 n = node->m_channels_count;
			tmp.resize(pd.w * pd.h * n);
			for (u32 i = 0; i < pd.w * pd.h; ++i) {
				for (u32 ch = 0; ch < n; ++ch) {
					tmp[i * n + ch] = ch < pd.channels ? pd.pixels[i * pd.channels + ch] : 0xff;
				}
			}
			pd.pixels = static_cast<OutputMemoryStream&&>(tmp);
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
	return true;
}

CompositeTexture::Node* CompositeTexture::addNode(CompositeTexture::NodeType type) {
	CompositeTexture::Node* node = createNode(type, *this, m_app.getAllocator());
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

static const struct {
	char key;
	const char* label;
	CompositeTexture::NodeType type;
} TYPES[] = {
	{ 'B', "Brightness", CompositeTexture::NodeType::BRIGHTNESS },
	{ 'V', "Cell noise", CompositeTexture::NodeType::CELLULAR_NOISE },
	{ 'O', "Circle", CompositeTexture::NodeType::CIRCLE },
	{ 'C', "Color", CompositeTexture::NodeType::COLOR },
	{ '1', "Constant", CompositeTexture::NodeType::CONSTANT },
	{ 0, "Contrast", CompositeTexture::NodeType::CONTRAST },
	{ 'U', "Curve", CompositeTexture::NodeType::CURVE },
	{ 0, "Cut", CompositeTexture::NodeType::CUT },
	{ 'F', "Flip", CompositeTexture::NodeType::FLIP },
	{ 0, "Gamma", CompositeTexture::NodeType::GAMMA },
	{ 0, "Gradient", CompositeTexture::NodeType::GRADIENT },
	{ 0, "Gradient map", CompositeTexture::NodeType::GRADIENT_MAP },
	{ 'G', "Grayscale", CompositeTexture::NodeType::GRAYSCALE },
	{ 'T', "Input", CompositeTexture::NodeType::INPUT },
	{ 'I', "Invert", CompositeTexture::NodeType::INVERT },
	{ 'M', "Merge", CompositeTexture::NodeType::MERGE },
	{ 'X', "Mix", CompositeTexture::NodeType::MIX },
	{ 0, "Multiply", CompositeTexture::NodeType::MULTIPLY },
	{ 0, "Random pixels", CompositeTexture::NodeType::RANDOM_PIXELS },
	{ 'R', "Resize", CompositeTexture::NodeType::RESIZE },
	{ 'A', "Set alpha", CompositeTexture::NodeType::SET_ALPHA },
	{ 0, "Sharpen", CompositeTexture::NodeType::SHARPEN },
	{ 'N', "Simplex", CompositeTexture::NodeType::SIMPLEX },
	{ 0, "Splat", CompositeTexture::NodeType::SPLAT },
	{ 0, "Splatter", CompositeTexture::NodeType::SPLATTER },
	{ 'S', "Split", CompositeTexture::NodeType::SPLIT },
	{ 0, "Static switch", CompositeTexture::NodeType::STATIC_SWITCH },
	{ 0, "Step", CompositeTexture::NodeType::STEP },
	{ 'W', "Wave noise", CompositeTexture::NodeType::WAVE_NOISE }
};

struct CompositeTextureEditorWindow : StudioApp::GUIPlugin, NodeEditor {
	CompositeTextureEditorWindow(const Path& path, CompositeTextureEditor& editor, StudioApp& app)
		: NodeEditor(app.getAllocator())
		, m_editor(editor)
		, m_app(app)
		, m_resource(app, app.getAllocator())
	{
		IAllocator& allocator = m_app.getAllocator();
		FileSystem& fs = m_app.getEngine().getFileSystem();
		m_path = path;
		m_loading_handle = fs.getContent(path, makeDelegate<&CompositeTextureEditorWindow::onLoaded>(this));
	}

	~CompositeTextureEditorWindow() {
		FileSystem& fs = m_app.getEngine().getFileSystem();
		if (m_loading_handle.isValid()) fs.cancel(m_loading_handle);
	}

	void onCanvasClicked(ImVec2 pos, i32 hovered_link) override {
		CompositeTexture::Node* n = nullptr;
		for (const auto& t : TYPES) {
			if (t.key && os::isKeyDown((os::Keycode)t.key)) {
				n = m_resource.addNode(t.type);
				break;
			}
		}
		if (n) {
			n->m_pos = pos;
			if (hovered_link >= 0) splitLink(m_resource.m_nodes.back(), m_resource.m_links, hovered_link);
			pushUndo(NO_MERGE_UNDO);
		}	
	}

	void pushUndo(u32 tag) override {
		m_dirty = true;
		SimpleUndoRedo::pushUndo(tag);
	}

	void onLinkDoubleClicked(NodeEditorLink& link, ImVec2 pos) override {}
	
	void onContextMenu(ImVec2 pos) override {
		CompositeTexture::Node* node = nullptr;
		static char filter[64] = "";
		if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
		ImGui::SetNextItemWidth(150);
		ImGui::InputTextWithHint("##filter", "Filter", filter, sizeof(filter), ImGuiInputTextFlags_AutoSelectAll);
		for (const auto& t : TYPES) {
			if ((!filter[0] || stristr(t.label, filter)) && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::MenuItem(t.label))) {
				node = m_resource.addNode(t.type);
				filter[0] = '\0';
				ImGui::CloseCurrentPopup();
				break;
			}
		}
		if (node) {
			node->m_pos = pos;
			pushUndo(NO_MERGE_UNDO);
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

	void deleteSelectedNodes() {
		if (m_is_any_item_active) return;
		m_resource.deleteSelectedNodes();
		pushUndo(NO_MERGE_UNDO);
	}

	bool onAction(const Action& action) override {
		ASSERT(m_has_focus);

		if (&action == &m_app.getUndoAction()) undo();
		else if (&action == &m_app.getRedoAction()) redo();
		else if (&action == &m_app.getSaveAction()) saveAs(m_path);
		else if (&action == &m_app.getDeleteAction()) deleteSelectedNodes();
		else return false;
		
		return true;
	}

	void onLoaded(u64 size, const u8* data, bool success) {
		m_loading_handle = FileSystem::AsyncHandle::invalid();
		if (!success) {
			logError("Failed to load ", m_path);
			return;
		}
		
		InputMemoryStream blob(data, size);
		m_resource.deserialize(blob);
		pushUndo(NO_MERGE_UNDO);
		m_dirty = false;
	}
	
	void exportAs() {
		char path[LUMIX_MAX_PATH];
		if (!os::getSaveFilename(Span(path), "TGA Image\0*.tga\0", "tga")) return;

		IAllocator& allocator = m_app.getAllocator();
		CompositeTexture::Result img(allocator);
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

		bool res = Texture::saveTGA(&file, img.layers[0].w, img.layers[0].h, gpu::TextureFormat::RGBA8, img.layers[0].pixels.data(), true, Path(path), allocator);
		file.close();

		if (!res) {
			logError("Could not save ", path);
		}
	}

	void saveAs(const Path& path) {
		FileSystem& fs = m_app.getEngine().getFileSystem();
		IAllocator& allocator = m_app.getAllocator();
		OutputMemoryStream blob(allocator);
		m_resource.serialize(blob);
		if (!fs.saveContentSync(path, blob)) {
			logError("Failed to save ", path);
			return;
		}
		CompositeTexture::Result img(allocator);
		m_resource.generate(&img);

		m_path = path;
		m_dirty = false;
	}

	void onGUI() override {
		Span<const char> basename = Path::getBasename(m_path.c_str());
		StaticString<128> title(basename, "##cte", (uintptr)this);
		bool open = true;
		m_has_focus = false;
		if (m_focus_request) {
			ImGui::SetNextWindowFocus();
			m_focus_request = false;
		}
		ImGui::SetNextWindowDockID(m_dock_id ? m_dock_id : m_app.getDockspaceID(), ImGuiCond_Appearing);
		ImGuiWindowFlags flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoSavedSettings;
		if (m_dirty) flags |= ImGuiWindowFlags_UnsavedDocument;
		if (ImGui::Begin(title, &open, flags)) {
			m_dock_id = ImGui::GetWindowDockID();
			m_has_focus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
			if (ImGui::BeginMenuBar()) {
				if (ImGui::BeginMenu("File")) {
					if (menuItem(m_app.getSaveAction(), true)) saveAs(m_path);
					if (ImGui::MenuItem("Save As")) m_show_save_as = true;
					if (ImGui::MenuItem("Export")) exportAs();
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Edit")) {
					if (menuItem(m_app.getUndoAction(), canUndo())) undo();
					if (menuItem(m_app.getRedoAction(), canRedo())) redo();
					if (ImGui::MenuItem(ICON_FA_BRUSH "Clear")) deleteUnreachable();
					ImGui::EndMenu();
				}
				if (ImGuiEx::IconButton(ICON_FA_SAVE, "Save")) saveAs(m_path);
				if (ImGuiEx::IconButton(ICON_FA_BRUSH, "Clear unreachble nodes")) deleteUnreachable();
				ImGui::EndMenuBar();
			}
		
			FileSelector& fs = m_app.getFileSelector();
			if (fs.gui("Save As", &m_show_save_as, "ltc", true)) saveAs(Path(fs.getPath()));

			if (m_loading_handle.isValid()) {
				ImGui::TextUnformatted("Loading...");
			}
			else {
				nodeEditorGUI(m_resource.m_nodes, m_resource.m_links);
			}
		}
		if (!open) {
			if (m_dirty) {
				ImGui::OpenPopup("Confirm##cct");
			}
			else {
				m_editor.m_windows.eraseItem(this);
				m_app.removePlugin(*this);
				LUMIX_DELETE(m_app.getAllocator(), this);
			}
		}

		if (ImGui::BeginPopupModal("Confirm##cct")) {
			ImGui::TextUnformatted("All changes will be lost. Continue anyway?");
			if (ImGui::Selectable("Yes")) {
				m_editor.m_windows.eraseItem(this);
				m_app.removePlugin(*this);
				LUMIX_DELETE(m_app.getAllocator(), this);
			}
			ImGui::Selectable("No");
			ImGui::EndPopup();
		}
		ImGui::End();
	}

	const char* getName() const override { return "composite_texture_editor"; }
	bool hasFocus() const override { return m_has_focus; }
	
	CompositeTextureEditor& m_editor;
	StudioApp& m_app;
	Path m_path;
	CompositeTexture m_resource;
	FileSystem::AsyncHandle m_loading_handle = FileSystem::AsyncHandle::invalid();
	bool m_has_focus = false;
	bool m_show_save_as = false;
	bool m_focus_request = false;
	bool m_dirty = false;
	bool m_show_confirm = false;
	ImGuiID m_dock_id = 0;
};

CompositeTextureEditor::CompositeTextureEditor(StudioApp& app)
	: m_app(app)
	, m_windows(app.getAllocator())
{}

void CompositeTextureEditor::open(const Path& path) {
	for (CompositeTextureEditorWindow* win : m_windows) {
		if (win->m_path == path) {
			win->m_focus_request = true;
			return;
		}
	}
	
	IAllocator& allocator = m_app.getAllocator();
	CompositeTextureEditorWindow* win = LUMIX_NEW(allocator, CompositeTextureEditorWindow)(Path(path), *this, m_app);
	if (!m_windows.empty()) win->m_dock_id = m_windows.last()->m_dock_id;
	m_windows.push(win);
	m_app.addPlugin(*win);
}

} // namespace Lumix
