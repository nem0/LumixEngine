#pragma once


#include "core/array.h"
#include "core/string.h"


class Groups
{
public:
	void onGUI();

private:
	Lumix::Array<Lumix::string> m_group_names;
	Lumix::Array<Lumix::Array<Lumix::Entity> > m_groups;
};