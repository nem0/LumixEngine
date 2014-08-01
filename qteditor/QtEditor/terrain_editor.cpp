#include "terrain_editor.h"
#include "ui_terrain_editor.h"

TerrainEditor::TerrainEditor(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::TerrainEditor)
{
    ui->setupUi(this);
}

TerrainEditor::~TerrainEditor()
{
    delete ui;
}
