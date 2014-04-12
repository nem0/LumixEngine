#include "animable_widget.h"
#include "ui_animable_widget.h"

AnimableWidget::AnimableWidget(QWidget *parent) :
	PropertyWidgetBase(parent),
	m_ui(new Ui::AnimableWidget)
{
	m_ui->setupUi(this);
}

AnimableWidget::~AnimableWidget()
{
	delete m_ui;
}

void AnimableWidget::onEntityProperties(Lux::PropertyListEvent&)
{
}
