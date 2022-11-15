#include "composite_texture.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/log.h"
#include "engine/math.h"
#include "engine/os.h"
#include "engine/stream.h"
#include "engine/string.h"
#include "editor/asset_browser.h"
#include "editor/studio_app.h"
#include "renderer/texture.h"
#include "stb/stb_image.h"

namespace Lumix {

enum class CompositeTexture::NodeType : u32 {
	OUTPUT,
	INPUT,
	INVERT,
	CONSTANT,
	SPLIT,
	MERGE,
	VERTICAL_FLIP
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
	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::SPLIT; }
	
	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		const Input input = getInput(0);
		if (!input) return false;
		CompositeTexture::PixelData tmp(m_resource->m_app.getAllocator());
		if (!input.getPixelData(&tmp)) return false;
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
			if (tmp.w != data->w || tmp.h != data->h) return false;
			
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
		ImGuiEx::NodeTitle("Constant");
		outputSlot();
		return ImGui::ColorPicker4("Color", &color.x);
	}

	Vec4 color = Vec4(1);
};

struct VerticalFlipNode final : CompositeTexture::Node {
	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::VERTICAL_FLIP; }

	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		const Input input = getInput(0);
		if (!input) return false;
		if (!input.getPixelData(data)) return false;

		u8* ptr = data->pixels.getMutableData();
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
		ImGuiEx::NodeTitle("Vertical flip");
		inputSlot();
		ImGui::TextUnformatted(" ");
		ImGui::SameLine();
		outputSlot();
		return false;
	}
};

struct InvertNode final : CompositeTexture::Node {
	CompositeTexture::NodeType getType() const override { return CompositeTexture::NodeType::INVERT; }

	bool hasInputPins() const override { return true; }
	bool hasOutputPins() const override { return true; }

	bool getPixelData(CompositeTexture::PixelData* data, u32 output_idx) override {
		const Input input = getInput(0);
		if (!input) return false;
		input.getPixelData(data);
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
		case CompositeTexture::NodeType::VERTICAL_FLIP: node = LUMIX_NEW(allocator, VerticalFlipNode); break; 
		case CompositeTexture::NodeType::INVERT: node = LUMIX_NEW(allocator, InvertNode); break; 
		case CompositeTexture::NodeType::CONSTANT: node = LUMIX_NEW(allocator, ConstantNode); break; 
		case CompositeTexture::NodeType::SPLIT: node = LUMIX_NEW(allocator, SplitNode); break; 
		case CompositeTexture::NodeType::MERGE: node = LUMIX_NEW(allocator, MergeNode); break; 
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

void CompositeTexture::removeTerrainLayer(u32 idx) {
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

void CompositeTexture::addTerrainLayer(const char* path) {
	OutputNode* node = (OutputNode*)m_nodes[0];
	if (node->m_output_type != OutputNode::OutputType::ARRAY) return;
	InputNode* inode = (InputNode*)addNode(NodeType::INPUT);
	inode->m_texture = path;
	Link& link = m_links.emplace();
	link.from = inode->m_id;
	link.to = node->m_id | (node->m_layers_count << 16);
	++node->m_layers_count;
}

Path CompositeTexture::getTerrainLayerPath(u32 layer) const {
	const OutputNode* node = (OutputNode*)m_nodes[0];
	if (node->m_output_type != OutputNode::OutputType::ARRAY) return Path();
	if (layer >= node->m_layers_count) return Path();

	const Node::Input input = node->getInput(layer);
	if (!input) return Path();

	if (input.node->getType() != NodeType::INPUT) return Path();
	InputNode* inode = (InputNode*)input.node;
	return Path(inode->m_texture);
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
		default: ASSERT(false); return false;
	}
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

	CompositeTexture::Node* output_node = m_resource->addNode(CompositeTexture::NodeType::OUTPUT);
	CompositeTexture::Node* const_node =m_resource->addNode(CompositeTexture::NodeType::CONSTANT);
	CompositeTexture::Link& link = m_resource->m_links.emplace();
	link.from = const_node->m_id;
	link.to = output_node->m_id;
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
	{ 'C', "Constant", CompositeTexture::NodeType::CONSTANT },
	{ 'F', "Vertical flip", CompositeTexture::NodeType::VERTICAL_FLIP },
	{ 'I', "Invert", CompositeTexture::NodeType::INVERT },
	{ 'M', "Merge", CompositeTexture::NodeType::MERGE },
	{ 'S', "Split", CompositeTexture::NodeType::SPLIT },
	{ 'T', "Input", CompositeTexture::NodeType::INPUT },
};

void CompositeTextureEditor::onCanvasClicked(ImVec2 pos, i32 hovered_link) {
	CompositeTexture::Node* n = nullptr;
	for (const auto& t : TYPES) {
		if (os::isKeyDown((os::Keycode)t.key)) {
			n = m_resource->addNode(t.type);
			break;
		}
	}
	if (n) {
		n->m_pos = pos;
		if (hovered_link >= 0) splitLink(*m_resource, hovered_link, m_resource->m_nodes.size() - 1);
		pushUndo(NO_MERGE_UNDO);
	}	
}

void CompositeTextureEditor::open(const Path& path) {
	m_is_open = true;
	FileSystem& fs = m_app.getEngine().getFileSystem();
	IAllocator& allocator = m_app.getAllocator();
	OutputMemoryStream content(allocator);
	if (fs.getContentSync(path, content)) {
		m_path = path;
		InputMemoryStream blob(content);
		deserialize(blob);
		pushUndo(NO_MERGE_UNDO);
	}
	else {
		logError("Could not load", path);
	}
}

bool CompositeTextureEditor::getSavePath() {
	char path[LUMIX_MAX_PATH];
	if (os::getSaveFilename(Span(path), "Composite texture\0*.ltc\0", "ltc")) {
		m_path = path;
		return true;
	}
	return false;
}

void CompositeTextureEditor::save() {
	if (!m_path.isEmpty() || getSavePath()) saveAs(m_path);
}

void CompositeTextureEditor::onWindowGUI() {
	if (!m_is_open) return;

	ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Composite texture", &m_is_open, ImGuiWindowFlags_MenuBar)) {
		m_has_focus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		if (ImGui::BeginMenuBar()) {
			if (ImGui::BeginMenu("File")) {
				menuItem(m_save_action, true);
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Edit")) {
				menuItem(m_undo_action, canUndo());
				menuItem(m_redo_action, canRedo());
				ImGui::EndMenu();
			}
			ImGui::EndMenuBar();
		}
		nodeEditorGUI(*m_resource);
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
