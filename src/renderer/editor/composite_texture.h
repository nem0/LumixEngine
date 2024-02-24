#pragma once

#include "foundation/allocators.h"
#include "foundation/array.h"
#include "foundation/math.h"
#include "foundation/path.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "renderer/gpu/gpu.h"

namespace Lumix {

struct CompositeTexture {
	enum class NodeType : u32;
	
	struct Image {
		Image(IAllocator& allocator);
		Image(u32 w, u32 h, u32 channels, IAllocator& allocator);
		Vec4 sample(i32 x, i32 y);
		Vec4 sampleWrap(i32 x, i32 y);
		Vec4 sample(float x, float y);
		Vec4 sampleWrap(float x, float y);
		void setPixel(u32 x, u32 y, const Vec4& color);
		void init(u32 w, u32 h, u32 channels);
		OutputMemoryStream asU8() const;
		Array<float> pixels;
		u32 channels = 4;
		u32 w;
		u32 h;
	};

	struct Result {
		Result(IAllocator& allocator) : layers(allocator) {}
		Array<Image> layers;
		bool is_cubemap;
	};
	
	struct PixelContext {
		const Image* image;
		Vec4 color;
		u32 x;
		u32 y;
	};

	struct ValueResult {
		ValueResult() {}
		ValueResult(Vec4 v) : value(v), channels(4) {}
		ValueResult(float v) : value(v), channels(1) {}

		u32 channels = 0;
		Vec4 value;

		bool isValid() const { return channels != 0; }
		bool isFloat() const { return channels == 1; }
	};

	struct Node : NodeEditorNode {
		Node(IAllocator& allocator) : m_allocator(allocator), m_error(allocator), m_outputs(allocator) {}

		virtual NodeType getType() const = 0;
		virtual bool gui() = 0;
		virtual bool generateInternal() = 0;
		virtual ValueResult getValue(const PixelContext& ctx) { ASSERT(false); return {}; }
		virtual void serialize(OutputMemoryStream& blob) const {}
		virtual void deserialize(InputMemoryStream& blob) {}
		
		bool nodeGUI() override;

		void nodeTitle(const char* title);
		bool generate();
		void markDirty();
		void inputSlot();
		void outputSlot();
		struct Input;
		Input getInput(u32 pin_idx) const;
		Image& getInputImage(u32 pin_idx) const;
		bool generateInput(u32 pin_idx);
		bool getInputImage(u32 pin_idx, Image* pd);
		ValueResult getInputValue(u32 pin_idx, const CompositeTexture::PixelContext& ctx);
		
		ValueResult errorValue(const char* msg) {
			m_error = msg;
			return {};
		}

		bool error(const char* msg) {
			m_outputs.clear();
			m_error = msg;
			return false;
		}

		IAllocator& m_allocator;
		Array<Image> m_outputs;
		float m_generate_duration = -1;
		bool m_selected = false;
		bool m_reachable = false;
		bool m_dirty = true;
		u32 m_input_counter;
		u32 m_output_counter;
		CompositeTexture* m_resource;
		String m_error;
		gpu::TextureHandle m_preview = gpu::INVALID_TEXTURE;
	};

	using Link = NodeEditorLink;

	CompositeTexture(StudioApp& app, IAllocator& allocator);
	~CompositeTexture();
	bool loadSync(struct FileSystem& fs, const Path& path);
	bool save(struct FileSystem& fs, const Path& path);
	Node* addNode(CompositeTexture::NodeType type);
	Node* getNodeByID(u16 id) const;
	void deleteSelectedNodes();
	void deleteUnreachable();
	u32 getLayersCount() const;
	void clear();
	void initDefault();

	void serialize(OutputMemoryStream& blob);
	bool deserialize(InputMemoryStream& blob);
	bool generate(Result* result);
	
	void addArrayLayer(const Path& path);
	void removeArrayLayer(u32 idx);
	void initTerrainAlbedo();
	void initTerrainNormal();
	void link(Node* from, u32 from_pin, Node* to, u32 to_pin);

	IAllocator& m_allocator;
	StudioApp& m_app;
	Array<Node*> m_nodes;
	Array<Link> m_links;
	u32 m_node_id_generator = 1;
};

struct CompositeTextureEditor {
	virtual ~CompositeTextureEditor() {}

	virtual void gui() = 0;
	virtual void menu() = 0;
	virtual void save() = 0;
	virtual bool isDirty() const = 0;
	virtual void doUndo() = 0;
	virtual void doRedo() = 0;
	virtual void deleteSelectedNodes() = 0;
	
	static UniquePtr<CompositeTextureEditor> open(const Path& path, StudioApp& app, IAllocator& allocator);
};

} // namespace Lumix