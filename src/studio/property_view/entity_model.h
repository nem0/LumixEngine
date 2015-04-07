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

	private:
		void onComponentAdded(Lumix::Component component);
		void onComponentDestroyed(Lumix::Component component);
		void onPropertySet(Lumix::Component component, const Lumix::IPropertyDescriptor& descriptor);
		void onEntityPosition(const Lumix::Entity& entity);
		void onUniverseDestroyed();
		void onEntityDestroyed(const Lumix::Entity& entity);

		void addNameProperty();
		void addPositionProperty();
		void addComponentNode(Lumix::Component cmp, int row);
		void addComponent(QWidget* widget, QPoint pos);
		void setEntityPosition(int index, float value);
		void setEntityRotation(int index, float value);
		void reset(const QString& reason);
		void set(Lumix::Entity entity, uint32_t component_type, int index, Lumix::IPropertyDescriptor* desc, QVariant value);
		QVariant get(Lumix::Entity entity, uint32_t component_type, int index, Lumix::IPropertyDescriptor* desc);

	private:
		Lumix::WorldEditor& m_editor;
		Lumix::Entity m_entity;
};

