#pragma once


#include "dynamic_object_model.h"
#include "universe/entity.h"


namespace Lumix
{
	class IPropertyDescriptor;
	class WorldEditor;
}


class EntityModel : public DynamicObjectModel
{
public:
	EntityModel(Lumix::WorldEditor& editor, Lumix::Entity entity);
	~EntityModel();
	void onComponentAdded(Lumix::Component component);
	void onComponentDestroyed(Lumix::Component component);
	void onPropertySet(Lumix::Component component, const Lumix::IPropertyDescriptor& descriptor);
	void addNameProperty();
	void setEntityPosition(int index, float value);
	void addPositionProperty();
	void onEntityPosition(const Lumix::Entity& entity);
	void addComponentNode(Lumix::Component cmp);
	void addComponent(QWidget* widget, QPoint pos);
	void set(Lumix::Component cmp, int index, Lumix::IPropertyDescriptor* desc, QVariant value);
	QVariant get(Lumix::Component cmp, int index, Lumix::IPropertyDescriptor* desc);
	Lumix::Entity getEntity() const;

private:
	Lumix::WorldEditor& m_editor;
	Lumix::Entity m_entity;
};

