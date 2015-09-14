#pragma once


#include "assimp/Importer.hpp"
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
	public:
		ImportAssetDialog(Lumix::WorldEditor& editor);
		void onGui();

	private:
		void checkSource();
		void import();

	private:
		Lumix::WorldEditor& m_editor;
		Assimp::Importer m_importer;
		char m_source[Lumix::MAX_PATH_LENGTH];
		char m_output_dir[Lumix::MAX_PATH_LENGTH];
		bool m_source_exists;
		Lumix::MT::Task* m_task;
};