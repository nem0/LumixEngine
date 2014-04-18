#include "lightwidget.h"
#include "ui_lightwidget.h"


LightWidget::LightWidget(QWidget *parent) 
	: PropertyWidgetBase(parent)
	, m_ui(new Ui::LightWidget)
{
	m_ui->setupUi(this);
}


LightWidget::~LightWidget()
{
	delete m_ui;
}
