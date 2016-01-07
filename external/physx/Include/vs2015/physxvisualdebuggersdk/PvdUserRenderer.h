/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */
#ifndef PVD_IMMEDIATE_RENDERER_H
#define PVD_IMMEDIATE_RENDERER_H
#include "physxvisualdebuggersdk/PvdObjectModelBaseTypes.h"
#include "physxprofilesdk/PxProfileEventBufferClientManager.h"
#include "foundation/PxVec3.h"
#include "foundation/PxTransform.h"

namespace physx { namespace debugger { namespace renderer {
	
	struct PvdPoint
	{
		PxVec3		pos;
		PvdColor	color;
		PvdPoint(const PxVec3& p, const PvdColor& c)
			: pos(p), color(c) {}
		PvdPoint(){}
	};
	
	struct PvdLine
	{
		PvdLine(const PxVec3& p0, const PxVec3& p1, const PvdColor& c)
			: pos0(p0), color0(c), pos1(p1), color1(c) {}
		PvdLine(){}

		PxVec3		pos0;
		PvdColor	color0;
		PxVec3		pos1;
		PvdColor	color1;
	};

	
	struct PvdTriangle
	{
		PvdTriangle(const PxVec3& p0, const PxVec3& p1, const PxVec3& p2, const PvdColor& c)
			: pos0(p0), color0(c), pos1(p1), color1(c), pos2(p2), color2(c) {}
		PvdTriangle(){}
		PxVec3		pos0;
		PvdColor	color0;
		PxVec3		pos1;
		PvdColor	color1;
		PxVec3		pos2;
		PvdColor	color2;
	};

	struct PvdTransform
	{
		PxTransform transform;
		PvdColor xAxisColor;
		PvdColor yAxisColor;
		PvdColor zAxisColor;
		PvdTransform( const PxTransform& _transform, PxU32 x, PxU32 y, PxU32 z )
			: transform( _transform )
			, xAxisColor( x )
			, yAxisColor( y )
			, zAxisColor( z )
		{
		}
	};

	class PvdUserRenderer : public PxProfileEventBufferClientManager
	{
	protected:
		virtual ~PvdUserRenderer(){}
	public:
		virtual void addRef() = 0;
		virtual void release() = 0;

		//Instance to associate the further rendering with.
		virtual void setInstanceId( const void* instanceId ) = 0;
		//Draw these points associated with this instance
		virtual void drawPoints( const PvdPoint* points, PxU32 count ) = 0;
		//Draw these lines associated with this instance
		virtual void drawLines( const PvdLine* lines, PxU32 count ) = 0;
		//Draw these triangles associated with this instance
		virtual void drawTriangles( const PvdTriangle* triangles, PxU32 count ) = 0;
		//Draw this text associated with this instance
		virtual void drawText( PxVec3 pos, PvdColor color, const char* text, ...) = 0;

		//Constraint visualization routines
		virtual void visualizeJointFrames( const PxTransform& parent, const PxTransform& child ) = 0;
		virtual void visualizeLinearLimit( const PxTransform& t0, const PxTransform& t1, PxF32 value, bool active ) = 0;
		virtual void visualizeAngularLimit( const PxTransform& t0, PxF32 lower, PxF32 upper, bool active) = 0;
		virtual void visualizeLimitCone( const PxTransform& t, PxF32 ySwing, PxF32 zSwing, bool active) = 0;
		virtual void visualizeDoubleCone( const PxTransform& t, PxF32 angle, bool active) = 0;

		//Clear the immedate buffer.
		virtual void flushRenderEvents() = 0;

		static PvdUserRenderer& create( PxAllocatorCallback& alloc, PxU32 bufferSize = 0x2000 );
	};

}}}

#endif
