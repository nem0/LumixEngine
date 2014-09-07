#include "profilerui.h"
#include "ui_profilerui.h"
#include <qpainter.h>
#include <qpixmap.h>

static const int MAX_FRAMES = 200;

ProfileModel::Block::Block()
{
	m_frames.reserve(MAX_FRAMES);
	for(int i = 0; i < MAX_FRAMES; ++i)
	{
		m_frames.push_back(0);
	}
	m_hit_counts.reserve(MAX_FRAMES);
	for (int i = 0; i < MAX_FRAMES; ++i)
	{
		m_hit_counts.push_back(0);
	}
}


ProfileModel::ProfileModel(QWidget* parent)
	: QAbstractItemModel(parent)
{
	Lumix::g_profiler.getFrameListeners().bind<ProfileModel, &ProfileModel::onFrame>(this);
	m_root = NULL;
	m_frame = -1;
}

void ProfileModel::cloneBlock(Block* my_block, Lumix::Profiler::Block* remote_block)
{
	ASSERT(my_block->m_name == remote_block->m_name);
	my_block->m_frames.push_back(remote_block->getLength());
	my_block->m_hit_counts.push_back(remote_block->getHitCount());
	if (my_block->m_frames.size() > MAX_FRAMES)
	{
		my_block->m_frames.pop_front();
	}
	if (my_block->m_hit_counts.size() > MAX_FRAMES)
	{
		my_block->m_hit_counts.pop_front();
	}

	if (!my_block->m_first_child && remote_block->m_first_child)
	{
		Lumix::Profiler::Block* remote_child = remote_block->m_first_child;
		Block* my_child = new Block;
		my_child->m_function = remote_child->m_function;
		my_child->m_name = remote_child->m_name;
		my_child->m_parent = my_block;
		my_child->m_next = NULL;
		my_child->m_first_child = NULL;
		my_block->m_first_child = my_child;
		cloneBlock(my_child, remote_child);
	}
	else if(my_block->m_first_child)
	{
		Lumix::Profiler::Block* remote_child = remote_block->m_first_child;
		Block* my_child = my_block->m_first_child;
		cloneBlock(my_child, remote_child);
	}

	if (!my_block->m_next && remote_block->m_next)
	{
		Lumix::Profiler::Block* remote_next = remote_block->m_next;
		Block* my_next = new Block;
		my_next->m_function = remote_next->m_function;
		my_next->m_name = remote_next->m_name;
		my_next->m_parent = my_block->m_parent;
		my_next->m_next = NULL;
		my_next->m_first_child = NULL;
		my_block->m_next = my_next;
		cloneBlock(my_next, remote_next);

	}
	else if (my_block->m_next)
	{
		cloneBlock(my_block->m_next, remote_block->m_next);
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
			case Values::LENGTH_EXCLUSIVE:
				return "Length exclusive (ms)";
			case Values::HIT_COUNT:
				return "Hit count";
				break;
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
		block = static_cast<Block*>(parent.internalPointer())->m_first_child;
	}
	else
	{
		block = m_root;
	}

	int index = row;
	while (block && index > 0)
	{
		block = block->m_next;
		--index;
	}

	return createIndex(row, column, block);
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
		int count = 0;
		Block* root = m_root;
		while (root)
		{
			++count;
			root = root->m_next;
		}
		return count;
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
			return m_frame >= 0 && m_frame < block->m_frames.size() ? block->m_frames[m_frame] : (block->m_frames.isEmpty() ? 0 : block->m_frames.back());
		case Values::LENGTH_EXCLUSIVE:
			{
				if (m_frame >= 0 && m_frame < block->m_frames.size())
				{
					float length = block->m_frames[m_frame];
					Block* child = block->m_first_child;
					while (child)
					{
						length -= m_frame < child->m_frames.size() ? child->m_frames[m_frame] : (child->m_frames.isEmpty() ? 0 : child->m_frames.back());
						child = child->m_next;
					}
					return length;
				}
				else
				{
					float length = block->m_frames.isEmpty() ? 0 : block->m_frames.back();
					Block* child = block->m_first_child;
					while (child)
					{
						length -= child->m_frames.isEmpty() ? 0 : child->m_frames.back();
						child = child->m_next;
					}
					return length;
				}
			}
			break;
		case Values::HIT_COUNT:
			return m_frame >= 0 && m_frame < block->m_hit_counts.size() ? block->m_hit_counts[m_frame] : (block->m_hit_counts.isEmpty() ? 0 : block->m_hit_counts.back());
			break;
		default:
			ASSERT(false);
			return QVariant();
	}
}


ProfilerUI::ProfilerUI(QWidget* parent) 
	: QDockWidget(parent)
	, m_ui(new Ui::ProfilerUI)
{
	m_sortable_model = new QSortFilterProxyModel(this);
	m_model = new ProfileModel(this);
	m_sortable_model->setSourceModel(m_model);
	m_ui->setupUi(this);
	m_ui->profileTreeView->setModel(m_sortable_model);
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
}

void ProfilerUI::on_recordCheckBox_stateChanged(int)
{
	Lumix::g_profiler.toggleRecording();
	m_ui->profileTreeView->setModel(NULL);
	m_sortable_model->setSourceModel(m_model);
	m_ui->profileTreeView->setModel(m_sortable_model);
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
