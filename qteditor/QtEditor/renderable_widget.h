#pragma once

#include <QFrame>
#include "property_widget_base.h"


namespace Ui 
{
	class RenderableWidget;
}

class RenderableWidget : public PropertyWidgetBase
{
	Q_OBJECT

public:
	explicit RenderableWidget(QWidget* parent = NULL);
	~RenderableWidget();
	virtual const char* getTitle() const override { return "Renderable"; }
	virtual void onEntityProperties(Lux::PropertyListEvent& event) override;

private slots:
	void on_browseSource_clicked();
	void on_sourceEdit_editingFinished();

private:
	Ui::RenderableWidget* m_ui;
};
