#pragma

#include <QFrame>
#include "property_widget_base.h"

namespace Ui
{
	class PhysicsBoxWidget;
}

class PhysicsBoxWidget : public PropertyWidgetBase
{
		Q_OBJECT

	public:
		explicit PhysicsBoxWidget(QWidget* parent = NULL);
		~PhysicsBoxWidget();

		virtual const char* getTitle() const override;
		virtual void onEntityProperties(Lux::PropertyListEvent& event) override;

	private slots:
		void on_isDynamicCheckBox_toggled(bool checked);

		void on_xSizeInput_valueChanged(double arg1);

		void on_ySizeInput_valueChanged(double arg1);

		void on_zSizeInput_valueChanged(double arg1);

	private:
		Ui::PhysicsBoxWidget* m_ui;
};

