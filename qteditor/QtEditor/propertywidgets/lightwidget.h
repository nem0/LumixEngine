#pragma once


#include <QFrame>
#include "property_widget_base.h"


namespace Ui {
	class LightWidget;
}

class LightWidget : public PropertyWidgetBase
{
		Q_OBJECT

	public:
		explicit LightWidget(QWidget* parent = NULL);
		~LightWidget();

		virtual const char* getTitle() const override { return "Light"; }
		virtual void onEntityProperties(Lux::PropertyListEvent&) override {}

	private:
		Ui::LightWidget* m_ui;
};


