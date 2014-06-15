#pragma once


#include <QDockWidget>
#include "core/array.h"
#include "core/string.h"

namespace Lux
{
	class EditorClient;
	class Event;
}

namespace Ui
{
	class PropertyView;
}

class QTreeWidgetItem;

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
			Lux::string m_name;
			Lux::string m_file_type;
			Lux::string m_component_name;
			uint32_t m_name_hash;
			QTreeWidgetItem* m_tree_item;
	};

public:
	explicit PropertyView(QWidget* parent = NULL);
	~PropertyView();
	void setEditorClient(Lux::EditorClient& client);

private slots:
	void on_addComponentButton_clicked();
	void on_checkboxStateChanged();
	void on_doubleSpinBoxValueChanged();
	void on_vec3ValueChanged();
	void on_lineEditEditingFinished();
	void on_browseFilesClicked();

private:
	void clear();
	void onPropertyList(Lux::Event& event);
	void onEntitySelected(Lux::Event& event);
	void addProperty(const char* component, const char* name, const char* label, Property::Type type, const char* file_type);
	void onPropertyValue(Property* property, void* data, int32_t data_size);

private:
	Ui::PropertyView* m_ui;
	Lux::EditorClient* m_client;
	Lux::Array<Property*> m_properties;
};


