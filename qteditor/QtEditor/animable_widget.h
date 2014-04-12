#pragma once

#include <QFrame>
#include "property_widget_base.h"

namespace Ui
{
	class AnimableWidget;
}

class AnimableWidget : public PropertyWidgetBase
{
	Q_OBJECT

	public:
		explicit AnimableWidget(QWidget* parent = NULL);
		~AnimableWidget();
		virtual const char* getTitle() const override { return "Animable"; }
		virtual void onEntityProperties(Lux::PropertyListEvent& event) override;

	private:
		Ui::AnimableWidget* m_ui;
};

