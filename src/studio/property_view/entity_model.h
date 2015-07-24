#pragma once


#include "dynamic_object_model.h"
#include "universe/component.h"


namespace Lumix
{
	class IArrayDescriptor;
	class IPropertyDescriptor;
	class Universe;
	class WorldEditor;
}

class PropertyView;


class EntityModel : public DynamicObjectModel
{
	public:
		EntityModel(PropertyView& view, Lumix::WorldEditor& editor, Lumix::Entity entity);
		~EntityModel();

	private:
		Lumix::Universe* getUniverse();

		void onComponentAdded(Lumix::ComponentOld component);
		void onComponentDestroyed(Lumix::ComponentOld component);
		void onPropertySet(Lumix::ComponentOld component, const Lumix::IPropertyDescriptor& descriptor);
		void onEntityPosition(Lumix::Entity entity);
		void onUniverseDestroyed();
		void onEntityDestroyed(Lumix::Entity entity);

		const char* getComponentName(Lumix::ComponentOld cmp) const;
		void addArrayProperty(Node& child, Lumix::IArrayDescriptor* desc, Lumix::ComponentOld cmp);
		void addResourceProperty(Node& child, Lumix::IPropertyDescriptor* desc, Lumix::ComponentOld cmp);
		void addNameProperty();
		void addPositionProperty();
		void addComponentNode(Lumix::ComponentOld cmp, int row);
		void addComponent(QWidget* widget, QPoint pos);
		void setEntityPosition(int index, float value);
		void setEntityRotation(int index, float value);
		void reset(const QString& reason);
		void set(Lumix::Entity entity, uint32_t component_type, int index, Lumix::IPropertyDescriptor* desc, QVariant value);
		QVariant get(Lumix::Entity entity, uint32_t component_type, int index, Lumix::IPropertyDescriptor* desc);

	private:
		Lumix::WorldEditor& m_editor;
		Lumix::Entity m_entity;
		PropertyView& m_view;
		bool m_is_setting;
};

