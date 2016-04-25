#pragma once


#include "assimp/Importer.hpp"
#include "engine/core/array.h"
#include "engine/core/associative_array.h"
#include "engine/core/binary_array.h"
#include "engine/core/mt/sync.h"
#include "engine/core/string.h"
#include "engine/lumix.h"


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
	ImportTexture textures[16];
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
	Lumix::Array<unsigned int> map_to_input;
	Lumix::Array<unsigned int> map_from_input;
	Lumix::Array<Lumix::int32> indices;
};



class LUMIX_RENDERER_API ImportAssetDialog
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
		ImportAssetDialog(Lumix::WorldEditor& editor, Metadata& metadata);
		~ImportAssetDialog();
		void setMessage(const char* message);
		void setImportMessage(const char* message, float progress_fraction);
		Lumix::WorldEditor& getEditor() { return m_editor; }
		void onGUI();
		DDSConvertCallbackData& getDDSConvertCallbackData() { return m_dds_convert_callback; }
		int importAsset(lua_State* L);

	public:
		bool m_is_opened;

	private:
		void checkSource();
		void checkTask(bool wait);
		void convert(bool use_ui);
		void getMessage(char* msg, int max_size);
		bool hasMessage();
		void importTexture();
		bool isTextureDirValid() const;
		void onMaterialsGUI();
		void onMeshesGUI();
		void onImageGUI();
		void onLODsGUI();

	private:
		Lumix::WorldEditor& m_editor;
		Lumix::Array<Lumix::uint32> m_saved_textures;
		Lumix::Array<Assimp::Importer> m_importers;
		Lumix::Array<ImportMesh> m_meshes;
		Lumix::Array<ImportMaterial> m_materials;
		char m_import_message[1024];
		float m_progress_fraction;
		char m_message[1024];
		char m_last_dir[Lumix::MAX_PATH_LENGTH];
		char m_source[Lumix::MAX_PATH_LENGTH];
		char m_output_filename[Lumix::MAX_PATH_LENGTH];
		char m_output_dir[Lumix::MAX_PATH_LENGTH];
		char m_texture_output_dir[Lumix::MAX_PATH_LENGTH];
		bool m_create_billboard_lod;
		bool m_optimize_mesh_on_import;
		bool m_gen_smooth_normal;
		bool m_convert_to_dds;
		bool m_convert_to_raw;
		bool m_import_animations;
		bool m_make_convex;
		bool m_is_converting;
		bool m_is_importing;
		bool m_remove_doubles;
		bool m_is_importing_texture;
		float m_raw_texture_scale;
		float m_mesh_scale;
		float m_lods[4];
		Orientation m_orientation;
		Lumix::MT::Task* m_task;
		Lumix::MT::SpinMutex m_mutex;
		Metadata& m_metadata;
		DDSConvertCallbackData m_dds_convert_callback;
};