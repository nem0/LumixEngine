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
	GREYSCALE,
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
	WAVE_NOISE
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
	ImGuiEx::EndNode();
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

bool CompositeTexture::Node::getInputPixelData(u32 pin_idx, PixelData* pd) const {
	const Input input = getInput(pin_idx);
	if (!input) return false;
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
	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::SPLIT; }
	
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		CompositeTexture::PixelData tmp(m_resource->m_app.getAllocator());
		if (!getInputPixelData(0, &tmp)) return false;
		if (output_idx >= tmp.channels) return false;

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
	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::MERGE; }
	
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		const Input inputs[] = { getInput(0), getInput(1), getInput(2), getInput(3) };
		for (const Input& i : inputs) if (!i) return false;
		
		CompositeTexture::PixelData first_pd(m_resource->m_app.getAllocator());
		if (!inputs[0].getPixelData(&first_pd)) return false;
		if (first_pd.channels != 1) return false;

		data->w = first_pd.w;
		data->h = first_pd.h;
		data->channels = 4;
		data->pixels.resize(4 * first_pd.w * first_pd.h);
		u8* dst = data->pixels.getMutableData();
		const u8* first_src = first_pd.pixels.data();
		for (u32 i = 0; i < first_pd.w * first_pd.h; ++i) {
			dst[i * 4] = first_src[i];
		}

		for (u32 i = 1; i < 4; ++i) {
			CompositeTexture::PixelData tmp(m_resource->m_app.getAllocator());
			if (!inputs[i].getPixelData(&tmp)) return false;
			if (tmp.channels != 1) return false;
			resize(&tmp, first_pd.w, first_pd.h);
			
			const u8* src = tmp.pixels.data();
			for (u32 j = 0; j < tmp.w * tmp.h; ++j) {
				dst[j * 4 + i] = src[j];
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
	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::MULTIPLY; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }
	
	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		CompositeTexture::PixelData tmp(m_resource->m_app.getAllocator());
		if (!getInputPixelData(0, data)) return false;
		if (!getInputPixelData(1, &tmp)) return false;
		if (tmp.channels != data->channels) return false;
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
		if (data->w == w && data->h != h) return true;

		OutputMemoryStream tmp(m_resource->m_app.getAllocator());
		tmp.resize(w * h * data->channels);

		const i32 res = stbir_resize_uint8(data->pixels.data(), data->w, data->h, 0, tmp.getMutableData(), w, h, 0, data->channels);

		data->w = w;
		data->h = h;
		data->pixels = tmp;
		return res == 1;
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
			default:
				ASSERT(false);
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

struct CircleNode final : CompositeTexture::Node {
	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::CIRCLE; }
	bool hasInputPins() const override { return true; }
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

struct GreyscaleNode final : CompositeTexture::Node {
	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::GREYSCALE; }
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }
	
	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		if (!getInputPixelData(0, data)) return false;
		if (data->channels < 3) return false;

		for (u32 i = 0, c = (u32)data->pixels.size(); i < c; i += data->channels) {
			Vec3 v(data->pixels[i], data->pixels[i + 1], data->pixels[i + 2]);
			v *= 1/255.f;

			float greyscale = v.x * 0.299f + v.y * 0.587f + v.z * 0.114f;
			greyscale = clamp(greyscale * 255.f, 0.f, 255.f);
			data->pixels[i] = data->pixels[i + 1] = data->pixels[i + 2] = u8(greyscale + 0.5f);
		}
		return true;
	}

	bool gui() override {
		ImGuiEx::NodeTitle("Greyscale");
		inputSlot();
		ImGui::TextUnformatted(" ");
		ImGui::SameLine();
		outputSlot();
		return false;
	}
};

struct MixNode final : CompositeTexture::Node {
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
		CompositeTexture::PixelData tmp(m_resource->allocator);
		if (!getInputPixelData(1, &tmp)) return false;
		if (tmp.channels != data->channels) return false;
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

		u8* p = data->pixels.getMutableData();
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
		if (m_texture.isEmpty()) return false;
		i32 w, h, cmp;
		OutputMemoryStream file_content(m_resource->allocator);
		FileSystem& fs = m_resource->m_app.getEngine().getFileSystem();
		file_content.clear();
		if (!fs.getContentSync(m_texture, file_content)) return false;

		stbi_uc* pixels = stbi_load_from_memory(file_content.data(), (i32)file_content.size(), &w, &h, &cmp, 0);
		if (!pixels) return false;

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
	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::OUTPUT; }

	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return false; }
	
	void serialize(OutputMemoryStream& blob) const override {
		blob.write(m_output_type);
		blob.write(m_layers_count);
	}
	
	void deserialize(InputMemoryStream& blob) override {
		blob.read(m_output_type);
		blob.read(m_layers_count);
	}

	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override { return false; };

	bool gui() override {
		ImGuiEx::NodeTitle("Output");
		switch(m_output_type) {
			case OutputType::SIMPLE:
				inputSlot(); ImGui::TextUnformatted("RGBA");
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
			default: ASSERT(false); old_pin_count = 1; break;
		}
		bool res = ImGui::Combo("Type", (i32*)&m_output_type, "Simple\0Array\0Cubemap\0");
		if (res) {
			u32 new_pin_count;
			switch(m_output_type) {
				case OutputType::SIMPLE: new_pin_count = 1; break;
				case OutputType::CUBEMAP: new_pin_count = 6; break;
				case OutputType::ARRAY: new_pin_count = m_layers_count; break;
				default: ASSERT(false); new_pin_count = 1; break;
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
		return res;
	}

	enum class OutputType : u32 {
		SIMPLE,
		ARRAY,
		CUBEMAP
	};

	OutputType m_output_type = OutputType::SIMPLE;
	u32 m_layers_count = 1;
};

CompositeTexture::Node* createNode(CompositeTexture::NodeType type, CompositeTexture& resource, IAllocator& allocator) {
	CompositeTexture::Node* node = nullptr;
	switch (type) {
		case CompositeTexture::NodeType::OUTPUT: node = LUMIX_NEW(allocator, OutputNode); break; 
		case CompositeTexture::NodeType::INPUT: node = LUMIX_NEW(allocator, InputNode); break; 
		case CompositeTexture::NodeType::FLIP: node = LUMIX_NEW(allocator, FlipNode); break; 
		case CompositeTexture::NodeType::INVERT: node = LUMIX_NEW(allocator, InvertNode); break; 
		case CompositeTexture::NodeType::COLOR: node = LUMIX_NEW(allocator, ColorNode); break; 
		case CompositeTexture::NodeType::SPLIT: node = LUMIX_NEW(allocator, SplitNode); break; 
		case CompositeTexture::NodeType::MERGE: node = LUMIX_NEW(allocator, MergeNode); break; 
		case CompositeTexture::NodeType::GAMMA: node = LUMIX_NEW(allocator, GammaNode); break; 
		case CompositeTexture::NodeType::CONTRAST: node = LUMIX_NEW(allocator, ContrastNode); break; 
		case CompositeTexture::NodeType::BRIGHTNESS: node = LUMIX_NEW(allocator, BrightnessNode); break; 
		case CompositeTexture::NodeType::RESIZE: node = LUMIX_NEW(allocator, ResizeNode); break; 
		case CompositeTexture::NodeType::SPLAT: node = LUMIX_NEW(allocator, SplatNode); break; 
		case CompositeTexture::NodeType::CELLULAR_NOISE: node = LUMIX_NEW(allocator, CellularNoiseNode); break; 
		case CompositeTexture::NodeType::SIMPLEX: node = LUMIX_NEW(allocator, SimplexNode); break; 
		case CompositeTexture::NodeType::WAVE_NOISE: node = LUMIX_NEW(allocator, WaveNoiseNode); break; 
		case CompositeTexture::NodeType::CIRCLE: node = LUMIX_NEW(allocator, CircleNode); break; 
		case CompositeTexture::NodeType::GREYSCALE: node = LUMIX_NEW(allocator, GreyscaleNode); break; 
		case CompositeTexture::NodeType::CONSTANT: node = LUMIX_NEW(allocator, ConstantNode); break; 
		case CompositeTexture::NodeType::MULTIPLY: node = LUMIX_NEW(allocator, MultiplyNode); break; 
		case CompositeTexture::NodeType::MIX: node = LUMIX_NEW(allocator, MixNode); break; 
		case CompositeTexture::NodeType::GRADIENT: node = LUMIX_NEW(allocator, GradientNode); break; 
		case CompositeTexture::NodeType::RANDOM_PIXELS: node = LUMIX_NEW(allocator, RandomPixelsNode); break; 
		default: ASSERT(false); return nullptr;
	}
	node->m_resource = &resource;
	return node;
}

} // anonymous namespace

CompositeTexture::CompositeTexture(StudioApp& app, IAllocator& allocator)
	: allocator(allocator)
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

	OutputMemoryStream data(allocator);
	if (!fs.getContentSync(path, data)) return false;

	InputMemoryStream blob(data);
	deserialize(blob);
	return true;
}

bool CompositeTexture::save(FileSystem& fs, const Path& path) {
	OutputMemoryStream blob(m_app.getAllocator());
	serialize(blob);
	os::OutputFile file;
	if (fs.open(path.c_str(), file)) {
		bool res = file.write(blob.data(), blob.size());
		file.close();
		return res;
	}
	return false;
}


CompositeTextureEditor::CompositeTextureEditor(StudioApp& app)
	: NodeEditor(app.getAllocator())
	, m_allocator(app.getAllocator())
	, m_app(app)
	, m_recent_paths(app.getAllocator())
{
	newGraph();

	m_save_action.init(ICON_FA_SAVE "Save", "Composite texture editor save", "composite_texture_editor_save", ICON_FA_SAVE, os::Keycode::S, Action::Modifiers::CTRL, true);
	m_save_action.func.bind<&CompositeTextureEditor::save>(this);
	m_save_action.plugin = this;

	m_delete_action.init(ICON_FA_TRASH "Delete", "Composite texture editor delete", "composite_texture_editor_delete", ICON_FA_TRASH, os::Keycode::DEL, Action::Modifiers::NONE, true);
	m_delete_action.func.bind<&CompositeTextureEditor::deleteSelectedNodes>(this);
	m_delete_action.plugin = this;

	m_undo_action.init(ICON_FA_UNDO "Undo", "Composite texture editor undo", "composite_texture_editor_undo", ICON_FA_UNDO, os::Keycode::Z, Action::Modifiers::CTRL, true);
	m_undo_action.func.bind<&CompositeTextureEditor::undo>((SimpleUndoRedo*)this);
	m_undo_action.plugin = this;

	m_redo_action.init(ICON_FA_REDO "Redo", "Composite texture editor redo", "composite_texture_editor_redo", ICON_FA_REDO, os::Keycode::Z, Action::Modifiers::CTRL | Action::Modifiers::SHIFT, true);
	m_redo_action.func.bind<&CompositeTextureEditor::redo>((SimpleUndoRedo*)this);
	m_redo_action.plugin = this;

	m_toggle_ui.init("Composite texture editor", "Toggle composite texture editor", "composite_texture_editor", "", true);
	m_toggle_ui.func.bind<&CompositeTextureEditor::toggleOpen>(this);
	m_toggle_ui.is_selected.bind<&CompositeTextureEditor::isOpen>(this);
	
	m_app.addWindowAction(&m_toggle_ui);
	m_app.addAction(&m_undo_action);
	m_app.addAction(&m_redo_action);
	m_app.addAction(&m_save_action);
	m_app.addAction(&m_delete_action);
}

CompositeTextureEditor::~CompositeTextureEditor() {
	m_app.removeAction(&m_toggle_ui);
	m_app.removeAction(&m_undo_action);
	m_app.removeAction(&m_redo_action);
	m_app.removeAction(&m_save_action);
	m_app.removeAction(&m_delete_action);
	if (m_resource) LUMIX_DELETE(m_allocator, m_resource);
}

void CompositeTextureEditor::deleteSelectedNodes() {
	if (m_is_any_item_active) return;
	m_resource->deleteSelectedNodes();
	pushUndo(NO_MERGE_UNDO);
}

bool CompositeTextureEditor::saveAs(const Path& path) {
	FileSystem& fs = m_app.getEngine().getFileSystem();
	os::OutputFile file;
	if (!fs.open(path.c_str(), file)) return false;
	
	OutputMemoryStream blob(m_app.getAllocator());
	serialize(blob);
	bool res = file.write(blob.data(), blob.size());
	file.close();
	pushRecent(path.c_str());
	return res;
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

bool CompositeTexture::generate(Result* result) {
	const OutputNode* node = (OutputNode*)m_nodes[0];
	switch(node->m_output_type) {
		case OutputNode::OutputType::SIMPLE: {
			result->is_cubemap = false;
			PixelData& pd = result->layers.emplace(m_app.getAllocator());
			const Node::Input input = node->getInput(0);
			if (!input) return false;
			return input.getPixelData(&pd);
		}
		case OutputNode::OutputType::CUBEMAP: {
			result->is_cubemap = true;
			for (u32 i = 0; i < 6; ++i) {
				PixelData& pd = result->layers.emplace(m_app.getAllocator());
				const Node::Input input = node->getInput(i);
				if (!input) return false;
				if (!input.getPixelData(&pd)) return false;
			}
			return true;
		}
		case OutputNode::OutputType::ARRAY: {
			result->is_cubemap = false;
			for (u32 i = 0; i < node->m_layers_count; ++i) {
				PixelData& pd = result->layers.emplace(m_app.getAllocator());
				const Node::Input input = node->getInput(i);
				if (!input) return false;
				if (!input.getPixelData(&pd)) return false;
			}
			return true;
		}
	}
	return false;
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
		default: ASSERT(false); return 1;
	}
}

void CompositeTextureEditor::newGraph() {
	if (m_resource) LUMIX_DELETE(m_allocator, m_resource);
	m_resource = LUMIX_NEW(m_allocator, CompositeTexture)(m_app, m_allocator);
	clearUndoStack();

	m_resource->initDefault();
	m_path = "";
	pushUndo(NO_MERGE_UNDO);
}

void CompositeTextureEditor::deserialize(InputMemoryStream& blob) {
	LUMIX_DELETE(m_allocator, m_resource);
	m_resource = LUMIX_NEW(m_allocator, CompositeTexture)(m_app, m_allocator);
	m_resource->deserialize(blob);
}

void CompositeTextureEditor::serialize(OutputMemoryStream& blob) {
	m_resource->serialize(blob);
}

static const struct {
	char key;
	const char* label;
	CompositeTexture::NodeType type;
} TYPES[] = {
	{ 'B', "Brightness", CompositeTexture::NodeType::BRIGHTNESS },
	{ 'O', "Circle", CompositeTexture::NodeType::CIRCLE },
	{ 'C', "Color", CompositeTexture::NodeType::COLOR },
	{ '1', "Constant", CompositeTexture::NodeType::CONSTANT },
	{ 0, "Contrast", CompositeTexture::NodeType::CONTRAST },
	{ 'F', "Flip", CompositeTexture::NodeType::FLIP },
	{ 0, "Gamma", CompositeTexture::NodeType::GAMMA },
	{ 0, "Gradient", CompositeTexture::NodeType::GRADIENT },
	{ 'G', "Greyscale", CompositeTexture::NodeType::GREYSCALE },
	{ 'T', "Input", CompositeTexture::NodeType::INPUT },
	{ 'I', "Invert", CompositeTexture::NodeType::INVERT },
	{ 'M', "Merge", CompositeTexture::NodeType::MERGE },
	{ 'X', "Mix", CompositeTexture::NodeType::MIX },
	{ 0, "Multiply", CompositeTexture::NodeType::MULTIPLY },
	{ 0, "Random pixels", CompositeTexture::NodeType::RANDOM_PIXELS },
	{ 'R', "Resize", CompositeTexture::NodeType::RESIZE },
	{ 0, "Simplex", CompositeTexture::NodeType::SIMPLEX },
	{ 0, "Splat", CompositeTexture::NodeType::SPLAT },
	{ 'S', "Split", CompositeTexture::NodeType::SPLIT },
	{ 'V', "Voronoi", CompositeTexture::NodeType::CELLULAR_NOISE },
	{ 'W', "Wave noise", CompositeTexture::NodeType::WAVE_NOISE }
};

void CompositeTextureEditor::onCanvasClicked(ImVec2 pos, i32 hovered_link) {
	CompositeTexture::Node* n = nullptr;
	for (const auto& t : TYPES) {
		if (t.key && os::isKeyDown((os::Keycode)t.key)) {
			n = m_resource->addNode(t.type);
			break;
		}
	}
	if (n) {
		n->m_pos = pos;
		if (hovered_link >= 0) splitLink(m_resource->m_nodes.back(), m_resource->m_links, hovered_link);
		pushUndo(NO_MERGE_UNDO);
	}	
}

void CompositeTextureEditor::open(const Path& path) {
	m_is_focus_request = true;
	m_is_open = true;
	FileSystem& fs = m_app.getEngine().getFileSystem();
	IAllocator& allocator = m_app.getAllocator();
	OutputMemoryStream content(allocator);
	if (fs.getContentSync(path, content)) {
		m_path = path;
		InputMemoryStream blob(content);
		deserialize(blob);
		pushUndo(NO_MERGE_UNDO);
		pushRecent(path.c_str());
	}
	else {
		logError("Could not load", path);
	}
}


bool CompositeTextureEditor::getSavePath() {
	char path[LUMIX_MAX_PATH];
	if (os::getSaveFilename(Span(path), "Composite texture\0*.ltc\0", "ltc")) {
		FileSystem& fs = m_app.getEngine().getFileSystem();
		char rel_path[LUMIX_MAX_PATH];
		if (!fs.makeRelative(Span(rel_path), path)) {
			logError("Can not save ", path, " because it's not in root directory (", fs.getBasePath(), ").");		
			return false;
		}
		m_path = path;
		return true;
	}
	return false;
}

void CompositeTextureEditor::save() {
	if (!m_path.isEmpty() || getSavePath()) saveAs(m_path);
}

void CompositeTextureEditor::onSettingsLoaded() {
	Settings& settings = m_app.getSettings();
	m_is_open = settings.getValue(Settings::GLOBAL, "is_composite_texture_editor_open", false);

	m_recent_paths.clear();
	char tmp[LUMIX_MAX_PATH];
	for (u32 i = 0; ; ++i) {
		const StaticString<32> key("proc_geom_editor_recent_", i);
		const u32 len = settings.getValue(Settings::LOCAL, key, Span(tmp));
		if (len == 0) break;
		m_recent_paths.emplace(tmp, m_app.getAllocator());
	}
}

void CompositeTextureEditor::onBeforeSettingsSaved() {
	Settings& settings = m_app.getSettings();
	settings.setValue(Settings::GLOBAL, "is_composite_texture_editor_open", m_is_open);
	
	for (const String& p : m_recent_paths) {
		const u32 i = u32(&p - m_recent_paths.begin());
		const StaticString<32> key("proc_geom_editor_recent_", i);
		settings.setValue(Settings::LOCAL, key, p.c_str());
	}
}

void CompositeTextureEditor::pushRecent(const char* path) {
	String p(path, m_app.getAllocator());
	m_recent_paths.eraseItems([&](const String& s) { return s == path; });
	m_recent_paths.push(static_cast<String&&>(p));
}

void CompositeTextureEditor::open() {
	char path[LUMIX_MAX_PATH];
	if (!os::getOpenFilename(Span(path), "Composite texture\0*.ltc\0", "ltc")) return;
	char rel_path[LUMIX_MAX_PATH];
	if (!m_app.getEngine().getFileSystem().makeRelative(Span(rel_path), path)) {
		FileSystem& fs = m_app.getEngine().getFileSystem();
		logError("Can not open ", path, " because it's not in root directory (", fs.getBasePath(), ").");		
		return;
	}
	open(Path(rel_path));
}

void CompositeTextureEditor::exportAs() {
	char path[LUMIX_MAX_PATH];
	if (!os::getSaveFilename(Span(path), "TGA Image\0*.tga\0", "tga")) return;

	CompositeTexture::Result img(m_allocator);
	if (!m_resource->generate(&img)) {
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

	bool res = Texture::saveTGA(&file, img.layers[0].w, img.layers[0].h, gpu::TextureFormat::RGBA8, img.layers[0].pixels.data(), true, Path(path), m_allocator);
	file.close();

	if (!res) {
		logError("Could not save ", path);
	}
}

void CompositeTextureEditor::onWindowGUI() {
	m_has_focus = false;
	if (!m_is_open) return;

	ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);
	if (m_is_focus_request) ImGui::SetNextWindowFocus();
	m_is_focus_request = false;
	if (ImGui::Begin("Composite texture", &m_is_open, ImGuiWindowFlags_MenuBar)) {
		m_has_focus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		if (ImGui::BeginMenuBar()) {
			if (ImGui::BeginMenu("File")) {
				if (ImGui::MenuItem("New")) newGraph();
				if (ImGui::MenuItem("Open")) open();
				menuItem(m_save_action, true);
				if (ImGui::MenuItem("Save As") && getSavePath()) saveAs(m_path);
				if (ImGui::MenuItem("Export")) exportAs();
				if (ImGui::BeginMenu("Recent", !m_recent_paths.empty())) {
					for (const String& s : m_recent_paths) {
						if (ImGui::MenuItem(s.c_str())) open(Path(s.c_str()));
					}
					ImGui::EndMenu();
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Edit")) {
				menuItem(m_undo_action, canUndo());
				menuItem(m_redo_action, canRedo());
				ImGui::EndMenu();
			}
			ImGui::EndMenuBar();
		}
		nodeEditorGUI(m_resource->m_nodes, m_resource->m_links);
	}
	ImGui::End();
}

void CompositeTextureEditor::onLinkDoubleClicked(CompositeTexture::Link& link, ImVec2 pos) {}

void CompositeTextureEditor::onContextMenu(bool recently_opened, ImVec2 pos) {
	CompositeTexture::Node* node = nullptr;
	for (const auto& t : TYPES) {
		if (ImGui::MenuItem(t.label)) node = m_resource->addNode(t.type);
	}
	if (node) {
		node->m_pos = pos;
		pushUndo(NO_MERGE_UNDO);
	}
}

} // namespace Lumix
