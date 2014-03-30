/********************************************************************************
** Form generated from reading UI file 'mainwindow.ui'
**
** Created by: Qt User Interface Compiler version 5.2.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_MAINWINDOW_H
#define UI_MAINWINDOW_H

#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_MainWindow
{
public:
    QAction *actionLog;
    QAction *actionNew;
    QAction *actionOpen;
    QAction *actionSave;
    QAction *actionE_xit;
    QAction *actionSave_As;
    QAction *actionGame_mode;
    QAction *actionCompile_scripts;
    QAction *actionCreate;
    QAction *actionProperties;
    QAction *actionGame_view;
    QWidget *centralWidget;
    QGridLayout *gridLayout;
    QMenuBar *menuBar;
    QMenu *menuView;
    QMenu *menuFile;
    QMenu *menuTools;
    QMenu *menuEntity;

    void setupUi(QMainWindow *MainWindow)
    {
        if (MainWindow->objectName().isEmpty())
            MainWindow->setObjectName(QStringLiteral("MainWindow"));
        MainWindow->resize(892, 658);
        MainWindow->setDockNestingEnabled(true);
        actionLog = new QAction(MainWindow);
        actionLog->setObjectName(QStringLiteral("actionLog"));
        actionNew = new QAction(MainWindow);
        actionNew->setObjectName(QStringLiteral("actionNew"));
        actionOpen = new QAction(MainWindow);
        actionOpen->setObjectName(QStringLiteral("actionOpen"));
        actionSave = new QAction(MainWindow);
        actionSave->setObjectName(QStringLiteral("actionSave"));
        actionE_xit = new QAction(MainWindow);
        actionE_xit->setObjectName(QStringLiteral("actionE_xit"));
        actionSave_As = new QAction(MainWindow);
        actionSave_As->setObjectName(QStringLiteral("actionSave_As"));
        actionGame_mode = new QAction(MainWindow);
        actionGame_mode->setObjectName(QStringLiteral("actionGame_mode"));
        actionCompile_scripts = new QAction(MainWindow);
        actionCompile_scripts->setObjectName(QStringLiteral("actionCompile_scripts"));
        actionCreate = new QAction(MainWindow);
        actionCreate->setObjectName(QStringLiteral("actionCreate"));
        actionProperties = new QAction(MainWindow);
        actionProperties->setObjectName(QStringLiteral("actionProperties"));
        actionGame_view = new QAction(MainWindow);
        actionGame_view->setObjectName(QStringLiteral("actionGame_view"));
        centralWidget = new QWidget(MainWindow);
        centralWidget->setObjectName(QStringLiteral("centralWidget"));
        centralWidget->setEnabled(true);
        QSizePolicy sizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(centralWidget->sizePolicy().hasHeightForWidth());
        centralWidget->setSizePolicy(sizePolicy);
        centralWidget->setMaximumSize(QSize(0, 0));
        gridLayout = new QGridLayout(centralWidget);
        gridLayout->setSpacing(6);
        gridLayout->setContentsMargins(11, 11, 11, 11);
        gridLayout->setObjectName(QStringLiteral("gridLayout"));
        MainWindow->setCentralWidget(centralWidget);
        menuBar = new QMenuBar(MainWindow);
        menuBar->setObjectName(QStringLiteral("menuBar"));
        menuBar->setGeometry(QRect(0, 0, 892, 21));
        menuView = new QMenu(menuBar);
        menuView->setObjectName(QStringLiteral("menuView"));
        menuFile = new QMenu(menuBar);
        menuFile->setObjectName(QStringLiteral("menuFile"));
        menuTools = new QMenu(menuBar);
        menuTools->setObjectName(QStringLiteral("menuTools"));
        menuEntity = new QMenu(menuBar);
        menuEntity->setObjectName(QStringLiteral("menuEntity"));
        MainWindow->setMenuBar(menuBar);

        menuBar->addAction(menuFile->menuAction());
        menuBar->addAction(menuView->menuAction());
        menuBar->addAction(menuTools->menuAction());
        menuBar->addAction(menuEntity->menuAction());
        menuView->addAction(actionLog);
        menuView->addAction(actionProperties);
        menuView->addAction(actionGame_view);
        menuFile->addAction(actionNew);
        menuFile->addAction(actionOpen);
        menuFile->addAction(actionSave);
        menuFile->addAction(actionSave_As);
        menuFile->addAction(actionE_xit);
        menuTools->addAction(actionGame_mode);
        menuTools->addAction(actionCompile_scripts);
        menuEntity->addAction(actionCreate);

        retranslateUi(MainWindow);

        QMetaObject::connectSlotsByName(MainWindow);
    } // setupUi

    void retranslateUi(QMainWindow *MainWindow)
    {
        MainWindow->setWindowTitle(QApplication::translate("MainWindow", "Lux Editor", 0));
        actionLog->setText(QApplication::translate("MainWindow", "Log", 0));
        actionNew->setText(QApplication::translate("MainWindow", "&New", 0));
        actionOpen->setText(QApplication::translate("MainWindow", "&Open", 0));
        actionSave->setText(QApplication::translate("MainWindow", "&Save", 0));
        actionE_xit->setText(QApplication::translate("MainWindow", "E&xit", 0));
        actionSave_As->setText(QApplication::translate("MainWindow", "Save As", 0));
        actionGame_mode->setText(QApplication::translate("MainWindow", "Game mode", 0));
        actionCompile_scripts->setText(QApplication::translate("MainWindow", "Compile scripts", 0));
        actionCreate->setText(QApplication::translate("MainWindow", "Create", 0));
        actionProperties->setText(QApplication::translate("MainWindow", "Properties", 0));
        actionGame_view->setText(QApplication::translate("MainWindow", "Game view", 0));
        menuView->setTitle(QApplication::translate("MainWindow", "View", 0));
        menuFile->setTitle(QApplication::translate("MainWindow", "File", 0));
        menuTools->setTitle(QApplication::translate("MainWindow", "Tools", 0));
        menuEntity->setTitle(QApplication::translate("MainWindow", "Entity", 0));
    } // retranslateUi

};

namespace Ui {
    class MainWindow: public Ui_MainWindow {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_MAINWINDOW_H
