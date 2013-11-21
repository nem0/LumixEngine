#include "component_factory.h"
#include "graphics/renderable.h"
#include "graphics/point_light.h"
#include "graphics/camera.h"
#include "physics/physical.h"


namespace Lux
{


void ComponentFactory::registerCreator(EntityComponent::Type type, Creator creator)
{
	m_creators.insert(type, creator);
}


EntityComponent* ComponentFactory::create(EntityComponent::Type type, Entity& entity) const
{
	Creator c;
	if(!m_creators.find(type, c))
	{
		return 0;
	}
	return c(entity, *this);
}


void ComponentFactory::registerCreators()
{
	registerCreator(Renderable::getStaticType(), &Renderable::create);
	registerCreator(PointLight::getStaticType(), &PointLight::create);
	registerCreator(Camera::getStaticType(), &Camera::create);
	registerCreator(Physical::getStaticType(), &Physical::create);
}


} // !namespace Lux