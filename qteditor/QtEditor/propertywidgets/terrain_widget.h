#pragma once

#include <QFrame>
#include "property_widget_base.h"

namespace Ui
{
	class TerrainWidget;
}

class TerrainWidget : public PropertyWidgetBase
{
		Q_OBJECT

	public:
		explicit TerrainWidget(QWidget* parent = NULL);
		~TerrainWidget();

		virtual const char* getTitle() const override { return "Terrain"; }
		virtual void onEntityProperties(Lux::PropertyListEvent& event) override;

	private slots:
		void on_heightmapEdit_editingFinished();
		void on_browseHeightmap_clicked();

		void on_browseMaterial_clicked();

		void on_materialEdit_editingFinished();

	private:
		Ui::TerrainWidget* m_ui;
};

