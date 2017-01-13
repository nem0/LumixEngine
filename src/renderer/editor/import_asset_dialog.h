#pragma once


#include "assimp/Importer.hpp"
#include "engine/array.h"
#include "engine/associative_array.h"
#include "engine/binary_array.h"
#include "engine/mt/sync.h"
#include "engine/string.h"
#include "engine/lumix.h"
#include "editor/studio_app.h"


struct lua_State;
class Metadata;


namespace Lumix
{

class WorldEditor;

namespace MT
{
class Task;
}

}


struct ImportTexture
{
	struct aiTexture* texture;
	char path[Lumix::MAX_PATH_LENGTH];
	char src[Lumix::MAX_PATH_LENGTH];
	bool import;
	bool to_dds;
	bool is_valid;
};


struct ImportMaterial
{
	const struct aiScene* scene;
	struct aiMaterial* material;
	bool import;
	bool alpha_cutout;
	int texture_count;
	char shader[20];
	char name[128];
	ImportTexture textures[16];
};


struct ImportAnimation
{
	bool import;
	struct aiAnimation* animation;
	int root_motion_bone_idx = -1;
	const aiScene* scene;
	char output_filename[Lumix::MAX_PATH_LENGTH];
};


struct ImportMesh
{
	ImportMesh(Lumix::IAllocator& allocator)
		: map_to_input(allocator)
		, map_from_input(allocator)
		, indices(allocator)
	{
	}

	int lod;
	bool import;
	bool import_physics;
	struct aiMesh* mesh;
	const aiScene* scene;
	int material;
	Lumix::Array<unsigned int> map_to_input;
	Lumix::Array<unsigned int> map_from_input;
	Lumix::Array<Lumix::i32> indices;
};



class ImportAssetDialog LUMIX_FINAL : public StudioApp::IPlugin
{
	friend struct ImportTask;
	friend struct ConvertTask;
	friend struct ImportTextureTask;
	public:
		enum Orientation : int
		{
			Y_UP,
			Z_UP,
			Z_MINUS_UP,
			X_MINUS_UP
		};

		struct DDSConvertCallbackData
		{
			ImportAssetDialog* dialog;
			const char* dest_path;
			bool cancel_requested;
		};

	public:
		ImportAssetDialog(StudioApp& app);
		~ImportAssetDialog();
		void setMessage(const char* message);
		void setImportMessage(const char* message, float progress_fraction);
		Lumix::WorldEditor& getEditor() { return m_editor; }
		void onWindowGUI() override;
		DDSConvertCallbackData& getDDSConvertCallbackData() { return m_dds_convert_callback; }
		int importAsset(lua_State* L);
		const char* getName() const override { return "import_asset"; }

	public:
		bool m_is_opened;

	private:
		bool checkSource();
		void checkTask(bool wait);
		void convert(bool use_ui);
		void import();
		void getMessage(char* msg, int max_size);
		bool hasMessage();
		void importTexture();
		bool isTextureDirValid() const;
		void onMaterialsGUI();
		void onMeshesGUI();
		void onAnimationsGUI();
		void onImageGUI();
		void onLODsGUI();
		void onAction();
		void saveModelMetadata();
		bool isOpened() const;
		void clearSources();
		void addSource(const char* src);

	public:
		Lumix::WorldEditor& m_editor;
		Lumix::Array<Lumix::u32> m_saved_textures;
		Lumix::Array<Assimp::Importer> m_importers;
		Lumix::Array<Lumix::StaticString<Lumix::MAX_PATH_LENGTH> > m_sources;
		Lumix::Array<ImportAnimation> m_animations;
		Lumix::Array<ImportMesh> m_meshes;
		Lumix::Array<ImportMaterial> m_materials;
		char m_import_message[1024];
		
		struct ModelData
		{
			float mesh_scale;
			float lods[4];
			bool create_billboard_lod;
			bool optimize_mesh_on_import;
			bool gen_smooth_normal;
			bool remove_doubles;
			Orientation orientation;
			Orientation root_orientation;
			bool make_convex;
			bool import_vertex_colors;
			bool all_nodes;
			float position_error;
			float rotation_error;
			float time_scale;
		} m_model;

		float m_progress_fraction;
		char m_message[1024];
		char m_last_dir[Lumix::MAX_PATH_LENGTH];
		char m_source[Lumix::MAX_PATH_LENGTH];
		char m_mesh_output_filename[Lumix::MAX_PATH_LENGTH];
		char m_output_dir[Lumix::MAX_PATH_LENGTH];
		char m_texture_output_dir[Lumix::MAX_PATH_LENGTH];
		bool m_convert_to_dds;
		bool m_convert_to_raw;
		bool m_is_converting;
		bool m_is_importing;
		bool m_is_importing_texture;
		float m_raw_texture_scale;
		Lumix::MT::Task* m_task;
		Lumix::MT::SpinMutex m_mutex;
		Metadata& m_metadata;
		DDSConvertCallbackData m_dds_convert_callback;
};