#include "measure_tool.h"
#include "renderer/ray_cast_model_hit.h"
#include "renderer/render_scene.h"


namespace Lumix
{
	MeasureTool::MeasureTool()
		: m_is_enabled(false)
	{ }


	bool MeasureTool::onEntityMouseDown(const RayCastModelHit& hit, int, int)
	{
		if (m_is_enabled)
		{
			if (!m_is_from_set)
			{
				m_from = hit.m_origin + hit.m_dir * hit.m_t;
				m_is_from_set = true;
			}
			else
			{
				m_is_from_set = false;
				m_to = hit.m_origin + hit.m_dir * hit.m_t;
			}
			if(m_distance_measured.isValid()) m_distance_measured.invoke(getDistance());
			return true;
		}
		return false;
	}


	void MeasureTool::createEditorLines(class RenderScene& scene)
	{
		if (m_is_enabled)
		{
			Vec3 color(0, 1, 0);
			scene.addDebugCross(m_from, 0.3f, color, 0);
			scene.addDebugCross(m_to, 0.3f, color, 0);
			scene.addDebugLine(m_from, m_to, color, 0);
		}
	}
}