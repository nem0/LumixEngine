#pragma once

#include <QFrame>
#include "property_widget_base.h"

namespace Ui
{
	class PhysicsControllerWidget;
}

class PhysicsControllerWidget : public PropertyWidgetBase
{
		Q_OBJECT

	public:
		explicit PhysicsControllerWidget(QWidget* parent = NULL);
		~PhysicsControllerWidget();

		virtual const char* getTitle() const override;
		virtual void onEntityProperties(Lux::PropertyListEvent& event) override;

	private:
		Ui::PhysicsControllerWidget* m_ui;
};
