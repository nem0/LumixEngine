#pragma once


#include <QFrame>
#include "core/array.h"
#include "core/string.h"


namespace Lux
{
	class EditorClient;
	struct PropertyListEvent;
}


namespace Ui
{
	class PropertyWidgetBase;
}


class PropertyWidgetBase : public QFrame
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
			Lux::string m_name;
			Lux::string m_file_type;
			uint32_t m_name_hash;
			QWidget* m_widget;
	};

public:
	explicit PropertyWidgetBase(QWidget* parent = NULL);
	~PropertyWidgetBase();
	
	void setEditorClient(Lux::EditorClient& client) { m_client = &client; }
	void setComponentType(const char* type) { m_component_type = type; }
	void setTitle(const char* title) { m_widget_title = title; }
	const char* getTitle() const { return m_widget_title.c_str(); }
	void onEntityProperties(Lux::PropertyListEvent&);
	void addProperty(const char* name, const char* label, Property::Type type, const char* file_type);


private slots:
	void browseFile();
	void setString();
	void setDecimal();
	void setVec3();
	void setBool();

private:
	Ui::PropertyWidgetBase* m_ui;
	Lux::EditorClient* m_client;
	Lux::Array<Property> m_properties;
	Lux::string m_widget_title;
	Lux::string m_component_type;
	class QFormLayout* m_form_layout;
};
