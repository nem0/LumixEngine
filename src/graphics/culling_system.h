#pragma once

//// remove
#include "core/array.h"
#include "core/sphere.h"
//// remove

namespace Lux
{
	struct Frustum;

	class LUX_ENGINE_API CullingSystem
	{
	public:
		CullingSystem() : m_impl(NULL) { }
		~CullingSystem();

		void create();
		void destroy();

		const Array<bool>& getResult();

		void cullToFrustum(const Frustum& frustum);

		//// test
		void insert(const Array<Sphere>& spheres);
		//// test

	private:
		struct CullingSystemImpl* m_impl;
	};
} // ~namespace Lux