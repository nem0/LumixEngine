#include "profilerui.h"
#include "ui_profilerui.h"
#include <qpainter.h>
#include <qpixmap.h>



ProfileModel::ProfileModel()
{
	Lumix::g_profiler.getFrameListeners().bind<ProfileModel, &ProfileModel::onFrame>(this);
	m_root = NULL;
	m_frame = 0;
}

void ProfileModel::cloneBlock(Block* my_block, Lumix::Profiler::Block* remote_block)
{
	ASSERT(my_block->m_name == remote_block->m_name);
	my_block->m_frames.push_back(remote_block->getLength());
	if(my_block->m_frames.size() > 200)
	{
		my_block->m_frames.pop_front();
	}
	if(!my_block->m_first_child && remote_block->m_first_child)
	{
		Lumix::Profiler::Block* remote_child = remote_block->m_first_child;
		Block* last_new_child = NULL;
		while(remote_child)
		{
			Block* my_child = new Block;
			my_child->m_function = remote_child->m_function;
			my_child->m_name = remote_child->m_name;
			my_child->m_parent = my_block;
			my_child->m_next = NULL;
			my_child->m_first_child = NULL;
			if(last_new_child)
			{
				last_new_child->m_next = my_child;
			}
			else
			{
				my_block->m_first_child = my_child;
			}
			last_new_child = my_child;
			cloneBlock(my_child, remote_child);
			remote_child = remote_child->m_next;
		}
	}
	else if(my_block->m_first_child)
	{
		Lumix::Profiler::Block* remote_child = remote_block->m_first_child;
		Block* my_child = my_block->m_first_child;
		while(remote_child)
		{
			if(my_child->m_name != remote_child->m_name && my_child->m_function != remote_child->m_function)
			{
				Block* new_child = new Block;
				new_child->m_function = remote_child->m_function;
				new_child->m_name = remote_child->m_name;
				new_child->m_parent = my_block;
				new_child->m_next = my_child;
				new_child->m_first_child = NULL;
				my_child = new_child;
				if(my_child == my_block->m_first_child)
				{
					my_block->m_first_child = new_child;
				}
			}
			cloneBlock(my_child, remote_child);
			remote_child = remote_child->m_next;
			my_child = my_child->m_next;
		}
	}
}

void ProfileModel::onFrame()
{
	if(!m_root && Lumix::g_profiler.getRootBlock())
	{
		m_root = new Block;
		m_root->m_function = Lumix::g_profiler.getRootBlock()->m_function;
		m_root->m_name = Lumix::g_profiler.getRootBlock()->m_name;
		m_root->m_parent = NULL;
		m_root->m_next = NULL;
		m_root->m_first_child = NULL;
	}
	if(m_root)
	{
		cloneBlock(m_root, Lumix::g_profiler.getRootBlock());
	}
	static int hit = 0;
	++hit;
	int count = 0;
	Block* child = m_root->m_first_child;
	while(child)
	{
		++count;
		child = child->m_next;
	}
	if(hit % 10 == 0)
	{
		emit dataChanged(createIndex(0, 0, m_root), createIndex(count, 0, m_root));
	}
}

QVariant ProfileModel::headerData(int section, Qt::Orientation, int role) const
{
	if(role == Qt::DisplayRole)
	{
		switch(section)
		{
			case Values::FUNCTION:
				return "Function";
			case Values::NAME:
				return "Name";
			case Values::LENGTH:
				return "Length (ms)";
			default:
				ASSERT(false);
				return QVariant();
		}
	}
	return QVariant();
}

QModelIndex ProfileModel::index(int row, int column, const QModelIndex& parent) const
{
	if(!hasIndex(row, column, parent))
	{
		return QModelIndex();
	}
	Block* block = NULL;
	if(parent.internalPointer() != NULL)
	{
		block = static_cast<Block*>(parent.internalPointer());
	}
	else
	{
		return createIndex(0, column, m_root);
	}

	int index = row;
	Block* child = block->m_first_child;
	while(child && index > 0)
	{
		child = child->m_next;
		--index;
	}

	return createIndex(row, column, child);
}
	
QModelIndex ProfileModel::parent(const QModelIndex& index) const
{
	if (!index.isValid() || !index.internalPointer())
	{
		return QModelIndex();
	}

	Block* child = static_cast<Block*>(index.internalPointer());
	Block* parent = child->m_parent;

	if (parent == NULL)
	{
		return QModelIndex();
	}

	int row = 0;
	Block* row_sibling = parent->m_first_child;
	while(row_sibling && row_sibling != child)
	{
		row_sibling = row_sibling->m_next;
		++row;
	}
	ASSERT(row_sibling);
	return createIndex(row, 0, parent);
}

int ProfileModel::rowCount(const QModelIndex& parent_index) const
{
	Block* parent;
	if (parent_index.column() > 0 || Lumix::g_profiler.getRootBlock() == NULL)
		return 0;

	if (!parent_index.isValid() || !parent_index.internalPointer())
	{
		return 1;
	}
	else
	{
		parent = static_cast<Block*>(parent_index.internalPointer());
	}


	int count = 0;
	Block* child = parent->m_first_child;
	while(child)
	{
		++count;
		child = child->m_next;
	}
	return count;
}

int ProfileModel::columnCount(const QModelIndex&) const
{
	return (int)Values::COUNT;
}

QVariant ProfileModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || !index.internalPointer())
	{
		return QVariant();
	}

	if (role != Qt::DisplayRole)
	{
		return QVariant();
	}
	Block* block = static_cast<Block*>(index.internalPointer());
	switch(index.column())
	{
		case Values::FUNCTION:
			return block->m_function;
		case Values::NAME:
			return block->m_name;
		case Values::LENGTH:
			{
				int idx = m_root->m_frames.size() - m_frame;
				return block->m_frames.size() >= idx ?  block->m_frames[block->m_frames.size() - idx] : 0;
			}
		default:
			ASSERT(false);
			return QVariant();
	}
}


ProfilerUI::ProfilerUI(QWidget* parent) 
	: QDockWidget(parent)
	, m_ui(new Ui::ProfilerUI)
{
	m_model = new ProfileModel;
	m_ui->setupUi(this);
	m_ui->profileTreeView->setModel(m_model);
	m_ui->profileTreeView->header()->setSectionResizeMode(0, QHeaderView::ResizeMode::Stretch);
	m_ui->profileTreeView->header()->setSectionResizeMode(1, QHeaderView::ResizeMode::ResizeToContents);
	m_ui->profileTreeView->header()->setSectionResizeMode(2, QHeaderView::ResizeMode::ResizeToContents);
	connect(m_model, &QAbstractItemModel::dataChanged, this, &ProfilerUI::on_dataChanged);
	connect(m_ui->graphView, &ProfilerGraph::frameSet, this, &ProfilerUI::on_frameSet);
	m_ui->graphView->setModel(m_model);
}


void ProfilerUI::on_dataChanged()
{
	m_ui->graphView->update();
}


ProfilerUI::~ProfilerUI()
{
	delete m_ui;
	delete m_model;
}

void ProfilerUI::on_recordCheckBox_stateChanged(int)
{
	Lumix::g_profiler.toggleRecording();
	m_ui->profileTreeView->setModel(NULL);
	m_ui->profileTreeView->setModel(m_model);
	m_ui->profileTreeView->update();
}


void ProfilerUI::on_frameSet()
{
	m_ui->recordCheckBox->setChecked(false);
	m_ui->profileTreeView->update();
	m_model->setFrame(m_ui->graphView->getFrame());
}


void ProfilerUI::on_profileTreeView_clicked(const QModelIndex &index)
{
	if(index.internalPointer() != NULL)
	{
		m_ui->graphView->setBlock(static_cast<ProfileModel::Block*>(index.internalPointer()));
		m_ui->graphView->update();
	}
}
