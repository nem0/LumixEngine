#pragma once


#include <QDockWidget>
#include "core/array.h"


namespace Lux
{
	class EditorClient;
	class Event;
}

namespace Ui {
class PropertyView;
}

class PropertyView : public QDockWidget
{
	Q_OBJECT

public:
	explicit PropertyView(QWidget *parent = 0);
	~PropertyView();
	void setEditorClient(Lux::EditorClient& client);

private slots:
	void on_addComponentButton_clicked();

private:
	void onPropertyList(Lux::Event& event);
	void onEntitySelected(Lux::Event& event);

private:
	Ui::PropertyView *m_ui;
	Lux::EditorClient* m_client;
	Lux::Array<class PropertyWidgetBase*> m_component_uis;
};


