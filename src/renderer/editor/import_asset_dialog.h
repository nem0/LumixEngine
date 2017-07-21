#pragma once


#include "engine/array.h"
#include "engine/associative_array.h"
#include "engine/binary_array.h"
#include "engine/mt/sync.h"
#include "engine/string.h"
#include "engine/lumix.h"
#include "editor/studio_app.h"


struct aiAnimation;
struct aiMaterial;
struct aiMesh;
struct lua_State;
class Metadata;


namespace Lumix
{


class WorldEditor;
namespace MT { class Task; }


class ImportAssetDialog LUMIX_FINAL : public StudioApp::IPlugin
{
	friend struct ImportTask;
	friend struct ConvertTask;
	friend struct ImportTextureTask;
	public:
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
		WorldEditor& getEditor() { return m_editor; }
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
		WorldEditor& m_editor;
		Array<u32> m_saved_textures;
		Array<StaticString<MAX_PATH_LENGTH> > m_sources;
		char m_import_message[1024];
		
		struct ImageData
		{
			u8* data;
			int width;
			int height;
			int comps;
			int resize_size[2];
		} m_image;
		
		struct ModelData
		{
			bool create_billboard_lod;
		} m_model;

		float m_progress_fraction;
		char m_message[1024];
		char m_last_dir[MAX_PATH_LENGTH];
		char m_source[MAX_PATH_LENGTH];
		char m_mesh_output_filename[MAX_PATH_LENGTH];
		char m_output_dir[MAX_PATH_LENGTH];
		char m_texture_output_dir[MAX_PATH_LENGTH];
		bool m_convert_to_dds;
		bool m_convert_to_raw;
		bool m_is_normal_map;
		bool m_is_importing_texture;
		float m_raw_texture_scale;
		MT::Task* m_task;
		MT::SpinMutex m_mutex;
		Metadata& m_metadata;
		DDSConvertCallbackData m_dds_convert_callback;
		struct FBXImporter* m_fbx_importer;
};


} // namespace Lumix