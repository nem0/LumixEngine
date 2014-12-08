#pragma once

#include "../property_view.h"
#include "editor/property_descriptor.h"
#include "file_edit.h"
#include "universe/component.h"
#include <qcheckbox.h>
#include <QColorDialog>
#include <qdesktopservices.h>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <qfiledialog.h>
#include <qmenu.h>
#include <qmessagebox.h>
#include <qmimedata.h>
#include <qlineedit.h>
#include <qpushbutton.h>
#include <qtextstream.h>


class ComponentArrayItemObject : public PropertyViewObject
{
public:
	ComponentArrayItemObject(PropertyViewObject* parent, const char* name, Lumix::IArrayDescriptor& descriptor, Lumix::Component component, int index)
		: PropertyViewObject(parent, name)
		, m_descriptor(descriptor)
		, m_component(component)
		, m_index(index)
	{
	}

	virtual void createEditor(PropertyView& view, QTreeWidgetItem* item) override
	{
		QWidget* widget = new QWidget();
		QHBoxLayout* layout = new QHBoxLayout(widget);
		layout->setContentsMargins(0, 0, 0, 0);
		QPushButton* button = new QPushButton(" - ");
		layout->addWidget(button);
		layout->addStretch(1);
		item->treeWidget()->setItemWidget(item, 1, widget);
		button->connect(button, &QPushButton::clicked, [this, &view]()
		{
			view.getWorldEditor()->removeArrayPropertyItem(m_component, m_index, m_descriptor);
			view.refresh();
		});
	}

	virtual bool isEditable() const override
	{
		return false;
	}

private:
	Lumix::IArrayDescriptor& m_descriptor;
	Lumix::Component m_component;
	int m_index;
};


class ComponentPropertyObject : public PropertyViewObject
{
public:
	ComponentPropertyObject(PropertyViewObject* parent, const char* name, Lumix::Component cmp, Lumix::IPropertyDescriptor& descriptor)
		: PropertyViewObject(parent, name)
		, m_descriptor(descriptor)
		, m_component(cmp)
	{
		m_array_index = -1;
		if (descriptor.getType() == Lumix::IPropertyDescriptor::ARRAY)
		{
			Lumix::IArrayDescriptor& array_desc = static_cast<Lumix::IArrayDescriptor&>(descriptor);
			int item_count = array_desc.getCount(cmp);
			for (int j = 0; j < item_count; ++j)
			{
				ComponentArrayItemObject* item = new ComponentArrayItemObject(this, name, array_desc, m_component, j);
				addMember(item);
				for (int i = 0; i < descriptor.getChildren().size(); ++i)
				{
					auto child = descriptor.getChildren()[i];
					auto member = new ComponentPropertyObject(this, child->getName(), cmp, *descriptor.getChildren()[i]);
					member->setArrayIndex(j);
					item->addMember(member);
				}
			}
		}
	}


	virtual void createEditor(PropertyView& view, QTreeWidgetItem* item) override
	{
		Lumix::Blob stream(view.getWorldEditor()->getAllocator());
		if (m_descriptor.getType() != Lumix::IPropertyDescriptor::ARRAY)
		{
			if (m_array_index >= 0)
			{
				m_descriptor.get(m_component, m_array_index, stream);
			}
			else
			{
				m_descriptor.get(m_component, stream);
			}
		}

		switch (m_descriptor.getType())
		{
		case Lumix::IPropertyDescriptor::BOOL:
		{
			bool b;
			stream.read(b);
			QCheckBox* checkbox = new QCheckBox();
			item->treeWidget()->setItemWidget(item, 1, checkbox);
			checkbox->setChecked(b);
			checkbox->connect(checkbox, &QCheckBox::stateChanged, [this, &view](bool new_value)
			{
				view.getWorldEditor()->setProperty(m_component.type, m_array_index, m_descriptor, &new_value, sizeof(new_value));
			});
		}
		break;
		case Lumix::IPropertyDescriptor::VEC3:
		{
			Lumix::Vec3 value;
			stream.read(value);
			item->setText(1, QString("%1; %2; %3").arg(value.x).arg(value.y).arg(value.z));

			QDoubleSpinBox* sb1 = new QDoubleSpinBox();
			sb1->setValue(value.x);
			item->insertChild(0, new QTreeWidgetItem(QStringList() << "x"));
			item->treeWidget()->setItemWidget(item->child(0), 1, sb1);

			QDoubleSpinBox* sb2 = new QDoubleSpinBox();
			sb2->setValue(value.y);
			item->insertChild(1, new QTreeWidgetItem(QStringList() << "y"));
			item->treeWidget()->setItemWidget(item->child(1), 1, sb2);

			QDoubleSpinBox* sb3 = new QDoubleSpinBox();
			sb3->setValue(value.y);
			item->insertChild(2, new QTreeWidgetItem(QStringList() << "z"));
			item->treeWidget()->setItemWidget(item->child(2), 1, sb3);

			sb1->connect(sb1, (void (QDoubleSpinBox::*)(double))&QDoubleSpinBox::valueChanged, [&view, this, sb1, sb2, sb3](double)
			{
				Lumix::Vec3 value;
				value.set((float)sb1->value(), (float)sb2->value(), (float)sb3->value());
				view.getWorldEditor()->setProperty(m_component.type, m_array_index, m_descriptor, &value, sizeof(value));
			});
		}
		break;
		case Lumix::IPropertyDescriptor::RESOURCE:
		case Lumix::IPropertyDescriptor::FILE:
		{
			char path[LUMIX_MAX_PATH];
			stream.read(path, stream.getBufferSize());
			QWidget* widget = new QWidget();
			FileEdit* edit = new FileEdit(widget, NULL);
			edit->setText(path);
			edit->setServer(view.getWorldEditor());
			QHBoxLayout* layout = new QHBoxLayout(widget);
			layout->addWidget(edit);
			layout->setContentsMargins(0, 0, 0, 0);
			QPushButton* button = new QPushButton("...", widget);
			layout->addWidget(button);
			button->connect(button, &QPushButton::clicked, [this, edit, &view]()
			{
				QString str = QFileDialog::getOpenFileName(NULL, QString(), QString(), dynamic_cast<Lumix::IFilePropertyDescriptor&>(m_descriptor).getFileType());
				if (str != "")
				{
					char rel_path[LUMIX_MAX_PATH];
					QByteArray byte_array = str.toLatin1();
					const char* text = byte_array.data();
					view.getWorldEditor()->getRelativePath(rel_path, LUMIX_MAX_PATH, Lumix::Path(text));
					view.getWorldEditor()->setProperty(m_component.type, m_array_index, m_descriptor, rel_path, strlen(rel_path) + 1);
					edit->setText(rel_path);
				}
			});

			QPushButton* button2 = new QPushButton("->", widget);
			layout->addWidget(button2);
			if (m_descriptor.getType() == Lumix::IPropertyDescriptor::RESOURCE)
			{
				button2->connect(button2, &QPushButton::clicked, [&view, edit]()
				{
					view.setSelectedResourceFilename(edit->text().toLatin1().data());
				});
			}
			else
			{
				button2->connect(button2, &QPushButton::clicked, [edit]()
				{
					QDesktopServices::openUrl(QUrl::fromLocalFile(edit->text()));
				});
			}

			item->treeWidget()->setItemWidget(item, 1, widget);
			connect(edit, &QLineEdit::editingFinished, [edit, &view, this]()
			{
				if (view.getObject())
				{
					QByteArray byte_array = edit->text().toLatin1();
					view.getWorldEditor()->setProperty(m_component.type, m_array_index, m_descriptor, byte_array.data(), byte_array.size() + 1);
				}
			});
		}
		break;
		case Lumix::IPropertyDescriptor::INTEGER:
		{
			auto& int_property = static_cast<Lumix::IIntPropertyDescriptor&>(m_descriptor);
			int value;
			stream.read(value);
			QSpinBox* edit = new QSpinBox();
			edit->setValue(value);
			item->treeWidget()->setItemWidget(item, 1, edit);
			edit->setMinimum(int_property.getMin());
			edit->setMaximum(int_property.getMax());
			connect(edit, (void (QSpinBox::*)(int))&QSpinBox::valueChanged, [this, &view](int new_value)
			{
				int value = new_value;
				view.getWorldEditor()->setProperty(m_component.type, m_array_index, m_descriptor, &value, sizeof(value));
			});
		}
		break;
		case Lumix::IPropertyDescriptor::DECIMAL:
		{
			float value;
			stream.read(value);
			QDoubleSpinBox* edit = new QDoubleSpinBox();
			edit->setMaximum(FLT_MAX);
			edit->setDecimals(4);
			edit->setSingleStep(0.001);
			edit->setValue(value);
			item->treeWidget()->setItemWidget(item, 1, edit);
			connect(edit, (void (QDoubleSpinBox::*)(double))&QDoubleSpinBox::valueChanged, [this, &view](double new_value)
			{
				float value = (float)new_value;
				view.getWorldEditor()->setProperty(m_component.type, m_array_index, m_descriptor, &value, sizeof(value));
			});
		}
		break;
		case Lumix::IPropertyDescriptor::STRING:
		{
			QLineEdit* edit = new QLineEdit();
			item->treeWidget()->setItemWidget(item, 1, edit);
			edit->setText((const char*)stream.getBuffer());
			connect(edit, &QLineEdit::editingFinished, [edit, this, &view]()
			{
				QByteArray byte_array = edit->text().toLatin1();
				const char* text = byte_array.data();
				view.getWorldEditor()->setProperty(m_component.type, m_array_index, m_descriptor, text, strlen(text) + 1);
			});
		}
		break;
		case Lumix::IPropertyDescriptor::ARRAY:
		{
			QWidget* widget = new QWidget();
			QHBoxLayout* layout = new QHBoxLayout(widget);
			layout->setContentsMargins(0, 0, 0, 0);
			QPushButton* button = new QPushButton(" + ");
			layout->addWidget(button);
			layout->addStretch(1);
			item->treeWidget()->setItemWidget(item, 1, widget);
			button->connect(button, &QPushButton::clicked, [this, &view]()
			{
				view.getWorldEditor()->addArrayPropertyItem(m_component, static_cast<Lumix::IArrayDescriptor&>(m_descriptor));
				view.refresh();
			});
		}
		break;
		case Lumix::IPropertyDescriptor::COLOR:
		{
			Lumix::Vec4 value;
			stream.read(value);
			QWidget* widget = new QWidget();
			QHBoxLayout* layout = new QHBoxLayout(widget);
			QColor color((int)(value.x * 255), (int)(value.y * 255), (int)(value.z * 255));
			QLabel* label = new QLabel(color.name());
			layout->setContentsMargins(0, 0, 0, 0);
			layout->addWidget(label);
			label->setStyleSheet(QString("QLabel { background-color : %1; }").arg(color.name()));
			QPushButton* button = new QPushButton("...");
			layout->addWidget(button);
			item->treeWidget()->setItemWidget(item, 1, widget);
			connect(button, &QPushButton::clicked, [this, &view, label, value]()
			{
				QColorDialog* dialog = new QColorDialog(QColor::fromRgbF(value.x, value.y, value.z, value.w));
				dialog->setModal(false);
				dialog->connect(dialog, &QColorDialog::currentColorChanged, [this, &view, dialog]()
				{
					QColor color = dialog->currentColor();
					Lumix::Vec4 value;
					value.x = color.redF();
					value.y = color.greenF();
					value.z = color.blueF();
					value.w = color.alphaF();
					view.getWorldEditor()->setProperty(m_component.type, m_array_index, m_descriptor, &value, sizeof(value));
				});
				dialog->connect(dialog, &QDialog::finished, [&view]()
				{
					view.refresh();
				});
				dialog->show();
			});
		}
		break;
		default:
			ASSERT(false);
			break;
		}
	}


	virtual bool isEditable() const override
	{
		return false;
	}


	Lumix::Component getComponent() const { return m_component; }
	void setArrayIndex(int index) { m_array_index = index; }

private:
	Lumix::IPropertyDescriptor& m_descriptor;
	Lumix::Component m_component;
	int m_array_index;
};

