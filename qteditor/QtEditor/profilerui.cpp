#include "profilerui.h"
#include "ui_profilerui.h"
#include <qabstractitemmodel.h>
#include <qpainter.h>
#include <qpixmap.h>
#include "core/profiler.h"


class ProfileModel : public QAbstractItemModel
{
	public:
		enum class Values
		{
			NAME,
			FUNCTION,
			LENGTH,
			COUNT
		};

		ProfileModel()
		{
			m_frame_uid = 0;
			Lux::g_profiler.getFrameListeners().bind<ProfileModel, &ProfileModel::onFrame>(this);
		}

		void onFrame(int frame_uid)
		{
			if(frame_uid % 10 == 0 && Lux::g_profiler.getRootBlock()) // do not update too fast, it consumes too many resources
			{
				m_frame_uid = frame_uid;
				int count = 0;
				Lux::Profiler::Block* block = Lux::g_profiler.getRootBlock()->m_first_child;
				while(block && block->m_next)
				{
					block = block->m_next;
					++count;
				}
				emit dataChanged(createIndex(0, 0, (void*)Lux::g_profiler.getRootBlock()), createIndex(count, 0, (void*)Lux::g_profiler.getRootBlock()));
			}
		}

		void setFrameOffset(int offset)
		{
			m_frame_offset = offset;
			onFrame(m_frame_uid);
		}

		virtual QVariant headerData(int section, Qt::Orientation, int role = Qt::DisplayRole) const override
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

		virtual QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override
		{
			if(!hasIndex(row, column, parent))
			{
				return QModelIndex();
			}
			Lux::Profiler::Block* block = NULL;
			if(parent.internalPointer() != NULL)
			{
				block = static_cast<Lux::Profiler::Block*>(parent.internalPointer());
			}
			else
			{
				return createIndex(0, column, Lux::g_profiler.getRootBlock());
			}

			int index = row;
			Lux::Profiler::Block* child = block->m_first_child;
			while(child && index > 0)
			{
				child = child->m_next;
				--index;
			}

			return createIndex(row, column, child);
		}
	
		virtual QModelIndex parent(const QModelIndex& index) const override
		{
			if (!index.isValid() || !index.internalPointer())
			{
				return QModelIndex();
			}

			Lux::Profiler::Block* child = static_cast<Lux::Profiler::Block*>(index.internalPointer());
			Lux::Profiler::Block* parent = child->m_parent;

			if (parent == NULL)
			{
				return QModelIndex();
			}

			int row = 0;
			Lux::Profiler::Block* row_sibling = parent->m_first_child;
			while(row_sibling && row_sibling != child)
			{
				row_sibling = row_sibling->m_next;
				++row;
			}
			ASSERT(row_sibling);
			return createIndex(row, 0, parent);
		}

		virtual int rowCount(const QModelIndex& parent_index) const override
		{
			Lux::Profiler::Block* parent;
			if (parent_index.column() > 0 || Lux::g_profiler.getRootBlock() == NULL)
				return 0;

			if (!parent_index.isValid() || !parent_index.internalPointer())
			{
				return 1;
			}
			else
			{
				parent = static_cast<Lux::Profiler::Block*>(parent_index.internalPointer());
			}


			int count = 0;
			Lux::Profiler::Block* child = parent->m_first_child;
			while(child)
			{
				++count;
				child = child->m_next;
			}
			return count;
		}

		virtual int columnCount(const QModelIndex&) const override
		{
			return (int)Values::COUNT;
		}

		virtual QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override
		{
			if (!index.isValid() || !index.internalPointer())
			{
				return QVariant();
			}

			if (role != Qt::DisplayRole)
			{
				return QVariant();
			}
			Lux::Profiler::Block* block = static_cast<Lux::Profiler::Block*>(index.internalPointer());
			switch(index.column())
			{
				case Values::FUNCTION:
					return block->m_function;
				case Values::NAME:
					return block->m_name;
				case Values::LENGTH:
					{
						int idx = (block->m_frame_index + m_frame_offset - Lux::Profiler::Block::MAX_FRAMES + 1) % Lux::Profiler::Block::MAX_FRAMES;
						return idx < 0 ? 0 : block->m_frames[idx].m_length;
					}
				default:
					ASSERT(false);
					return QVariant();
			}
		}


		int m_frame_uid;
		int m_frame_offset;
};


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
	Lux::g_profiler.toggleRecording();
	m_ui->profileTreeView->setModel(NULL);
	m_ui->profileTreeView->setModel(m_model);
}


void ProfilerUI::on_frameSet()
{
	((ProfileModel*)m_model)->setFrameOffset(m_ui->graphView->getFrame());
	m_ui->recordCheckBox->setChecked(false);
}


void ProfilerUI::on_profileTreeView_clicked(const QModelIndex &index)
{
	if(index.internalPointer() != NULL)
	{
		m_ui->graphView->setBlock(static_cast<Lux::Profiler::Block*>(index.internalPointer()));
	}
}
