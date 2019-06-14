#include "measure_tool.h"
#include "editor/render_interface.h"


namespace Lumix
{
	MeasureTool::MeasureTool()
		: m_is_enabled(false)
		, m_is_from_set(false)
		, m_from(0, 0, 0)
		, m_to(0, 0, 0)
	{ }


	bool MeasureTool::onMouseDown(const WorldEditor::RayHit& hit, int, int)
	{
		if (!m_is_enabled) return false;
		if (!hit.is_hit) return false;

		if (!m_is_from_set)
		{
			m_from = hit.pos;
			m_is_from_set = true;
		}
		else
		{
			m_is_from_set = false;
			m_to = hit.pos;
		}
		if(m_distance_measured.isValid()) m_distance_measured.invoke(getDistance());
		return true;
	}


	void MeasureTool::createEditorLines(RenderInterface& interface) const
	{
		if (!m_is_enabled) return;
		
		static const u32 COLOR = 0x00ff00ff;
		interface.addDebugCross(m_from, 0.3f, COLOR);
		interface.addDebugCross(m_to, 0.3f, COLOR);
		interface.addDebugLine(m_from, m_to, COLOR);
	}
}