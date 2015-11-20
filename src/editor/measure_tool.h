#pragma once


#include "world_editor.h"


namespace Lumix
{
	class MeasureTool : public WorldEditor::Plugin
	{
		public:
			MeasureTool();

			void tick() override {}
			virtual bool onEntityMouseDown(const RayCastModelHit& hit, int x, int y) override;
			void onMouseMove(int, int, int, int) override {}
			virtual void onMouseUp(int, int, MouseButton::Value) override {}
			void enable(bool is_enabled) { m_is_enabled = is_enabled; m_is_from_set = false; }
			bool isEnabled() const { return m_is_enabled; }
			const Vec3& getFrom() const { return m_from; }
			const Vec3& getTo() const { return m_to; }
			float getDistance() const { return (m_from - m_to).length(); }
			void createEditorLines(class RenderScene& scene);
			Delegate<void(float)>& distanceMeasured() { return m_distance_measured; }

		private:
			bool m_is_enabled;
			bool m_is_from_set;
			Vec3 m_from;
			Vec3 m_to;
			Delegate<void(float)> m_distance_measured;
	};
} // namespace Lumix