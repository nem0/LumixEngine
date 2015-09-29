#pragma once


#include "lumix.h"
#include "core/string.h"


template <int size>
struct StringBuilder
{
	StringBuilder(const char* str)
	{
		Lumix::copyString(data, size, str);
	}

	template <typename T>
	StringBuilder(const char* str, T value)
	{
		Lumix::copyString(data, size, str);
		add(value);
	}

	template <typename T, typename T2>
	StringBuilder(const char* str, T value, T2 value2)
	{
		Lumix::copyString(data, size, str);
		add(value);
		add(value2);
	}

	template <typename T, typename T2, typename T3>
	StringBuilder(const char* str, T value, T2 value2, T3 value3)
	{
		Lumix::copyString(data, size, str);
		add(value);
		add(value2);
		add(value3);
	}


	template <typename T>
	StringBuilder& operator <<(T value)
	{
		add(value);
		return *this;
	}

	template<int size>
	void add(StringBuilder<size>& value)
	{
		Lumix::catString(data, size, value.data);
	}

	void add(const char* value)
	{
		Lumix::catString(data, size, value);
	}

	void add(char* value)
	{
		Lumix::catString(data, size, value);
	}

	void add(float value)
	{
		int len = (int)strlen(data);
		Lumix::toCString(value, data + len, size - len, 3);
	}

	template <typename T>
	void add(T value)
	{
		int len = (int)strlen(data);
		Lumix::toCString(value, data + len, size - len);
	}


	operator const char*() {
		return data;
	}
	char data[size];
};


class StudioApp;


struct Action
{
	Action(const char* label, const char* name, void (StudioApp::*func)())
	{
		this->label = label;
		this->name = name;
		this->func = func;
		shortcut[0] = shortcut[1] = shortcut[2] = -1;
	}

	Action(const char* label,
		const char* name,
		void (StudioApp::*func)(),
		int shortcut0,
		int shortcut1,
		int shortcut2)
	{
		this->label = label;
		this->name = name;
		this->func = func;
		shortcut[0] = shortcut0;
		shortcut[1] = shortcut1;
		shortcut[2] = shortcut2;
	}

	int shortcut[3];
	const char* name;
	const char* label;
	void (StudioApp::*func)();
};


namespace Lumix
{
	class WorldEditor;
}

bool ColorPicker(const char* label, float col[3]);
const char* getKeyToString(int key);
void getEntityListDisplayName(Lumix::WorldEditor& editor,
	char* buf,
	int max_size,
	Lumix::Entity entity);
