#pragma once

#include <QFrame>
#include "property_widget_base.h"


namespace Ui 
{
	class CameraWidget;
}

class CameraWidget : public PropertyWidgetBase
{
		Q_OBJECT

	public:
		explicit CameraWidget(QWidget* parent = NULL);
		~CameraWidget();

		virtual const char* getTitle() const override { return "Camera"; }
		virtual void onEntityProperties(Lux::PropertyListEvent& event) override;

private slots:
	void on_priorityInput_valueChanged(int arg1);

private:
		Ui::CameraWidget* m_ui;
};

