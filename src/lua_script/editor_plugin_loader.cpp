#include "editor_plugin_loader.h"
#include "core/crc32.h"
#include "core/log.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/json_serializer.h"
#include "core/lua_wrapper.h"
#include "editor/ieditor_command.h"
#include "editor/world_editor.h"
#include "engine.h"
#include "mainwindow.h"
#include <lua.hpp>
#include <qbuffer.h>
#include <qdir.h>
#include <qmenubar.h>
#include <qpushbutton.h>
#include <qtextedit.h>
#include <quiloader.h>


namespace Lumix
{
namespace LuaWrapper
{


template <> inline void pushLua(lua_State* L, QString value)
{
	lua_pushstring(L, value.toLatin1().data());
}


} // namespace LuaWrapper
} // namespace Lumix


EditorPluginLoader::EditorPluginLoader(MainWindow& main_window)
	: m_main_window(main_window)
{
	m_global_state = nullptr;
	setWorldEditor(main_window.getWorldEditor());
}


EditorPluginLoader::~EditorPluginLoader()
{
	if (m_global_state)
	{
		lua_close(m_global_state);
	}
}


namespace API
{


static void
registerMenuFunction(lua_State* L, const char* name, const char* function)
{
	if (lua_getglobal(L, "API_plugin_loader") == LUA_TLIGHTUSERDATA)
	{
		auto loader = (EditorPluginLoader*)lua_touserdata(L, -1);
		auto* menu = loader->getMainWindow().getToolsMenu();
		auto* action = menu->addAction(name);
		QString func = function;
		auto callback = [func, L]()
		{
			if (lua_getglobal(L, func.toLatin1().data()) == LUA_TFUNCTION)
			{
				bool error = lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK;
				if (error)
				{
					Lumix::g_log_error.log("editor") << ": "
													 << lua_tostring(L, -1);
					lua_pop(L, 1);
				}
			}
			else
			{
				Lumix::g_log_error.log("editor") << "Lua function "
												 << func.toLatin1().data()
												 << " not found.";
				lua_pop(L, 1);
			}
		};
		action->connect(action, &QAction::triggered, callback);
	}
	lua_pop(L, 1);
}


static QString getTextEditText(void* widget_ptr, const char* child_name)
{
	QWidget* widget = (QWidget*)widget_ptr;
	if (!widget)
	{
		return "";
	}
	auto* edit = widget->findChild<QTextEdit*>(child_name);
	if (!edit)
	{
		return "";
	}

	return edit->toPlainText();
}


static void registerButtonClickCallback(lua_State* L,
										void* widget_ptr,
										const char* child_name,
										const char* function_name)
{
	QWidget* widget = (QWidget*)widget_ptr;
	if (!widget)
	{
		return;
	}
	auto* button = widget->findChild<QPushButton*>(child_name);
	if (!button)
	{
		return;
	}
	QString func = function_name;
	auto callback = [L, func]()
	{
		if (lua_getglobal(L, func.toLatin1().data()) == LUA_TFUNCTION)
		{
			bool error = lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK;
			if (error)
			{
				Lumix::g_log_error.log("editor") << ": " << lua_tostring(L, -1);
				lua_pop(L, 1);
			}
		}
		else
		{
			Lumix::g_log_error.log("editor")
				<< "Lua function " << func.toLatin1().data() << " not found.";
			lua_pop(L, 1);
		}
	};
	button->connect(button, &QPushButton::clicked, callback);
}


static void* createUI(const char* ui)
{
	QUiLoader loader;
	QBuffer buffer;
	buffer.setData(ui, strlen(ui));
	auto* widget = loader.load(&buffer);
	widget->show();
	return widget;
}


static void logError(const char* text)
{
	Lumix::g_log_error.log("editor") << text;
}


static void logWarning(const char* text)
{
	Lumix::g_log_warning.log("editor") << text;
}


static void logInfo(const char* text)
{
	Lumix::g_log_info.log("editor") << text;
}


static void
executeEditorCommand(lua_State* L, const char* name, const char* data)
{
	if (lua_getglobal(L, "API_plugin_loader") == LUA_TLIGHTUSERDATA)
	{
		auto loader = (EditorPluginLoader*)lua_touserdata(L, -1);
		auto& editor = loader->getMainWindow().getWorldEditor();
		auto& engine = editor.getEngine();
		auto* command = editor.createEditorCommand(Lumix::crc32(name));
		if (command)
		{
			Lumix::FS::IFile* file = engine.getFileSystem().open(
				engine.getFileSystem().getMemoryDevice(),
				"",
				Lumix::FS::Mode::WRITE);
			ASSERT(file);
			file->write(data, strlen(data));
			file->seek(Lumix::FS::SeekMode::BEGIN, 0);
			Lumix::JsonSerializer serializer(
				*file, Lumix::JsonSerializer::READ, "", engine.getAllocator());
			command->deserialize(serializer);
			editor.executeCommand(command);
			engine.getFileSystem().close(*file);
		}
	}
	lua_pop(L, 1);
}


} // namespace API


void EditorPluginLoader::registerAPI()
{
	luaL_openlibs(m_global_state);
	lua_pushlightuserdata(m_global_state, this);
	lua_setglobal(m_global_state, "API_plugin_loader");

	auto f = Lumix::LuaWrapper::wrap<decltype(&API::registerMenuFunction),
									 API::registerMenuFunction>;
	lua_register(m_global_state, "API_registerMenuFunction", f);

	f = Lumix::LuaWrapper::wrap<decltype(&API::logError), API::logError>;
	lua_register(m_global_state, "API_logError", f);

	f = Lumix::LuaWrapper::wrap<decltype(&API::logWarning), API::logWarning>;
	lua_register(m_global_state, "API_logWarning", f);

	f = Lumix::LuaWrapper::wrap<decltype(&API::logInfo), API::logInfo>;
	lua_register(m_global_state, "API_logInfo", f);

	f = Lumix::LuaWrapper::wrap<decltype(&API::executeEditorCommand),
								API::executeEditorCommand>;
	lua_register(m_global_state, "API_executeEditorCommand", f);

	f = Lumix::LuaWrapper::wrap<decltype(&API::createUI), API::createUI>;
	lua_register(m_global_state, "API_createUI", f);

	f = Lumix::LuaWrapper::wrap<decltype(&API::registerButtonClickCallback),
								API::registerButtonClickCallback>;
	lua_register(m_global_state, "API_registerButtonClickCallback", f);

	f = Lumix::LuaWrapper::wrap<decltype(&API::getTextEditText),
								API::getTextEditText>;
	lua_register(m_global_state, "API_getTextEditText", f);
}


void EditorPluginLoader::setWorldEditor(Lumix::WorldEditor& editor)
{
	m_global_state = luaL_newstate();
	registerAPI();
	auto lua_plugins = QDir("plugins").entryInfoList(QStringList() << "*.lua");
	for (auto& lua_plugin : lua_plugins)
	{
		QFile file(lua_plugin.absoluteFilePath());
		if (file.open(QIODevice::ReadOnly))
		{
			auto content = file.readAll();
			bool errors =
				luaL_loadbuffer(
					m_global_state,
					content.data(),
					content.size(),
					lua_plugin.absoluteFilePath().toLatin1().data()) != LUA_OK;
			errors = errors ||
					 lua_pcall(m_global_state, 0, LUA_MULTRET, 0) != LUA_OK;
			if (errors)
			{
				Lumix::g_log_error.log("editor")
					<< lua_plugin.absoluteFilePath().toLatin1().data() << ": "
					<< lua_tostring(m_global_state, -1);
				lua_pop(m_global_state, 1);
			}
		}
		else
		{
			Lumix::g_log_warning.log("editor")
				<< "Could not open plugin "
				<< lua_plugin.absoluteFilePath().toLatin1().data();
		}
	}
}
