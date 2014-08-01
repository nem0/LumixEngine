#ifndef TERRAIN_EDITOR_H
#define TERRAIN_EDITOR_H

#include <QWidget>

namespace Ui {
class TerrainEditor;
}

class TerrainEditor : public QWidget
{
    Q_OBJECT

public:
    explicit TerrainEditor(QWidget *parent = 0);
    ~TerrainEditor();

private:
    Ui::TerrainEditor *ui;
};

#endif // TERRAIN_EDITOR_H
