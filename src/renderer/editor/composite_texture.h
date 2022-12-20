#pragma once

#include "engine/array.h"
#include "engine/path.h"
#include "editor/studio_app.h"
#include "editor/utils.h"

namespace Lumix {

struct CompositeTexture {
	enum class NodeType : u32;
	
	struct PixelData {
		PixelData(IAllocator& allocator) : pixels(allocator) {}
		OutputMemoryStream pixels;
		u32 channels = 4;
		u32 w;
		u32 h;
	};

	struct Result {
		Result(IAllocator& allocator) : layers(allocator) {}
		Array<PixelData> layers;
		bool is_cubemap;
	};
	
	struct Node : NodeEditorNode {
		virtual NodeType getType() const = 0;
		virtual bool gui() = 0;
		virtual bool getPixelData(PixelData* data, u32 output_idx) = 0;
		virtual void serialize(OutputMemoryStream& blob) const {}
		virtual void deserialize(InputMemoryStream& blob) {}
		
		bool nodeGUI() override;

		void inputSlot();
		void outputSlot();
		struct Input;
		Input getInput(u32 pin_idx) const;
		bool getInputPixelData(u32 pin_idx, PixelData* pd) const;
		
		bool error(const char* msg) const {
			m_resource->m_error = msg;
			return false;
		}

		bool m_selected = false;
		u32 m_input_counter;
		u32 m_output_counter;
		CompositeTexture* m_resource;
	};

	using Link = NodeEditorLink;

	CompositeTexture(StudioApp& app, IAllocator& allocator);
	~CompositeTexture();
	bool loadSync(struct FileSystem& fs, const Path& path);
	bool save(struct FileSystem& fs, const Path& path);
	Node* addNode(CompositeTexture::NodeType type);
	Node* getNodeByID(u16 id) const;
	void deleteSelectedNodes();
	u32 getLayersCount() const;
	void clear();
	void initDefault();

	void serialize(OutputMemoryStream& blob);
	bool deserialize(InputMemoryStream& blob);
	bool generate(Result* result);
	
	void addArrayLayer(const char* path);
	void removeArrayLayer(u32 idx);
	void initTerrainAlbedo();
	void initTerrainNormal();
	void link(Node* from, u32 from_pin, Node* to, u32 to_pin);

	IAllocator& m_allocator;
	StudioApp& m_app;
	Array<Node*> m_nodes;
	Array<Link> m_links;
	u32 m_node_id_generator = 1;
	String m_error;
};

struct CompositeTextureEditor final : StudioApp::GUIPlugin, NodeEditor {
	CompositeTextureEditor(StudioApp& app);
	~CompositeTextureEditor();

	void open(const char* path);

private:
	void exportAs();
	bool saveAs(const char* path);
	void newGraph();
	void onWindowGUI() override;
	const char* getName() const override { return "composite_texture_editor"; }
	void onSettingsLoaded() override;
	void onBeforeSettingsSaved() override;

	bool getSavePath();
	bool hasFocus() override { return m_has_focus; }
	void deserialize(InputMemoryStream& blob) override;
	void serialize(OutputMemoryStream& blob) override;
	void onCanvasClicked(ImVec2 pos, i32 hovered_link) override;
	void onLinkDoubleClicked(CompositeTexture::Link& link, ImVec2 pos) override;
	void onContextMenu(ImVec2 pos) override;
	void toggleOpen() { m_is_open = !m_is_open; }
	bool isOpen() const { return m_is_open; }
	void save();
	void deleteSelectedNodes();

	StudioApp& m_app;
	IAllocator& m_allocator;
	bool m_is_open = false;
	Action m_toggle_ui;
	Action m_save_action;
	Action m_delete_action;
	Action m_undo_action;
	Action m_redo_action;
	Path m_path;
	CompositeTexture* m_resource = nullptr;
	bool m_has_focus = false;
	bool m_show_save_as = false;
	bool m_show_open = false;
	RecentPaths m_recent_paths;
};

} // namespace Lumix