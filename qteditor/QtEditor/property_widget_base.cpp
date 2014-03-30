#include "property_widget_base.h"
#include "ui_property_widget_base.h"

PropertyWidgetBase::PropertyWidgetBase(QWidget *parent) :
	QFrame(parent),
	ui(new Ui::PropertyWidgetBase)
{
	ui->setupUi(this);
}

PropertyWidgetBase::~PropertyWidgetBase()
{
	delete ui;
}
