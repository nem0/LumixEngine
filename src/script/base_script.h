#pragma once
#include "core/lumix.h"
#include "universe/universe.h"



namespace Lumix
{


class ScriptSystem;
class ScriptVisitor;


class BaseScript
{
	public:
		virtual ~BaseScript() {}

		virtual void create(ScriptSystem& ctx, Entity e) = 0;
		virtual void update(float) {}
		virtual void visit(ScriptVisitor&) {}
};


} // ~namespace Lumix
