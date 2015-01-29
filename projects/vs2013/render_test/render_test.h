#ifndef RENDER_TEST_H
#define RENDER_TEST_H

#include <QtWidgets/QMainWindow>
#include "ui_render_test.h"

class RenderTest : public QMainWindow
{
	Q_OBJECT

public:
	RenderTest(QWidget *parent = 0);
	~RenderTest();

private:
	Ui::RenderTestClass ui;
};

#endif // RENDER_TEST_H
