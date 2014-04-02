#pragma once


#include <QFrame>


namespace Lux
{
	class EditorClient;
	struct PropertyListEvent;
}


namespace Ui {
class PropertyWidgetBase;
}


class PropertyWidgetBase : public QFrame
{
	Q_OBJECT

public:
	explicit PropertyWidgetBase(QWidget *parent = NULL);
	~PropertyWidgetBase();
	void setEditorClient(Lux::EditorClient& client) { m_client = &client; }
	virtual const char* getTitle() const = 0;
	virtual void onEntityProperties(Lux::PropertyListEvent& event) = 0;

protected:
	Lux::EditorClient* getClient() { return m_client; }

private:
	Ui::PropertyWidgetBase *m_ui;
	Lux::EditorClient* m_client;
};
