#pragma once
#include "core\lux.h"
#include "universe\universe.h"



namespace Lux
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


} // ~namespace Lux
