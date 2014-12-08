#pragma once


#include "../property_view.h"


template <class T, bool own_value> class InstanceObject;


template <class T>
class InstanceObject<T, false> : public PropertyViewObject
{
public:
	typedef void(*CreateEditor)(PropertyView&, QTreeWidgetItem*, InstanceObject<T, false>*);

	InstanceObject(PropertyViewObject* parent, const char* name, T* object, CreateEditor create_editor)
		: PropertyViewObject(parent, name)
	{
		m_value = object;
		m_create_editor = create_editor;
	}


	~InstanceObject()
	{
	}


	void setEditor(CreateEditor create_editor)
	{
		m_create_editor = create_editor;
	}


	virtual void createEditor(PropertyView& view, QTreeWidgetItem* item) override
	{
		if (m_create_editor)
		{
			m_create_editor(view, item, this);
		}
	}

	virtual bool isEditable() const override { return false; }
	T* getValue() const { return m_value; }

private:
	T* m_value;
	CreateEditor m_create_editor;
};


template <class T>
class InstanceObject<T, true> : public PropertyViewObject
{
public:
	typedef void(*CreateEditor)(PropertyView&, QTreeWidgetItem*, InstanceObject<T, true>*);

	InstanceObject(PropertyViewObject* parent, const char* name, T* object, CreateEditor create_editor)
		: PropertyViewObject(parent, name)
	{
		m_value = object;
		m_create_editor = create_editor;
	}


	~InstanceObject()
	{
		delete m_value;
	}


	void setEditor(CreateEditor create_editor)
	{
		m_create_editor = create_editor;
	}


	virtual void createEditor(PropertyView& view, QTreeWidgetItem* item) override
	{
		if (m_create_editor)
		{
			m_create_editor(view, item, this);
		}
	}

	virtual bool isEditable() const override { return false; }
	T* getValue() const { return m_value; }

private:
	T* m_value;
	CreateEditor m_create_editor;
};