#pragma once

#include <QFrame>
#include "property_widget_base.h"

namespace Ui
{
	class ScriptWidget;
}

class ScriptWidget : public PropertyWidgetBase
{
	Q_OBJECT

public:
	explicit ScriptWidget(QWidget* parent = NULL);
	~ScriptWidget();
	virtual const char* getTitle() const override { return "Script"; }
	virtual void onEntityProperties(Lux::PropertyListEvent& event) override;

private slots:
	void on_browseSourceButton_clicked();
	void on_sourceEdit_editingFinished();

private:
	Ui::ScriptWidget* m_ui;
};

