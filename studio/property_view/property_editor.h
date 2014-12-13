#pragma once


#include <functional>
#include <qcheckbox.h>
#include <qdesktopservices.h>
#include <qfiledialog.h>


void createResourceSelector(PropertyView& view, QTreeWidgetItem* item, Lumix::Resource* resource, std::function<void(const Lumix::Path&)> setter)
{
	QWidget* widget = new QWidget();
	QHBoxLayout* layout = new QHBoxLayout(widget);
	FileEdit* edit = new FileEdit(NULL, &view);
	edit->setText(resource->getPath().c_str());
	layout->addWidget(edit);
	layout->setContentsMargins(0, 0, 0, 0);
	item->treeWidget()->setItemWidget(item, 1, widget);
	edit->connect(edit, &FileEdit::editingFinished, [setter, resource, edit]()
	{
		setter(Lumix::Path(edit->text().toLatin1().data()));
	});
	QPushButton* button = new QPushButton("...");
	layout->addWidget(button);
	button->connect(button, &QPushButton::clicked, [setter, &view, resource, edit]()
	{
		QString str = QFileDialog::getOpenFileName(NULL, QString(), QString(), "All files (*.*)");
		if (str != "")
		{
			char rel_path[LUMIX_MAX_PATH];
			QByteArray byte_array = str.toLatin1();
			const char* text = byte_array.data();
			view.getWorldEditor()->getRelativePath(rel_path, LUMIX_MAX_PATH, Lumix::Path(text));

			setter(Lumix::Path(rel_path));

			edit->setText(rel_path);
		}
	});

	QPushButton* go_button = new QPushButton("->");
	layout->addWidget(go_button);
	go_button->connect(go_button, &QPushButton::clicked, [edit]()
	{
		QDesktopServices::openUrl(QUrl::fromLocalFile(edit->text()));
	});
}


QTreeWidgetItem* newSubItem(QTreeWidgetItem* parent)
{
	QTreeWidgetItem* subitem = new QTreeWidgetItem();
	parent->addChild(subitem);
	return subitem;
}


template <typename T>
class PropertyEditor
{
public:
	template <typename Setter, typename SelectorCreator, typename EditorCreator>
	static PropertyEditor<T> create(PropertyView& view, const char* name, QTreeWidgetItem* item, T object, Setter setter, SelectorCreator selector_creator, EditorCreator editor_creator)
	{
		QTreeWidgetItem* subitem = new QTreeWidgetItem();
		item->addChild(subitem);
		subitem->setText(0, name);

		selector_creator(view, subitem, object, setter);
		editor_creator(view, subitem, TypedObject(object, 0), name);

		return PropertyEditor<T>();
	}

	template <typename Setter, typename SelectorCreator>
	static PropertyEditor<T> create(PropertyView& view, const char* name, QTreeWidgetItem* item, T object, Setter setter, SelectorCreator selector_creator)
	{
		QTreeWidgetItem* subitem = new QTreeWidgetItem();
		item->addChild(subitem);
		subitem->setText(0, name);

		selector_creator(view, subitem, object, setter);

		return PropertyEditor<T>();
	}
};


template <>
class PropertyEditor<bool>
{
public:
	template <typename Setter>
	static PropertyEditor<bool> create(const char* name, QTreeWidgetItem* item, bool value, Setter setter)
	{
		QTreeWidgetItem* subitem = new QTreeWidgetItem();
		item->addChild(subitem);
		subitem->setText(0, name);

		QCheckBox* checkbox = new QCheckBox();
		checkbox->setChecked(value);
		subitem->treeWidget()->setItemWidget(subitem, 1, checkbox);

		checkbox->connect(checkbox, &QCheckBox::stateChanged, [setter](bool new_state){
			setter(new_state);
		});
		return PropertyEditor<bool>(*checkbox);
	}

private:
	PropertyEditor(QCheckBox& edit)
		: m_edit(edit)
	{}

private:
	QCheckBox& m_edit;
};


template <>
class PropertyEditor<Lumix::Vec3>
{
public:
	template <typename Setter>
	static PropertyEditor<Lumix::Vec3> create(const char* name, QTreeWidgetItem* item, Lumix::Vec3 value, Setter setter)
	{
		QTreeWidgetItem* subitem = new QTreeWidgetItem();
		item->addChild(subitem);
		subitem->setText(0, name);

		subitem->setText(1, QString("%1; %2; %3").arg(value.x).arg(value.y).arg(value.z));

		QDoubleSpinBox* sb1 = new QDoubleSpinBox();
		sb1->setValue(value.x);
		subitem->insertChild(0, new QTreeWidgetItem(QStringList() << "x"));
		subitem->treeWidget()->setItemWidget(subitem->child(0), 1, sb1);

		QDoubleSpinBox* sb2 = new QDoubleSpinBox();
		sb2->setValue(value.y);
		subitem->insertChild(1, new QTreeWidgetItem(QStringList() << "y"));
		subitem->treeWidget()->setItemWidget(subitem->child(1), 1, sb2);

		QDoubleSpinBox* sb3 = new QDoubleSpinBox();
		sb3->setValue(value.z);
		subitem->insertChild(2, new QTreeWidgetItem(QStringList() << "z"));
		subitem->treeWidget()->setItemWidget(subitem->child(2), 1, sb3);

		sb1->connect(sb1, (void (QDoubleSpinBox::*)(double))&QDoubleSpinBox::valueChanged, [setter, sb1, sb2, sb3](double)
		{
			Lumix::Vec3 value;
			value.set((float)sb1->value(), (float)sb2->value(), (float)sb3->value());
			setter(value);
		});

		sb2->connect(sb2, (void (QDoubleSpinBox::*)(double))&QDoubleSpinBox::valueChanged, [setter, sb1, sb2, sb3](double)
		{
			Lumix::Vec3 value;
			value.set((float)sb1->value(), (float)sb2->value(), (float)sb3->value());
			setter(value);
		});

		sb3->connect(sb3, (void (QDoubleSpinBox::*)(double))&QDoubleSpinBox::valueChanged, [setter, sb1, sb2, sb3](double)
		{
			Lumix::Vec3 value;
			value.set((float)sb1->value(), (float)sb2->value(), (float)sb3->value());
			setter(value);
		});

		return PropertyEditor<Lumix::Vec3>();
	}

private:
};


template <>
class PropertyEditor<Lumix::Vec4>
{
public:
	template <typename Setter>
	static PropertyEditor<Lumix::Vec4> create(const char* name, QTreeWidgetItem* item, Lumix::Vec4 value, Setter setter)
	{
		QTreeWidgetItem* subitem = new QTreeWidgetItem();
		item->addChild(subitem);
		subitem->setText(0, name);

		QWidget* widget = new QWidget();
		QHBoxLayout* layout = new QHBoxLayout(widget);
		QColor color((int)(value.x * 255), (int)(value.y * 255), (int)(value.z * 255));
		QLabel* label = new QLabel(color.name());
		layout->setContentsMargins(0, 0, 0, 0);
		layout->addWidget(label);
		label->setStyleSheet(QString("QLabel { background-color : %1; }").arg(color.name()));
		QPushButton* button = new QPushButton("...");
		layout->addWidget(button);
		subitem->treeWidget()->setItemWidget(subitem, 1, widget);
		button->connect(button, &QPushButton::clicked, [setter, label, value]()
		{
			QColorDialog* dialog = new QColorDialog(QColor::fromRgbF(value.x, value.y, value.z, value.w));
			dialog->setModal(false);
			dialog->connect(dialog, &QColorDialog::currentColorChanged, [setter, label, dialog]()
			{
				QColor color = dialog->currentColor();
				Lumix::Vec4 value;
				value.x = color.redF();
				value.y = color.greenF();
				value.z = color.blueF();
				value.w = color.alphaF();
				label->setStyleSheet(QString("QLabel { background-color : %1; }").arg(color.name()));

				setter(value);
			});
			dialog->show();
		});

		return PropertyEditor<Lumix::Vec4>();
	}

private:
};


template <>
class PropertyEditor<int>
{
public:
	template <typename Setter>
	static PropertyEditor<int> create(const char* name, QTreeWidgetItem* item, int value, Setter setter)
	{
		QTreeWidgetItem* subitem = new QTreeWidgetItem();
		item->addChild(subitem);
		subitem->setText(0, name);

		QSpinBox* edit = new QSpinBox();
		edit->setValue(value);
		subitem->treeWidget()->setItemWidget(subitem, 1, edit);
		edit->connect(edit, (void (QSpinBox::*)(int))&QSpinBox::valueChanged, [setter](int new_value)
		{
			setter(new_value);
		});

		return PropertyEditor<int>(edit);
	}

	static PropertyEditor<int> create(const char* name, QTreeWidgetItem* item, int value)
	{
		QTreeWidgetItem* subitem = new QTreeWidgetItem();
		item->addChild(subitem);
		subitem->setText(0, name);
		subitem->setText(1, QString::number(value));

		return PropertyEditor<int>(NULL);
	}

	void setMinimum(int minimum) { m_edit->setMinimum(minimum); }
	void setMaximum(int maximum) { m_edit->setMaximum(maximum); }

private:
	PropertyEditor(QSpinBox* edit)
		: m_edit(edit)
	{}

private:
	QSpinBox* m_edit;
};


template <>
class PropertyEditor<const char*>
{
public:
	template <typename Setter>
	static PropertyEditor<const char*> create(const char* name, QTreeWidgetItem* item, const char* value, Setter setter)
	{
		QTreeWidgetItem* subitem = new QTreeWidgetItem();
		item->addChild(subitem);
		subitem->setText(0, name);

		QLineEdit* edit = new QLineEdit();
		subitem->treeWidget()->setItemWidget(subitem, 1, edit);
		edit->setText(value);
		edit->connect(edit, &QLineEdit::editingFinished, [edit, setter]()
		{
			QByteArray byte_array = edit->text().toLatin1();
			const char* text = byte_array.data();
			setter(text);
		});

		return PropertyEditor<const char*>();
	}
};


template <>
class PropertyEditor<float>
{
public:
	template <typename Setter>
	static PropertyEditor<float> create(const char* name, QTreeWidgetItem* item, float value, Setter setter)
	{
		QTreeWidgetItem* subitem = new QTreeWidgetItem();
		item->addChild(subitem);
		subitem->setText(0, name);

		QDoubleSpinBox* edit = new QDoubleSpinBox();
		edit->setMinimum(-FLT_MAX);
		edit->setMaximum(FLT_MAX);
		edit->setDecimals(4);
		edit->setSingleStep(0.1);
		edit->setValue(value);
		subitem->treeWidget()->setItemWidget(subitem, 1, edit);
		edit->connect(edit, (void (QDoubleSpinBox::*)(double))&QDoubleSpinBox::valueChanged, [setter](double new_value)
		{
			setter((float)new_value);
		});

		return PropertyEditor<float>(edit);
	}

	static PropertyEditor<float> create(const char* name, QTreeWidgetItem* item, float value)
	{
		QTreeWidgetItem* subitem = new QTreeWidgetItem();
		item->addChild(subitem);
		subitem->setText(0, name);
		subitem->setText(1, QString::number(value));

		return PropertyEditor<float>(NULL);
	}

	void setMinimum(float minimum) { m_edit->setMinimum(minimum); }
	void setMaximum(float maximum) { m_edit->setMaximum(maximum); }
	void setStep(float step) { m_edit->setSingleStep(step); }

private:
	PropertyEditor(QDoubleSpinBox* edit)
		: m_edit(edit)
	{}

private:
	QDoubleSpinBox* m_edit;
};


template <typename T>
PropertyEditor<T> makePropertyEditor(PropertyView& view, const char* name, QTreeWidgetItem* item, T value)
{
	return PropertyEditor<T>::create(view, name, item, value);
}


template <typename Getter, typename Namer, typename Counter>
void addArray(
	PropertyView& view
	, const char* name
	, QTreeWidgetItem* item
	, Getter getter
	, Namer namer
	, Counter counter
	)
{
	QTreeWidgetItem* subitem = new QTreeWidgetItem();
	subitem->setText(0, name);
	if (item != NULL)
	{
		item->addChild(subitem);
		subitem->setText(0, name);
	}
	else
	{
		view.m_ui->propertyList->insertTopLevelItem(0, subitem);
	}

	for (int i = 0; i < counter(); ++i)
	{
		makePropertyEditor(view, namer(i), subitem, getter(i));
	}
}


template <typename T, typename T2, typename T3, typename SelectorCreator>
void addArray(
	PropertyView& view
	, const char* name
	, QTreeWidgetItem* item
	, T* object
	, T2(T::*getter)(int) const
	, void (T::*setter)(int, T3)
	, int (T::*counter)() const
	, SelectorCreator selector_creator
	)
{
	QTreeWidgetItem* subitem = new QTreeWidgetItem();
	item->addChild(subitem);
	subitem->setText(0, name);
	subitem->setText(1, QString("%1 items").arg((object->*counter)()));

	for (int i = 0; i < (object->*counter)(); ++i)
	{
		auto subsubitem_name = QString::number(i + 1).toLatin1();

		PropertyEditor<T2>::create(view, subsubitem_name, subitem, (object->*getter)(i), [setter, object, i](T3 v) { (object->*setter)(i, v); }, selector_creator);
	}
}


template <>
class PropertyEditor<Lumix::Resource*>
{
public:
	template <typename Setter>
	static PropertyEditor<Lumix::Resource*> create(PropertyView& view, const char* name, QTreeWidgetItem* item, Lumix::Resource* value, Setter setter)
	{
		QTreeWidgetItem* subitem = new QTreeWidgetItem();
		item->addChild(subitem);
		subitem->setText(0, name);

		QWidget* widget = new QWidget();
		FileEdit* edit = new FileEdit(widget, NULL);
		if (value)
		{
			edit->setText(value->getPath().c_str());
		}
		edit->setServer(view.getWorldEditor());
		QHBoxLayout* layout = new QHBoxLayout(widget);
		layout->addWidget(edit);
		layout->setContentsMargins(0, 0, 0, 0);
		QPushButton* button = new QPushButton("...", widget);
		layout->addWidget(button);
		button->connect(button, &QPushButton::clicked, [&view, setter, edit]()
		{
			QString str = QFileDialog::getOpenFileName(NULL, QString(), QString()/*, dynamic_cast<Lumix::IFilePropertyDescriptor&>(m_descriptor).getFileType()*/); TODO("todo");
			if (str != "")
			{
				char rel_path[LUMIX_MAX_PATH];
				QByteArray byte_array = str.toLatin1();
				const char* text = byte_array.data();
				view.getWorldEditor()->getRelativePath(rel_path, LUMIX_MAX_PATH, Lumix::Path(text));
				setter(rel_path);
				edit->setText(rel_path);
			}
		});

		if (value)
		{
			QPushButton* button2 = new QPushButton("->", widget);
			layout->addWidget(button2);
			button2->connect(button2, &QPushButton::clicked, [value, &view, edit]()
			{
				view.setSelectedResource(value);
			});
		}

		subitem->treeWidget()->setItemWidget(subitem, 1, widget);
		edit->connect(edit, &QLineEdit::editingFinished, [setter, edit]()
		{
			QByteArray byte_array = edit->text().toLatin1();
			setter(byte_array.data());
		});

		return PropertyEditor<Lumix::Resource*>();
	}
};


template <>
class PropertyEditor<Lumix::Texture*>
{
public:
	static PropertyEditor<Lumix::Texture*> create(PropertyView&, const char* name, QTreeWidgetItem* item, Lumix::Texture* texture)
	{
		item->setText(0, name);
		item->setText(1, texture->getPath().c_str());

		PropertyEditor<float>::create("Width", item, texture->getWidth());
		PropertyEditor<float>::create("Height", item, texture->getHeight());

		QTreeWidgetItem* subitem = new QTreeWidgetItem();
		item->addChild(subitem);
		subitem->setText(0, "Preview");
		subitem->setText(1, "Loading...");

		QLabel* image_label = new QLabel();
		subitem->treeWidget()->setItemWidget(subitem, 1, image_label);
		QImage image(texture->getPath().c_str());
		image_label->setPixmap(QPixmap::fromImage(image).scaledToHeight(100));
		image_label->adjustSize();
		TODO("Find out why this does not work anymore!");

		return PropertyEditor<Lumix::Texture*>();
	}

	template <typename Setter, typename SelectorCreator>
	static PropertyEditor<Lumix::Texture*> create(PropertyView& view, const char* name, QTreeWidgetItem* item, Lumix::Texture* texture, Setter setter, SelectorCreator selector_creator)
	{
		auto editor = create(view, name, item, texture);

		selector_creator(view, item, texture, setter);

		return editor;
	}
};


template <>
class PropertyEditor<Lumix::Material*>
{
public:
	static PropertyEditor<Lumix::Material*> create(PropertyView& view, const char* name, QTreeWidgetItem* subitem, Lumix::Material* material)
	{
		subitem->setText(0, name);
		QWidget* widget = new QWidget();
		QHBoxLayout* layout = new QHBoxLayout(widget);
		layout->setContentsMargins(0, 0, 0, 0);
		QLabel* label = new QLabel(material->getPath().c_str());
		layout->addWidget(label);
		QPushButton* button = new QPushButton("Save");
		QPushButton* go_button = new QPushButton("->");
		layout->addWidget(button);
		layout->addWidget(go_button);
		go_button->connect(go_button, &QPushButton::clicked, [material]()
		{
			QDesktopServices::openUrl(QUrl::fromLocalFile(material->getPath().c_str()));
		});

		button->connect(button, &QPushButton::clicked, [material, &view]()
		{
			Lumix::FS::FileSystem& fs = view.getWorldEditor()->getEngine().getFileSystem();
			// use temporary because otherwise the material is reloaded during saving
			char tmp_path[LUMIX_MAX_PATH];
			strcpy(tmp_path, material->getPath().c_str());
			strcat(tmp_path, ".tmp");
			Lumix::FS::IFile* file = fs.open(fs.getDefaultDevice(), tmp_path, Lumix::FS::Mode::CREATE | Lumix::FS::Mode::WRITE);
			if (file)
			{
				Lumix::DefaultAllocator allocator;
				Lumix::JsonSerializer serializer(*file, Lumix::JsonSerializer::AccessMode::WRITE, material->getPath().c_str(), allocator);
				material->save(serializer);
				fs.close(file);

				QFile::remove(material->getPath().c_str());
				QFile::rename(tmp_path, material->getPath().c_str());
			}
			else
			{
				Lumix::g_log_error.log("Material manager") << "Could not save file " << material->getPath().c_str();
			}
		});
		subitem->treeWidget()->setItemWidget(subitem, 1, widget);

		PropertyEditor<bool>::create("Alpha cutout", subitem, material->isAlphaCutout(), [material](bool v) { material->enableAlphaCutout(v); });
		PropertyEditor<bool>::create("Alpha to coverage", subitem, material->isAlphaToCoverage(), [material](bool v) { material->enableAlphaToCoverage(v); });
		PropertyEditor<bool>::create("Backface culling", subitem, material->isBackfaceCulling(), [material](bool v) { material->enableBackfaceCulling(v); });
		PropertyEditor<bool>::create("Shadow receiver", subitem, material->isShadowReceiver(), [material](bool v) { material->enableShadowReceiving(v); });
		PropertyEditor<bool>::create("Z test", subitem, material->isZTest(), [material](bool v) { material->enableZTest(v); });

		PropertyEditor<Lumix::Shader*>::create(view, "Shader", subitem, material->getShader(), [material](const Lumix::Path& path) { material->setShader(path); }, &createResourceSelector);

		addArray(view, "Textures", subitem, material, &Lumix::Material::getTexture, &Lumix::Material::setTexturePath, &Lumix::Material::getTextureCount, &createResourceSelector);

		return PropertyEditor<Lumix::Material*>();
	}
};


template <>
class PropertyEditor<Lumix::Model*>
{
public:
	static PropertyEditor<Lumix::Model*> create(PropertyView& view, QTreeWidgetItem* item, Lumix::Model* model)
	{
		item->setText(0, "model");
		PropertyEditor<int>::create("Bones count", item, model->getBoneCount());
		PropertyEditor<float>::create("Bounding radius", item, model->getBoundingRadius());

		QTreeWidgetItem* meshes_item = new QTreeWidgetItem();
		item->addChild(meshes_item);
		meshes_item->setText(0, "Meshes");
		for (int i = 0; i < model->getMeshCount(); ++i)
		{
			auto& mesh = model->getMesh(i);
			QTreeWidgetItem* mesh_item = new QTreeWidgetItem();
			meshes_item->addChild(mesh_item);
			mesh_item->setText(0, "Mesh");
			mesh_item->setText(1, mesh.getName());

			PropertyEditor<int>::create("Triangles", mesh_item, mesh.getTriangleCount());
			PropertyEditor<Lumix::Material*>::create(view, "material", newSubItem(mesh_item), mesh.getMaterial());
		}

		return PropertyEditor<Lumix::Model*>();
	}
};


