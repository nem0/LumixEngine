#include "measure_tool.h"
#include "graphics/ray_cast_model_hit.h"
#include "graphics/render_scene.h"


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
			m_distance_measured.invoke(getDistance());
			return true;
		}
		return false;
	}


	void MeasureTool::createEditorLines(class RenderScene& scene)
	{
		if (m_is_enabled)
		{
			scene.addDebugLine(m_from, m_to, Vec3(0, 1, 0), 0);
		}
	}
}