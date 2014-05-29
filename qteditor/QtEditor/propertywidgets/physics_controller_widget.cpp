#include "physics_controller_widget.h"
#include "ui_physics_controller_widget.h"

PhysicsControllerWidget::PhysicsControllerWidget(QWidget* parent)
	: PropertyWidgetBase(parent)
	, m_ui(new Ui::PhysicsControllerWidget)
{
	m_ui->setupUi(this);
}

PhysicsControllerWidget::~PhysicsControllerWidget()
{
	delete m_ui;
}


const char* PhysicsControllerWidget::getTitle() const
{
	return "Physics Controller";
}

void PhysicsControllerWidget::onEntityProperties(Lux::PropertyListEvent& event) 
{
}

