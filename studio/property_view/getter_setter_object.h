#pragma once


#include "../property_view.h"


template <class Value, class Obj>
class GetterSetterObject : public PropertyViewObject
{
public:
	typedef Value(Obj::*Getter)() const;
	typedef void (Obj::*Setter)(Value);
	typedef void(*CreateEditor)(QTreeWidgetItem*, GetterSetterObject&, Value);


	GetterSetterObject(PropertyViewObject* parent, const char* name, Obj* object, Getter getter, Setter setter, CreateEditor create_editor)
		: PropertyViewObject(parent, name)
	{
		m_object = object;
		m_getter = getter;
		m_setter = setter;
		m_create_editor = create_editor;
	}


	virtual void createEditor(PropertyView&, QTreeWidgetItem* item) override
	{
		m_create_editor(item, *this, (m_object->*m_getter)());
	}


	virtual bool isEditable() const override
	{
		return m_setter != NULL;
	}


	void set(Value value)
	{
		(m_object->*m_setter)(value);
	}


private:
	Obj* m_object;
	Getter m_getter;
	Setter m_setter;
	CreateEditor m_create_editor;
};


template<class T>
void createEditor(QTreeWidgetItem* item, GetterSetterObject<int, T>&, int value)
{
	item->setText(1, QString::number(value));
}


template<class T>
void createEditor(QTreeWidgetItem* item, GetterSetterObject<size_t, T>&, size_t value)
{
	item->setText(1, QString::number(value));
}


template<class T>
void createEditor(QTreeWidgetItem* item, GetterSetterObject<float, T>&, float value)
{
	item->setText(1, QString::number(value));
}


template <class T>
void createEditor(QTreeWidgetItem* item, GetterSetterObject<bool, T>& object, bool value)
{
	QCheckBox* checkbox = new QCheckBox();
	item->treeWidget()->setItemWidget(item, 1, checkbox);
	checkbox->setChecked(value);
	if (object.isEditable())
	{
		checkbox->connect(checkbox, &QCheckBox::stateChanged, [&object](bool new_state) {
			object.set(new_state);
		});
	}
	else
	{
		checkbox->setDisabled(true);
	}
}


