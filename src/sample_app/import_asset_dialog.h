#pragma once


#include "assimp/Importer.hpp"
#include "core/array.h"
#include "core/associative_array.h"
#include "core/mt/spin_mutex.h"
#include "core/string.h"
#include "lumix.h"


namespace Lumix
{

class WorldEditor;

namespace MT
{
	class Task;
}

}


class ImportAssetDialog
{
	friend struct ImportTask;
	friend struct ConvertTask;
	friend struct ImportTextureTask;
	public:
		ImportAssetDialog(Lumix::WorldEditor& editor);
		~ImportAssetDialog();
		void setErrorMessage(const char* message);
		void setImportMessage(const char* message);

		void onGui();

	public:
		bool m_is_opened;

	private:
		void checkSource();
		void convert();
		void getErrorMessage(char* msg, int max_size);
		bool hasErrorMessage();
		bool checkTextures();
		bool checkTexture(const char* source_dir, const char* path, const char* message);
		void importTexture();

	private:
		Lumix::WorldEditor& m_editor;
		Lumix::Array<Lumix::string> m_saved_textures;
		Lumix::Array<Lumix::string> m_saved_embedded_textures;
		Assimp::Importer m_importer;
		Lumix::AssociativeArray<Lumix::string, Lumix::string> m_path_mapping;
		char m_import_message[1024];
		char m_error_message[1024];
		char m_source[Lumix::MAX_PATH_LENGTH];
		char m_output_dir[Lumix::MAX_PATH_LENGTH];
		bool m_source_exists;
		bool m_gen_smooth_normal;
		bool m_create_directory;
		bool m_import_materials;
		bool m_convert_to_dds;
		bool m_import_animations;
		bool m_import_physics;
		bool m_is_converting;
		bool m_is_importing;
		bool m_make_convex;
		bool m_is_importing_texture;
		Lumix::MT::Task* m_task;
		Lumix::MT::SpinMutex m_mutex;
};