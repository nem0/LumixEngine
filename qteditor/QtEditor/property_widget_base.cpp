#include "property_widget_base.h"
#include "ui_property_widget_base.h"

PropertyWidgetBase::PropertyWidgetBase(QWidget *parent) :
	QFrame(parent),
	m_ui(new Ui::PropertyWidgetBase)
{
	m_ui->setupUi(this);
}

PropertyWidgetBase::~PropertyWidgetBase()
{
	delete m_ui;
}
