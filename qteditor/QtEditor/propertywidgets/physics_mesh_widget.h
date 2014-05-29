#pragma once

#include <QFrame>
#include "property_widget_base.h"

namespace Ui
{
	class PhysicsMeshWidget;
}

class PhysicsMeshWidget : public PropertyWidgetBase
{
		Q_OBJECT

	public:
		explicit PhysicsMeshWidget(QWidget* parent = NULL);
		~PhysicsMeshWidget();

		virtual const char* getTitle() const override;
		virtual void onEntityProperties(Lux::PropertyListEvent& event) override;

	private slots:
		void on_lineEdit_editingFinished();
		void on_browseButton_clicked();

	private:
		Ui::PhysicsMeshWidget* m_ui;
};

