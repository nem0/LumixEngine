#pragma once


#include "engine/delegate_list.h"
#include "engine/vec.h"
#include "world_editor.h"


namespace Lumix
{
	class MeasureTool final : public WorldEditor::Plugin
	{
		public:
			MeasureTool();

			bool onMouseDown(const WorldEditor::RayHit& hit, int x, int y) override;
			void onMouseMove(int, int, int, int) override {}
			void onMouseUp(int, int, OS::MouseButton) override {}
			void enable(bool is_enabled) { m_is_enabled = is_enabled; m_is_from_set = false; }
			bool isEnabled() const { return m_is_enabled; }
			const DVec3& getFrom() const { return m_from; }
			const DVec3& getTo() const { return m_to; }
			double getDistance() const { return (m_from - m_to).length(); }
			void createEditorLines(class RenderInterface& interface) const;
			Delegate<void(double)>& distanceMeasured() { return m_distance_measured; }

		private:
			bool m_is_enabled;
			bool m_is_from_set;
			DVec3 m_from;
			DVec3 m_to;
			Delegate<void(double)> m_distance_measured;
	};
} // namespace Lumix