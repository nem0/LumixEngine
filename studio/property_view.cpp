#include "property_view.h"
#include "ui_property_view.h"
#include "animation/animation_system.h"
#include "assetbrowser.h"
#include "core/aabb.h"
#include "core/crc32.h"
#include "core/FS/file_system.h"
#include "core/json_serializer.h"
#include "core/log.h"
#include "core/path_utils.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "editor/ieditor_command.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "entity_list.h"
#include "entity_template_list.h"
#include "graphics/geometry.h"
#include "graphics/material.h"
#include "graphics/model.h"
#include "graphics/model_instance.h"
#include "graphics/render_scene.h"
#include "graphics/shader.h"
#include "graphics/texture.h"
#include "property_view/component_property_object.h"
#include "property_view/getter_setter_object.h"
#include "property_view/instance_object.h"
#include "property_view/terrain_editor.h"
#include "script/script_system.h"
#include "scripts/scriptcompiler.h"
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
#include <functional>


static const char* component_map[] =
{
	"Animable", "animable",
	"Camera", "camera",
	"Directional light", "light",
	"Mesh", "renderable",
	"Physics Box", "box_rigid_actor",
	"Physics Controller", "physical_controller",
	"Physics Mesh", "mesh_rigid_actor",
	"Physics Heightfield", "physical_heightfield",
	"Script", "script",
	"Terrain", "terrain"
};


static const uint32_t TERRAIN_HASH = crc32("terrain");
static const uint32_t SCRIPT_HASH = crc32("script");


#pragma region new_props


void createComponentPropertyEditor(PropertyView& view, int array_index, Lumix::IPropertyDescriptor* desc, Lumix::Component& cmp, Lumix::Blob& stream, QTreeWidgetItem* property_item);


#pragma region object_basic


void addEdit(const char* name, QTreeWidgetItem* item, int value)
{
	QTreeWidgetItem* subitem = new QTreeWidgetItem();
	item->insertChild(0, subitem);
	subitem->setText(0, name);
	subitem->setText(1, QString::number(value));
}


void addEdit(const char* name, QTreeWidgetItem* item, float value)
{
	QTreeWidgetItem* subitem = new QTreeWidgetItem();
	item->insertChild(0, subitem);
	subitem->setText(0, name);
	subitem->setText(1, QString::number(value));
}


void addEdit(PropertyView& view, const char* name, QTreeWidgetItem* item, Lumix::Component value)
{
	QTreeWidgetItem* subitem = new QTreeWidgetItem();
	item->addChild(subitem);
	subitem->setText(0, name);

	QWidget* widget = new QWidget();
	QHBoxLayout* layout = new QHBoxLayout(widget);
	layout->setContentsMargins(0, 0, 0, 0);
	QPushButton* button = new QPushButton(" - ");
	button->connect(button, &QPushButton::clicked, [subitem, &view, value]()
	{
		subitem->parent()->removeChild(subitem);
		view.getWorldEditor()->destroyComponent(value);
	});
	layout->addStretch(1);
	layout->addWidget(button);
	subitem->treeWidget()->setItemWidget(subitem, 1, widget);


	Lumix::Blob stream(view.getWorldEditor()->getAllocator());
	auto& descriptors = view.getWorldEditor()->getPropertyDescriptors(value.type);
	for (int j = 0; j < descriptors.size(); ++j)
	{
		stream.clearBuffer();
		auto desc = descriptors[j];

		createComponentPropertyEditor(view, -1, desc, value, stream, subitem);
	}
}


template <typename T>
void addEdit(const char* name, QTreeWidgetItem* item, T* object, bool (T::*getter)() const, void (T::*setter)(bool))
{
	QTreeWidgetItem* subitem = new QTreeWidgetItem();
	item->addChild(subitem);
	subitem->setText(0, name);

	QCheckBox* checkbox = new QCheckBox();
	checkbox->setChecked((object->*getter)());
	subitem->treeWidget()->setItemWidget(subitem, 1, checkbox);

	checkbox->connect(checkbox, &QCheckBox::stateChanged, [object, setter](bool new_state){
		(object->*setter)(new_state);
	});
}


template <typename Setter>
void addEdit(const char* name, QTreeWidgetItem* item, bool value, Setter setter)
{
	QTreeWidgetItem* subitem = new QTreeWidgetItem();
	item->addChild(subitem);
	subitem->setText(0, name);

	QCheckBox* checkbox = new QCheckBox();
	checkbox->setChecked(value);
	subitem->treeWidget()->setItemWidget(subitem, 1, checkbox);

	checkbox->connect(checkbox, &QCheckBox::stateChanged, [value, setter](bool new_state){
		setter(new_state);
	});
}


template <typename Setter>
void addResourceEdit(PropertyView& view, const char* name, QTreeWidgetItem* item, Lumix::Resource* value, Setter setter)
{
	QTreeWidgetItem* subitem = new QTreeWidgetItem();
	item->addChild(subitem);
	subitem->setText(0, name);

	QWidget* widget = new QWidget();
	FileEdit* edit = new FileEdit(widget, NULL);
	edit->setText(value->getPath().c_str());
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

	QPushButton* button2 = new QPushButton("->", widget);
	layout->addWidget(button2);
	button2->connect(button2, &QPushButton::clicked, [value, &view, edit]()
	{
		view.setObject(TypedObject(value, 1));
	});

	subitem->treeWidget()->setItemWidget(subitem, 1, widget);
	edit->connect(edit, &QLineEdit::editingFinished, [setter, edit]()
	{
		QByteArray byte_array = edit->text().toLatin1();
		setter(byte_array.data());
	});
}


template <typename Setter>
void addFileEdit(PropertyView& view, const char* name, QTreeWidgetItem* item, const char* value, Setter setter)
{
	QTreeWidgetItem* subitem = new QTreeWidgetItem();
	item->addChild(subitem);
	subitem->setText(0, name);

	QWidget* widget = new QWidget();
	FileEdit* edit = new FileEdit(widget, NULL);
	edit->setText(value);
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

	QPushButton* button2 = new QPushButton("->", widget);
	layout->addWidget(button2);
	/*if (m_descriptor.getType() == Lumix::IPropertyDescriptor::RESOURCE)
	{
		button2->connect(button2, &QPushButton::clicked, [&view, edit]()
		{
			view.setSelectedResourceFilename(edit->text().toLatin1().data());
		});
	}
	else
	{*/
		button2->connect(button2, &QPushButton::clicked, [edit]()
		{
			QDesktopServices::openUrl(QUrl::fromLocalFile(edit->text()));
		});
	//}

	subitem->treeWidget()->setItemWidget(subitem, 1, widget);
	edit->connect(edit, &QLineEdit::editingFinished, [setter, edit]()
	{
		QByteArray byte_array = edit->text().toLatin1();
		setter(byte_array.data());
	});
}



template <typename Setter>
void addEdit(const char* name, QTreeWidgetItem* item, Lumix::Vec3 value, Setter setter)
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
}


template <typename Setter>
void addEdit(const char* name, QTreeWidgetItem* item, Lumix::Vec4 value, Setter setter)
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
}


template <typename Setter>
void addEdit(const char* name, QTreeWidgetItem* item, int value, Setter setter)
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

}


template <typename Setter>
void addEdit(const char* name, QTreeWidgetItem* item, const char* value, Setter setter)
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
}


template <typename Setter>
void addEdit(const char* name, QTreeWidgetItem* item, float value, Setter setter)
{
	QTreeWidgetItem* subitem = new QTreeWidgetItem();
	item->addChild(subitem);
	subitem->setText(0, name);

	QDoubleSpinBox* edit = new QDoubleSpinBox();
	edit->setMaximum(FLT_MAX);
	edit->setDecimals(4);
	edit->setSingleStep(0.001);
	edit->setValue(value);
	subitem->treeWidget()->setItemWidget(subitem, 1, edit);
	edit->connect(edit, (void (QDoubleSpinBox::*)(double))&QDoubleSpinBox::valueChanged, [setter](double new_value)
	{
		setter((float)new_value);
	});
}


template <typename T, typename T2>
void addEdit(PropertyView& view, const char* name, QTreeWidgetItem* item, T* object, T2* (T::*getter)() const, void(*editor_creator)(PropertyView&, QTreeWidgetItem*, TypedObject, const char*))
{
	QTreeWidgetItem* subitem = new QTreeWidgetItem();
	item->addChild(subitem);
	subitem->setText(0, name);
	
	editor_creator(view, subitem, TypedObject(object, 0), name);
}


template <typename Object, typename Setter, typename SelectorCreator>
void addEdit(PropertyView& view, const char* name, QTreeWidgetItem* item, Object object, Setter setter, SelectorCreator selector_creator, void(*editor_creator)(PropertyView&, QTreeWidgetItem*, TypedObject, const char*))
{
	QTreeWidgetItem* subitem = new QTreeWidgetItem();
	item->addChild(subitem);
	subitem->setText(0, name);

	selector_creator(view, subitem, object, setter);
	if (editor_creator)
	{
		editor_creator(view, subitem, TypedObject(object, 0), name);
	}
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
		addEdit(view, namer(i), subitem, getter(i));
	}
}

template <typename T, typename T2, typename T3, typename SelectorCreator>
void addArray(
	PropertyView& view
	, const char* name
	, QTreeWidgetItem* item
	, T* object
	, T2 (T::*getter)(int) const
	, void (T::*setter)(int, T3)
	, int (T::*counter)() const
	, SelectorCreator selector_creator
	, void(*editor_creator)(PropertyView&, QTreeWidgetItem*, TypedObject, const char*)
)
{
	QTreeWidgetItem* subitem = new QTreeWidgetItem();
	item->addChild(subitem);
	subitem->setText(0, name);
	subitem->setText(1, QString("%1 items").arg((object->*counter)()));

	for (int i = 0; i < (object->*counter)(); ++i)
	{
		auto subsubitem_name = QString::number(i + 1).toLatin1();

		addEdit(view, subsubitem_name, subitem, (object->*getter)(i), [setter, object, i](T3 v) { (object->*setter)(i, v); }, selector_creator, editor_creator);
	}
}


#pragma endregion


#pragma region resources


void createTextureEditor(PropertyView& view, QTreeWidgetItem* item, TypedObject object, const char* name)
{
	auto* texture = static_cast<Lumix::Texture*>(object.m_object);
	item->setText(0, name);
	item->setText(1, texture->getPath().c_str());

	addEdit("Width", item, texture->getWidth());
	addEdit("Height", item, texture->getHeight());
	
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
}


void createResourceSelector(PropertyView& view, QTreeWidgetItem* item, Lumix::Resource* resource, std::function<void (const Lumix::Path&)> setter)
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


void createMaterialEditor(PropertyView& view, QTreeWidgetItem* subitem, TypedObject object, const char* name)
{
	subitem->setText(0, name);
	Lumix::Material* material = (Lumix::Material*)object.m_object;
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

	addEdit("Alpha cutout", subitem, material, &Lumix::Material::isAlphaCutout, &Lumix::Material::enableAlphaCutout);
	addEdit("Alpha to coverage", subitem, material, &Lumix::Material::isAlphaToCoverage, &Lumix::Material::enableAlphaToCoverage);
	addEdit("Backface culling", subitem, material, &Lumix::Material::isBackfaceCulling, &Lumix::Material::enableBackfaceCulling);
	addEdit("Shadow receiver", subitem, material, &Lumix::Material::isShadowReceiver, &Lumix::Material::enableShadowReceiving);
	addEdit("Z test", subitem, material, &Lumix::Material::isZTest, &Lumix::Material::enableZTest);

	addEdit(view, "Shader", subitem, material->getShader(), [material](const Lumix::Path& path) { material->setShader(path); }, &createResourceSelector, NULL);

	addArray(view, "Textures", subitem, material, &Lumix::Material::getTexture, &Lumix::Material::setTexturePath, &Lumix::Material::getTextureCount, &createResourceSelector, &createTextureEditor);
};


QTreeWidgetItem* newSubItem(QTreeWidgetItem* parent)
{
	QTreeWidgetItem* subitem = new QTreeWidgetItem();
	parent->addChild(subitem);
	return subitem;
}


void createModelEditor(PropertyView& view, QTreeWidgetItem* item, TypedObject object)
{
	item->setText(0, "model");
	auto* model = static_cast<Lumix::Model*>(object.m_object);
	addEdit("Bones count", item, model->getBoneCount());
	addEdit("Bounding radius", item, model->getBoundingRadius());

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

		addEdit("Triangles", mesh_item, mesh.getTriangleCount());
		createMaterialEditor(view, newSubItem(mesh_item), TypedObject(mesh.getMaterial(), 1), "Material");
	}
}


#pragma endregion


#pragma region entity


void addComponentArrayPropertyEdit(PropertyView& view, Lumix::IPropertyDescriptor* desc, Lumix::Component& cmp, Lumix::Blob& stream, QTreeWidgetItem* property_item)
{
	QTreeWidgetItem* array_item = new QTreeWidgetItem();
	property_item->addChild(array_item);

	QWidget* widget = new QWidget();
	QHBoxLayout* layout = new QHBoxLayout(widget);
	layout->setContentsMargins(0, 0, 0, 0);
	QPushButton* button = new QPushButton(" + ");
	layout->addWidget(button);
	layout->addStretch(1);
	array_item->setText(0, desc->getName());
	array_item->treeWidget()->setItemWidget(array_item, 1, widget);
	auto& array_desc = static_cast<Lumix::IArrayDescriptor&>(*desc);

	button->connect(button, &QPushButton::clicked, [array_item, cmp, &array_desc, &view]()
	{
		view.getWorldEditor()->addArrayPropertyItem(cmp, array_desc);
		
		QTreeWidgetItem* item = new QTreeWidgetItem();
		array_item->addChild(item);
		item->setText(0, QString::number(array_desc.getCount(cmp) - 1));
		auto& children = array_desc.getChildren();
		Lumix::Blob stream(view.getWorldEditor()->getAllocator());
		for (int i = 0; i < children.size(); ++i)
		{
			createComponentPropertyEditor(view, array_desc.getCount(cmp) - 1, children[i], Lumix::Component(cmp), stream, item);
		}
		item->setExpanded(true);
	});

	auto& children = desc->getChildren();
	for (int j = 0; j < array_desc.getCount(cmp); ++j)
	{
		QTreeWidgetItem* item = new QTreeWidgetItem();
		array_item->addChild(item);
		item->setText(0, QString::number(j));
		QWidget* widget = new QWidget();
		QHBoxLayout* layout = new QHBoxLayout(widget);
		layout->setContentsMargins(0, 0, 0, 0);
		QPushButton* button = new QPushButton(" - ");
		button->connect(button, &QPushButton::clicked, [j, &view, cmp, &array_desc, item]() 
		{
			item->parent()->removeChild(item);
			view.getWorldEditor()->removeArrayPropertyItem(cmp, j, array_desc);
		});
		layout->addStretch(1);
		layout->addWidget(button);
		item->treeWidget()->setItemWidget(item, 1, widget);
		for (int i = 0; i < children.size(); ++i)
		{
			createComponentPropertyEditor(view, j, children[i], cmp, stream, item);
		}
	}
}


template <typename T>
void addComponentPropertyEdit(PropertyView& view, int array_index, Lumix::IPropertyDescriptor* desc, Lumix::Component& cmp, Lumix::Blob& stream, QTreeWidgetItem* item)
{
	T v;
	stream.clearBuffer();
	desc->get(cmp, array_index, stream);
	stream.rewindForRead();
	stream.read(v);
	addEdit(desc->getName(), item, v, [&view, desc, cmp, array_index](T v) { view.getWorldEditor()->setProperty(cmp.type, array_index, *desc, &v, sizeof(v)); });
}


void addComponentFilePropertyEdit(PropertyView& view, int array_index, Lumix::IPropertyDescriptor* desc, Lumix::Component& cmp, Lumix::Blob& stream, QTreeWidgetItem* item)
{
	stream.clearBuffer();
	desc->get(cmp, array_index, stream);
	stream.rewindForRead();
	auto* res = view.getResource((const char*)stream.getBuffer());
	if (res)
	{
		addResourceEdit(view, desc->getName(), item, res, [&view, desc, cmp, array_index](const char* v) { view.getWorldEditor()->setProperty(cmp.type, array_index, *desc, v, strlen(v) + 1); });
	}
}


void addComponentStringPropertyEdit(PropertyView& view, int array_index, Lumix::IPropertyDescriptor* desc, Lumix::Component& cmp, Lumix::Blob& stream, QTreeWidgetItem* item)
{
	stream.clearBuffer();
	desc->get(cmp, array_index, stream);
	stream.rewindForRead();
	addEdit(desc->getName(), item, (const char*)stream.getBuffer(), [&view, desc, cmp, array_index](const char* v) { view.getWorldEditor()->setProperty(cmp.type, array_index, *desc, v, strlen(v) + 1); });
}


void createComponentPropertyEditor(PropertyView& view, int array_index, Lumix::IPropertyDescriptor* desc, Lumix::Component& cmp, Lumix::Blob& stream, QTreeWidgetItem* property_item)
{
	switch (desc->getType())
	{
		case Lumix::IPropertyDescriptor::ARRAY:
			addComponentArrayPropertyEdit(view, desc, cmp, stream, property_item);
			break;
		case Lumix::IPropertyDescriptor::BOOL:
			addComponentPropertyEdit<bool>(view, array_index, desc, cmp, stream, property_item);
			break;
		case Lumix::IPropertyDescriptor::COLOR:
			addComponentPropertyEdit<Lumix::Vec4>(view, array_index, desc, cmp, stream, property_item);
			break;
		case Lumix::IPropertyDescriptor::DECIMAL:
			addComponentPropertyEdit<float>(view, array_index, desc, cmp, stream, property_item);
			break;
		case Lumix::IPropertyDescriptor::FILE:
		case Lumix::IPropertyDescriptor::RESOURCE:
			addComponentFilePropertyEdit(view, array_index, desc, cmp, stream, property_item);
			break;
		case Lumix::IPropertyDescriptor::INTEGER:
			addComponentPropertyEdit<int>(view, array_index, desc, cmp, stream, property_item);
			break;
		case Lumix::IPropertyDescriptor::STRING:
			addComponentStringPropertyEdit(view, array_index, desc, cmp, stream, property_item);
			break;
		case Lumix::IPropertyDescriptor::VEC3:
			addComponentPropertyEdit<Lumix::Vec3>(view, array_index, desc, cmp, stream, property_item);
			break;
		default:
			ASSERT(false);
			break;
	}
}


void addEdit(PropertyView& view, QTreeWidgetItem* item, Lumix::Entity e)
{
	auto& cmps = view.getWorldEditor()->getComponents(e);
	addArray(view, "Entity", item,
		[&](int i)
		{
			return cmps[i];
		}, 
		
		[&cmps](int i) 
		{
			Lumix::Component cmp = cmps[i];
			const char* name = "";
			for (int i = 0; i < sizeof(component_map) / sizeof(component_map[0]); i += 2)
			{
				if (crc32(component_map[i + 1]) == cmp.type)
				{
					return component_map[i];
				}
			}
			return (const char*)NULL;
		}, 
			
		[&cmps]() 
		{ 
			return cmps.size(); 
		});
};


#pragma endregion


#pragma endregion


PropertyViewObject::~PropertyViewObject()
{
	for (int i = 0; i < m_members.size(); ++i)
	{
		delete m_members[i];
	}
}


PropertyView::PropertyView(QWidget* parent) 
	: QDockWidget(parent)
	, m_ui(new Ui::PropertyView)
	, m_terrain_editor(NULL)
	, m_selected_resource(NULL)
	, m_object(NULL)
	, m_selected_entity(Lumix::Entity::INVALID)
{
	m_ui->setupUi(this);

	QStringList component_list;
	for(int j = 0; j < sizeof(component_map) / sizeof(component_map[0]); j += 2)
	{
		component_list << component_map[j];
	}
	m_ui->componentTypeCombo->insertItems(0, component_list);

/*	addResourcePlugin(&createMaterialObject);
	addResourcePlugin(&createModelObject);
	addResourcePlugin(&createTextureObject);*/
}


PropertyView::~PropertyView()
{
	m_world_editor->entitySelected().unbind<PropertyView, &PropertyView::onEntitySelected>(this);
	m_world_editor->universeCreated().unbind<PropertyView, &PropertyView::onUniverseCreated>(this);
	m_world_editor->universeDestroyed().unbind<PropertyView, &PropertyView::onUniverseDestroyed>(this);
	onUniverseCreated();
	delete m_terrain_editor;
	delete m_ui;
}


void PropertyView::onEntityPosition(const Lumix::Entity& e)
{
	if (m_selected_entity == e)
	{
		bool b1 = m_ui->positionX->blockSignals(true);
		bool b2 = m_ui->positionY->blockSignals(true);
		bool b3 = m_ui->positionZ->blockSignals(true);
		
		Lumix::Vec3 pos = e.getPosition();
		m_ui->positionX->setValue(pos.x);
		m_ui->positionY->setValue(pos.y);
		m_ui->positionZ->setValue(pos.z);
		
		m_ui->positionX->blockSignals(b1);
		m_ui->positionY->blockSignals(b2);
		m_ui->positionZ->blockSignals(b3);
	}
}


void PropertyView::refresh()
{
//	setObject(NULL);
	onEntitySelected(getWorldEditor()->getSelectedEntities());
}


Lumix::WorldEditor* PropertyView::getWorldEditor()
{
	return m_world_editor;
}


void PropertyView::setWorldEditor(Lumix::WorldEditor& editor)
{
	m_world_editor = &editor;
	m_terrain_editor = new TerrainEditor(editor, m_entity_template_list, m_entity_list);
	m_world_editor->addPlugin(m_terrain_editor);
	m_world_editor->entitySelected().bind<PropertyView, &PropertyView::onEntitySelected>(this);
	m_world_editor->universeCreated().bind<PropertyView, &PropertyView::onUniverseCreated>(this);
	m_world_editor->universeDestroyed().bind<PropertyView, &PropertyView::onUniverseDestroyed>(this);
	if (m_world_editor->getEngine().getUniverse())
	{
		onUniverseCreated();
	}
}


void PropertyView::onUniverseCreated()
{
	m_world_editor->getEngine().getUniverse()->entityMoved().bind<PropertyView, &PropertyView::onEntityPosition>(this);
}


void PropertyView::onUniverseDestroyed()
{
	m_world_editor->getEngine().getUniverse()->entityMoved().unbind<PropertyView, &PropertyView::onEntityPosition>(this);
}


void PropertyView::setAssetBrowser(AssetBrowser& asset_browser)
{
	m_asset_browser = &asset_browser;
	connect(m_asset_browser, &AssetBrowser::fileSelected, this, &PropertyView::setSelectedResourceFilename);
}


Lumix::Resource* PropertyView::getResource(const char* filename)
{
	char rel_path[LUMIX_MAX_PATH];
	m_world_editor->getRelativePath(rel_path, LUMIX_MAX_PATH, Lumix::Path(filename));
	Lumix::ResourceManagerBase* manager = NULL;
	char extension[10];
	Lumix::PathUtils::getExtension(extension, sizeof(extension), filename);
	if (strcmp(extension, "msh") == 0)
	{
		manager = m_world_editor->getEngine().getResourceManager().get(Lumix::ResourceManager::MODEL);
	}
	else if (strcmp(extension, "mat") == 0)
	{
		manager = m_world_editor->getEngine().getResourceManager().get(Lumix::ResourceManager::MATERIAL);
	}
	else if (strcmp(extension, "dds") == 0 || strcmp(extension, "tga") == 0)
	{
		manager = m_world_editor->getEngine().getResourceManager().get(Lumix::ResourceManager::TEXTURE);
	}

	if (manager != NULL)
	{
		return manager->load(Lumix::Path(rel_path));
	}
	else
	{
		return NULL;
	}
}


void PropertyView::setSelectedResourceFilename(const char* filename)
{
	char rel_path[LUMIX_MAX_PATH];
	m_world_editor->getRelativePath(rel_path, LUMIX_MAX_PATH, Lumix::Path(filename));
	Lumix::ResourceManagerBase* manager = NULL;
	char extension[10];
	Lumix::PathUtils::getExtension(extension, sizeof(extension), filename);
	if (strcmp(extension, "msh") == 0)
	{
		manager = m_world_editor->getEngine().getResourceManager().get(Lumix::ResourceManager::MODEL);
	}
	else if (strcmp(extension, "mat") == 0)
	{
		manager = m_world_editor->getEngine().getResourceManager().get(Lumix::ResourceManager::MATERIAL);
	}
	else if (strcmp(extension, "dds") == 0 || strcmp(extension, "tga") == 0)
	{
		manager = m_world_editor->getEngine().getResourceManager().get(Lumix::ResourceManager::TEXTURE);
	}

	if (manager != NULL)
	{
		setSelectedResource(manager->load(Lumix::Path(rel_path)));
	}
	else
	{
		setSelectedResource(NULL);
	}
}


void PropertyView::addResourcePlugin(PropertyViewObject::Creator plugin)
{
	m_resource_plugins.push_back(plugin);
}


void PropertyView::onSelectedResourceLoaded(Lumix::Resource::State, Lumix::Resource::State new_state)
{
	if (new_state == Lumix::Resource::State::READY)
	{
		setObject(TypedObject(m_selected_resource, 1));
		m_ui->propertyList->expandToDepth(1);
		m_ui->propertyList->resizeColumnToContents(0);
	}
}


void PropertyView::setScriptCompiler(ScriptCompiler* compiler)
{
	m_compiler = compiler;
	if(m_compiler)
	{
		connect(m_compiler, &ScriptCompiler::compiled, this, &PropertyView::on_script_compiled);
	}
}


void PropertyView::clear()
{
	m_ui->propertyList->clear();

	delete m_object;
	m_object = NULL;
}


void PropertyView::setScriptStatus(uint32_t status)
{
	for(int i = 0; i < m_ui->propertyList->topLevelItemCount(); ++i)
	{
		QTreeWidgetItem* top_level_item = m_ui->propertyList->topLevelItem(i);
		for (int k = 0; k < top_level_item->childCount(); ++k)
		{
			QTreeWidgetItem* item = m_ui->propertyList->topLevelItem(i)->child(k);
			if (item->text(0) == "Script")
			{
				for (int j = 0; j < item->childCount(); ++j)
				{
					if (item->child(j)->text(0) == "Status")
					{
						switch (status)
						{
						case ScriptCompiler::SUCCESS:
							item->child(j)->setText(1, "Success");
							break;
						case ScriptCompiler::NOT_COMPILED:
							item->child(j)->setText(1, "Not compiled");
							break;
						case ScriptCompiler::UNKNOWN:
							item->child(j)->setText(1, "Unknown");
							break;
						case ScriptCompiler::FAILURE:
							item->child(j)->setText(1, "Failure");
							break;
						default:
							ASSERT(false);
							break;
						}

						return;
					}
				}
			}
		}
	}
}


void PropertyView::on_script_compiled(const Lumix::Path&, uint32_t status)
{
	setScriptStatus(status == 0 ? ScriptCompiler::SUCCESS : ScriptCompiler::FAILURE);
}


void PropertyView::addScriptCustomProperties(QTreeWidgetItem& tree_item, const Lumix::Component& script_component)
{
	QTreeWidgetItem* tools_item = new QTreeWidgetItem(QStringList() << "Tools");
	tree_item.insertChild(0, tools_item);
	QWidget* widget = new QWidget();
	QHBoxLayout* layout = new QHBoxLayout(widget);
	layout->setContentsMargins(0, 0, 0, 0);
	QPushButton* compile_button = new QPushButton("Compile", widget);
	layout->addWidget(compile_button);
	m_ui->propertyList->setItemWidget(tools_item, 1, widget);
	connect(compile_button, &QPushButton::clicked, this, [this, script_component](){
		Lumix::string path(m_world_editor->getAllocator());
		static_cast<Lumix::ScriptScene*>(script_component.scene)->getScriptPath(script_component, path);
		m_compiler->compile(Lumix::Path(path.c_str()));
	});

	QTreeWidgetItem* status_item = new QTreeWidgetItem(QStringList() << "Status");
	tree_item.insertChild(0, status_item);
	Lumix::string path(m_world_editor->getAllocator());
	static_cast<Lumix::ScriptScene*>(script_component.scene)->getScriptPath(script_component, path);
	switch (m_compiler->getStatus(Lumix::Path(path.c_str())))
	{
		case ScriptCompiler::SUCCESS:
			status_item->setText(1, "Compiled");
			break;
		case ScriptCompiler::FAILURE:
			status_item->setText(1, "Failure");
			break;
		default:
			status_item->setText(1, "Unknown");
			break;
	}
}


void PropertyView::addTerrainCustomProperties(QTreeWidgetItem& tree_item, const Lumix::Component& terrain_component)
{
	m_terrain_editor->m_tree_top_level = &tree_item;
	m_terrain_editor->m_component = terrain_component;

	{
		QWidget* widget = new QWidget();
		QTreeWidgetItem* item = new QTreeWidgetItem(QStringList() << "Save");
		tree_item.insertChild(0, item);
		QHBoxLayout* layout = new QHBoxLayout(widget);
		QPushButton* height_button = new QPushButton("Heightmap", widget);
		layout->addWidget(height_button);
		QPushButton* texture_button = new QPushButton("Splatmap", widget);
		layout->addWidget(texture_button);
		layout->setContentsMargins(2, 2, 2, 2);
		m_ui->propertyList->setItemWidget(item, 1, widget);
		connect(height_button, &QPushButton::clicked, [this]()
		{
			Lumix::Material* material = m_terrain_editor->getMaterial();
			material->getTextureByUniform("hm_texture")->save();
		});
		connect(texture_button, &QPushButton::clicked, [this]()
		{
			Lumix::Material* material = m_terrain_editor->getMaterial();
			material->getTextureByUniform("splat_texture")->save();
		});
	}

	QSlider* slider = new QSlider(Qt::Orientation::Horizontal);
	QTreeWidgetItem* item = new QTreeWidgetItem(QStringList() << "Brush size");
	tree_item.insertChild(1, item);
	m_ui->propertyList->setItemWidget(item, 1, slider);
	slider->setMinimum(1);
	slider->setMaximum(100);
	connect(slider, &QSlider::valueChanged, [this](int value)
	{
		m_terrain_editor->m_terrain_brush_size = value;
	});

	slider = new QSlider(Qt::Orientation::Horizontal);
	item = new QTreeWidgetItem(QStringList() << "Brush strength");
	tree_item.insertChild(2, item);
	m_ui->propertyList->setItemWidget(item, 1, slider);
	slider->setMinimum(-100);
	slider->setMaximum(100);
	connect(slider, &QSlider::valueChanged, [this](int value)
	{
		m_terrain_editor->m_terrain_brush_strength = value / 100.0f;
	});

	QWidget* widget = new QWidget();
	item = new QTreeWidgetItem(QStringList() << "Brush type");
	tree_item.insertChild(3, item);
	QHBoxLayout* layout = new QHBoxLayout(widget);
	QPushButton* height_button = new QPushButton("Height", widget);
	layout->addWidget(height_button);
	QPushButton* texture_button = new QPushButton("Texture", widget);
	layout->addWidget(texture_button);
	QPushButton* entity_button = new QPushButton("Entity", widget);
	layout->addWidget(entity_button);
	layout->setContentsMargins(2, 2, 2, 2);
	m_ui->propertyList->setItemWidget(item, 1, widget);
	m_terrain_editor->m_type = TerrainEditor::HEIGHT;
	connect(height_button, &QPushButton::clicked, [this]()
	{
		m_terrain_editor->m_type = TerrainEditor::HEIGHT;
		if (m_terrain_editor->m_texture_tree_item)
		{
			m_terrain_editor->m_tree_top_level->removeChild(m_terrain_editor->m_texture_tree_item);
		}
	});
	connect(texture_button, &QPushButton::clicked, this, &PropertyView::on_TerrainTextureTypeClicked);
	connect(entity_button, &QPushButton::clicked, [this]()
	{
		m_terrain_editor->m_type = TerrainEditor::ENTITY;
		if (m_terrain_editor->m_texture_tree_item)
		{
			m_terrain_editor->m_tree_top_level->removeChild(m_terrain_editor->m_texture_tree_item);
		}
	});

}


void PropertyView::on_TerrainTextureTypeClicked()
{
	m_terrain_editor->m_type = TerrainEditor::TEXTURE;

	QComboBox* combobox = new QComboBox();
	QTreeWidgetItem* item = new QTreeWidgetItem(QStringList() << "Texture");
	m_terrain_editor->m_tree_top_level->insertChild(4, item);
	Lumix::Material* material = m_terrain_editor->getMaterial();
	if (material && material->isReady())
	{
		for (int i = 1; i < material->getTextureCount() - 1; ++i)
		{
			combobox->addItem(material->getTexture(i)->getPath().c_str());
		}
	}
	m_ui->propertyList->setItemWidget(item, 1, combobox);

	m_terrain_editor->m_texture_tree_item = item;
	connect(combobox, (void (QComboBox::*)(int))&QComboBox::currentIndexChanged, this, &PropertyView::on_terrainBrushTextureChanged);
}


void PropertyView::on_terrainBrushTextureChanged(int value)
{
	m_terrain_editor->m_texture_idx = value;
}


void PropertyView::setSelectedResource(Lumix::Resource* resource)
{
	if(resource)
	{
		m_world_editor->selectEntities(NULL, 0);
	}
	clear();
	if (m_selected_resource)
	{
		m_selected_resource->getObserverCb().unbind<PropertyView, &PropertyView::onSelectedResourceLoaded>(this);
	}
	m_selected_resource = resource;
	if (resource)
	{
		m_selected_resource->onLoaded<PropertyView, &PropertyView::onSelectedResourceLoaded>(this);
	}
}


void PropertyView::onEntitySelected(const Lumix::Array<Lumix::Entity>& e)
{
	setSelectedResource(NULL);
	m_selected_entity = e.empty() ? Lumix::Entity::INVALID : e[0];
	clear();
	if (e.size() == 1 && e[0].isValid())
	{
		Lumix::Entity entity(e[0]);
		setObject(TypedObject(&entity, 0));
		m_ui->propertyList->expandAll();
		m_ui->propertyList->resizeColumnToContents(0);
		onEntityPosition(e[0]);
		m_ui->nameEdit->setText(e[0].getName());
	}
}


void PropertyView::on_addComponentButton_clicked()
{
	QByteArray s = m_ui->componentTypeCombo->currentText().toLocal8Bit();
	const char* c = s.data();

	for(int i = 0; i < sizeof(component_map) / sizeof(component_map[0]); i += 2)
	{
		if(strcmp(c, component_map[i]) == 0)
		{
			m_world_editor->addComponent(crc32(component_map[i+1]));
			Lumix::Array<Lumix::Entity> tmp(m_world_editor->getAllocator());
			tmp.push(m_selected_entity);
			onEntitySelected(tmp);
			return;
		}
	}
	ASSERT(false); // unknown component type
}


void PropertyView::updateSelectedEntityPosition()
{
	if(m_world_editor->getSelectedEntities().size() == 1)
	{
		Lumix::Array<Lumix::Vec3> positions(m_world_editor->getAllocator());
		positions.push(Lumix::Vec3((float)m_ui->positionX->value(), (float)m_ui->positionY->value(), (float)m_ui->positionZ->value()));
		m_world_editor->setEntitiesPositions(m_world_editor->getSelectedEntities(), positions);
	}
}


void PropertyView::on_positionX_valueChanged(double)
{
	updateSelectedEntityPosition();
}


void PropertyView::on_positionY_valueChanged(double)
{
	updateSelectedEntityPosition();
}


void PropertyView::on_positionZ_valueChanged(double)
{
	updateSelectedEntityPosition();
}


PropertyViewObject* PropertyView::getObject()
{
	return m_object;
}


void PropertyView::setObject(TypedObject object)
{
	clear();

	if (object.m_type == 0)
	{
		addEdit(*this, NULL, *static_cast<Lumix::Entity*>(object.m_object));
	}
	else
	{
		QTreeWidgetItem* item = new QTreeWidgetItem();
		m_ui->propertyList->insertTopLevelItem(0, item);
		if (dynamic_cast<Lumix::Model*>((Lumix::Resource*)object.m_object))
		{
			createModelEditor(*this, item, object);
		}
		else if (dynamic_cast<Lumix::Material*>((Lumix::Resource*)object.m_object))
		{
			createMaterialEditor(*this, item, object, "material");
		}
		else if (dynamic_cast<Lumix::Texture*>((Lumix::Resource*)object.m_object))
		{
			createTextureEditor(*this, item, object, "texture");
		}
	}
	m_ui->propertyList->expandToDepth(1);
	m_ui->propertyList->resizeColumnToContents(0);
}


void PropertyView::on_propertyList_customContextMenuRequested(const QPoint &pos)
{
	QMenu* menu = new QMenu("Item actions", NULL);
	const QModelIndex& index = m_ui->propertyList->indexAt(pos);
	if (index.isValid() && index.parent().isValid() && !index.parent().parent().isValid() && m_selected_entity.isValid())
	{
		QAction* remove_component_action = new QAction("Remove component", menu);
		menu->addAction(remove_component_action);
		QAction* action = menu->exec(m_ui->propertyList->mapToGlobal(pos));
		if (action == remove_component_action)
		{
			uint32_t cmp_hash = 0;
			QByteArray label = m_ui->propertyList->itemAt(pos)->text(0).toLatin1();
			for (int i = 0; i < sizeof(component_map) / sizeof(component_map[0]); i += 2)
			{
				if (strcmp(component_map[i], label.data()) == 0)
				{
					cmp_hash = crc32(component_map[i + 1]);
					break;
				}
			}
			const Lumix::WorldEditor::ComponentList& cmps = m_world_editor->getComponents(m_selected_entity);
			for (int i = 0, c = cmps.size(); i < c; ++i)
			{
				if (cmps[i].type == cmp_hash)
				{
					Lumix::Entity entity = cmps[i].entity;
					m_world_editor->destroyComponent(cmps[i]);
					Lumix::Array<Lumix::Entity> tmp(m_world_editor->getAllocator());
					tmp.push(m_selected_entity);
					onEntitySelected(tmp);
					break;
				}
			}
		}
	}
}


void PropertyView::on_nameEdit_editingFinished()
{
	if (m_selected_entity.isValid() && strcmp(m_ui->nameEdit->text().toLatin1().data(), m_selected_entity.getName()) != 0)
	{
		if (m_selected_entity.universe->nameExists(m_ui->nameEdit->text().toLatin1().data()))
		{
			static bool is = false;
			if (!is)
			{
				is = true;
				QMessageBox::critical(NULL, "Error", "Name already taken", QMessageBox::StandardButton::Ok, 0);
				is = false;
			}
		}
		else
		{
			m_world_editor->setEntityName(m_selected_entity, m_ui->nameEdit->text().toLatin1().data());
		}
	}
}

