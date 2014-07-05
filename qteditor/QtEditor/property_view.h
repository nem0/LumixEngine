#pragma once


#include <QDockWidget>
#include "core/array.h"
#include "core/string.h"

namespace Lumix
{
	class EditorClient;
	struct EntitySelectedEvent;
	class Event;
	class Path;
	struct PropertyListEvent;
}

namespace Ui
{
	class PropertyView;
}

class QTreeWidgetItem;
class ScriptCompiler;

class PropertyView : public QDockWidget
{
	Q_OBJECT

public:
	class Property
	{
		public:
			enum Type
			{
				FILE,
				STRING,
				DECIMAL,
				VEC3,
				BOOL
			};
				
			Type m_type;
			uint32_t m_component;
			Lumix::string m_name;
			Lumix::string m_file_type;
			Lumix::string m_component_name;
			uint32_t m_name_hash;
			QTreeWidgetItem* m_tree_item;
	};

public:
	explicit PropertyView(QWidget* parent = NULL);
	~PropertyView();
	void setEditorClient(Lumix::EditorClient& client);
	Lumix::EditorClient* getEditorClient();
	void setScriptCompiler(ScriptCompiler* compiler);

private slots:
	void on_addComponentButton_clicked();
	void on_checkboxStateChanged();
	void on_doubleSpinBoxValueChanged();
	void on_vec3ValueChanged();
	void on_lineEditEditingFinished();
	void on_browseFilesClicked();
	void on_compileScriptClicked();
	void on_editScriptClicked();

private:
	void clear();
	void onPropertyList(Lumix::PropertyListEvent& event);
	void onEntitySelected(Lumix::EntitySelectedEvent& event);
	void addProperty(const char* component, const char* name, const char* label, Property::Type type, const char* file_type);
	void onPropertyValue(Property* property, void* data, int32_t data_size);
	void addScriptCustomProperties();
	void onScriptCompiled(const Lumix::Path& path, uint32_t status);
	void setScriptStatus(uint32_t status);

private:
	Ui::PropertyView* m_ui;
	Lumix::EditorClient* m_client;
	Lumix::Array<Property*> m_properties;
	ScriptCompiler* m_compiler;
};


