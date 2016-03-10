#pragma once


#include "assimp/Importer.hpp"
#include "core/array.h"
#include "core/associative_array.h"
#include "core/binary_array.h"
#include "core/mt/sync.h"
#include "core/string.h"
#include "lumix.h"


class Metadata;


namespace Lumix
{

class WorldEditor;

namespace MT
{
	class Task;
}

}


class LUMIX_EDITOR_API ImportAssetDialog
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

	public:
		bool m_is_opened;

	private:
		void checkSource();
		void convert();
		void getMessage(char* msg, int max_size);
		bool hasMessage();
		bool checkTextures();
		bool checkTexture(const char* source_dir, const char* path, const char* message);
		void importTexture();
		bool isTextureDirValid() const;

	private:
		Lumix::WorldEditor& m_editor;
		Lumix::Array<Lumix::string> m_saved_textures;
		Lumix::Array<Lumix::string> m_saved_embedded_textures;
		Assimp::Importer m_importer;
		Lumix::AssociativeArray<Lumix::string, Lumix::string> m_path_mapping;
		Lumix::BinaryArray m_mesh_mask;
		char m_import_message[1024];
		float m_progress_fraction;
		char m_message[1024];
		char m_last_dir[Lumix::MAX_PATH_LENGTH];
		char m_source[Lumix::MAX_PATH_LENGTH];
		char m_output_dir[Lumix::MAX_PATH_LENGTH];
		char m_texture_output_dir[Lumix::MAX_PATH_LENGTH];
		bool m_source_exists;
		bool m_optimize_mesh_on_import;
		bool m_gen_smooth_normal;
		bool m_import_materials;
		bool m_convert_to_dds;
		bool m_import_textures;
		bool m_convert_to_raw;
		bool m_import_animations;
		bool m_import_physics;
		bool m_import_model;
		bool m_is_converting;
		bool m_is_importing;
		bool m_make_convex;
		bool m_is_importing_texture;
		float m_raw_texture_scale;
		float m_mesh_scale;
		Orientation m_orientation;
		Lumix::MT::Task* m_task;
		Lumix::MT::SpinMutex m_mutex;
		Metadata& m_metadata;
		DDSConvertCallbackData m_dds_convert_callback;
};