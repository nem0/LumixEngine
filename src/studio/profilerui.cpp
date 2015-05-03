#include "profilerui.h"
#include "ui_profilerui.h"
#include <qpainter.h>
#include <qpixmap.h>

static const int MAX_FRAMES = 200;

class ProfilerFilterModel : public QSortFilterProxyModel
{
	public:
		explicit ProfilerFilterModel(QObject *parent)
			: QSortFilterProxyModel(parent)
		{}

		bool check(ProfileModel::Block* block, const QRegExp& regexp) const
		{
			if (QString(block->m_name).contains(regexp))
			{
				return true;
			}
			auto* child = block->m_first_child;
			while (child)
			{
				if (check(child, regexp))
					return true;
				child = child->m_next;
			}
			return false;
		}

		virtual bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const
		{
			auto* block = static_cast<ProfileModel::Block*>(sourceModel()->index(source_row, 0, source_parent).internalPointer());
			return check(block, filterRegExp());
		}
};


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

QModelIndex ProfileModel::getIndex(Block* block)
{
	if(!block)
		return QModelIndex();
	return createIndex(getRow(block), 0, block);
}

int ProfileModel::getRow(Block* block)
{
	int row = 0;
	Block* b = block->m_parent ? block->m_parent->m_first_child : m_root;
	while(b && b != block)
	{
		b = b->m_next;
		++row;
	}
	return row;
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
		beginInsertRows(getIndex(my_block), 0, 0);
		Lumix::Profiler::Block* remote_child = remote_block->m_first_child;
		Block* my_child = new Block;
		my_child->m_name = remote_child->m_name;
		my_child->m_parent = my_block;
		my_child->m_next = NULL;
		my_child->m_first_child = NULL;
		my_block->m_first_child = my_child;
		endInsertRows();
		cloneBlock(my_child, remote_child);
	}
	else if(my_block->m_first_child)
	{
		Lumix::Profiler::Block* remote_child = remote_block->m_first_child;
		Block* my_child = my_block->m_first_child;
		if(my_child->m_name != remote_child->m_name)
		{
			beginInsertRows(getIndex(my_block), 0, 0);
			Block* my_new_child = new Block;
			my_new_child->m_name = remote_child->m_name;
			my_new_child->m_parent = my_block;
			my_new_child->m_next = my_child;
			my_new_child->m_first_child = NULL;
			my_block->m_first_child = my_new_child;
			my_child = my_new_child;
			endInsertRows();
		}
		cloneBlock(my_child, remote_child);
	}

	if (!my_block->m_next && remote_block->m_next)
	{
		int row = getRow(my_block) + 1;
		beginInsertRows(getIndex(my_block->m_parent), row, row);
		Lumix::Profiler::Block* remote_next = remote_block->m_next;
		Block* my_next = new Block;
		my_next->m_name = remote_next->m_name;
		my_next->m_parent = my_block->m_parent;
		my_next->m_next = NULL;
		my_next->m_first_child = NULL;
		my_block->m_next = my_next;
		endInsertRows();
		cloneBlock(my_next, remote_next);
	}
	else if (my_block->m_next)
	{
		if(my_block->m_next->m_name != remote_block->m_next->m_name)
		{
			int row = getRow(my_block) + 1;
			beginInsertRows(getIndex(my_block->m_parent), row, row);
			Block* my_next = new Block;
			Lumix::Profiler::Block* remote_next = remote_block->m_next;
			my_next->m_name = remote_next->m_name;
			my_next->m_parent = my_block->m_parent;
			my_next->m_next = my_block->m_next;
			my_next->m_first_child = NULL;
			my_block->m_next = my_next;
			endInsertRows();
		}
		cloneBlock(my_block->m_next, remote_block->m_next);
	}
}

void ProfileModel::onFrame()
{
	if(!m_root && Lumix::g_profiler.getRootBlock())
	{
		beginInsertRows(QModelIndex(), 0, 0);
		m_root = new Block;
		m_root->m_name = Lumix::g_profiler.getRootBlock()->m_name;
		m_root->m_parent = NULL;
		m_root->m_next = NULL;
		m_root->m_first_child = NULL;
		endInsertRows();
	}
	else
	{
		ASSERT(m_root->m_name == Lumix::g_profiler.getRootBlock()->m_name);
	}
	if(m_root)
	{
		cloneBlock(m_root, Lumix::g_profiler.getRootBlock());
	}

	static int hit = 0;
	++hit;
	int count = 0;
	Block* block = m_root;
	Block* last_block = block;
	while(block)
	{
		++count;
		last_block = block;
		block = block->m_next;
	}
	if(hit % 10 == 0 && m_root->m_first_child)
	{
		emit dataChanged(createIndex(0, 1, m_root), createIndex(count - 1, (int)Values::COUNT - 1, last_block));
		emitDataChanged(m_root);
	}
}


void ProfileModel::emitDataChanged(Block* block)
{
	if(block->m_first_child)
	{
		int row = 0;
		Block* last_child = block->m_first_child;
		while(last_child->m_next)
		{
			++row;
			last_child = last_child->m_next;
		}
		emit dataChanged(createIndex(0, 1, block->m_first_child), createIndex(row, (int)Values::COUNT - 1, last_child));

		Block* child = block->m_first_child;
		while(child)
		{
			emitDataChanged(child);
			child = child->m_next;
		}
	}
}

QVariant ProfileModel::headerData(int section, Qt::Orientation, int role) const
{
	if(role == Qt::DisplayRole)
	{
		switch(section)
		{
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
	m_model = new ProfileModel(this);
	m_sortable_model = new ProfilerFilterModel(this);
	m_sortable_model->setSourceModel(m_model);
	m_ui->setupUi(this);
	m_ui->profileTreeView->setModel(m_sortable_model);
	m_ui->profileTreeView->header()->setSectionResizeMode(0, QHeaderView::ResizeMode::Stretch);
	m_ui->profileTreeView->header()->setSectionResizeMode(1, QHeaderView::ResizeMode::ResizeToContents);
	m_ui->profileTreeView->header()->setSectionResizeMode(2, QHeaderView::ResizeMode::ResizeToContents);
	connect(m_ui->filterInput, &QLineEdit::textChanged, this, &ProfilerUI::on_filterChanged);
	connect(m_model, &QAbstractItemModel::dataChanged, this, &ProfilerUI::on_dataChanged);
	connect(m_ui->graphView, &ProfilerGraph::frameSet, this, &ProfilerUI::on_frameSet);
	m_ui->graphView->setModel(m_model);
}


void ProfilerUI::on_filterChanged(const QString& value)
{
	m_sortable_model->setFilterRegExp(value);
	m_sortable_model->setFilterCaseSensitivity(Qt::CaseInsensitive);
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
}


void ProfilerUI::on_frameSet()
{
	m_ui->recordCheckBox->setChecked(false);
	m_ui->profileTreeView->update();
	m_model->setFrame(m_ui->graphView->getFrame());
}


void ProfilerUI::on_profileTreeView_clicked(const QModelIndex &index)
{
	void* ptr = m_sortable_model->mapToSource(index).internalPointer();
	if(ptr)
	{
		m_ui->graphView->setBlock(static_cast<ProfileModel::Block*>(ptr));
		//m_ui->graphView->setBlock(static_cast<ProfileModel::Block*>(index.internalPointer()));
		m_ui->graphView->update();
	}
}
