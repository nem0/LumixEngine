#pragma once


#include "core/lumix.h"
#include "engine/iplugin.h"
#include "core/string.h"


namespace Lumix
{
	namespace FS
	{
		class FileSystem;
	};

	class Animation;
	class Engine;
	struct Entity;
	class ISerializer;
	class Universe;

	class LUMIX_ENGINE_API AnimationSystem : public IPlugin
	{
		public:
			AnimationSystem() { m_impl = 0; }

			virtual bool create(Engine&) override;
			virtual void update(float time_delta) override;
			virtual void onCreateUniverse(Universe& universe) override;
			virtual void onDestroyUniverse(Universe& universe) override;
			virtual void serialize(ISerializer& serializer) override;
			virtual void deserialize(ISerializer& serializer) override;
			virtual Component createComponent(uint32_t, const Entity&) override;
			virtual const char* getName() const override { return "animation"; }

			void setFrame(Component cmp, int frame);
			bool isManual(Component cmp);
			void setManual(Component cmp, bool is_manual);
			void getPreview(Component cmp, string& path);
			void setPreview(Component cmp, const string& path);
			void destroy();
			Component createAnimable(const Entity& entity);
			void playAnimation(const Component& cmp, const char* path);
			void setAnimationTime(const Component& cmp, float time);
			Animation* loadAnimation(const char* path);

		private:
			struct AnimationSystemImpl* m_impl;
	};


}// ~ namespace Lumix 
