#pragma once



namespace Lumix
{
class Engine;
}


class FileSystemUI
{
	public:
		virtual ~FileSystemUI() {}
		virtual void onGUI() = 0;

		static FileSystemUI* create(Lumix::Engine& engine);
		static void destroy(FileSystemUI& ui);

		bool m_is_opened;
};