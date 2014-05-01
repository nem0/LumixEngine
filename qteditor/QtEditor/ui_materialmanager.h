/********************************************************************************
** Form generated from reading UI file 'materialmanager.ui'
**
** Created by: Qt User Interface Compiler version 5.2.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_MATERIALMANAGER_H
#define UI_MATERIALMANAGER_H

#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QDockWidget>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QListView>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>
#include "renderdevicewidget.h"

QT_BEGIN_NAMESPACE

class Ui_MaterialManager
{
public:
    QWidget *dockWidgetContents;
    QVBoxLayout *verticalLayout;
    QSplitter *splitter;
    QTabWidget *tabWidget;
    QWidget *tab;
    QVBoxLayout *verticalLayout_2;
    QListView *fileListView;
    QWidget *tab_2;
    QVBoxLayout *verticalLayout_3;
    QListWidget *objectMaterialList;
    RenderDeviceWidget *previewWidget;
    QWidget *widget_2;

    void setupUi(QDockWidget *MaterialManager)
    {
        if (MaterialManager->objectName().isEmpty())
            MaterialManager->setObjectName(QStringLiteral("MaterialManager"));
        MaterialManager->resize(400, 300);
        dockWidgetContents = new QWidget();
        dockWidgetContents->setObjectName(QStringLiteral("dockWidgetContents"));
        verticalLayout = new QVBoxLayout(dockWidgetContents);
        verticalLayout->setObjectName(QStringLiteral("verticalLayout"));
        splitter = new QSplitter(dockWidgetContents);
        splitter->setObjectName(QStringLiteral("splitter"));
        splitter->setOrientation(Qt::Horizontal);
        tabWidget = new QTabWidget(splitter);
        tabWidget->setObjectName(QStringLiteral("tabWidget"));
        tab = new QWidget();
        tab->setObjectName(QStringLiteral("tab"));
        verticalLayout_2 = new QVBoxLayout(tab);
        verticalLayout_2->setObjectName(QStringLiteral("verticalLayout_2"));
        verticalLayout_2->setContentsMargins(0, 0, 0, 0);
        fileListView = new QListView(tab);
        fileListView->setObjectName(QStringLiteral("fileListView"));

        verticalLayout_2->addWidget(fileListView);

        tabWidget->addTab(tab, QString());
        tab_2 = new QWidget();
        tab_2->setObjectName(QStringLiteral("tab_2"));
        verticalLayout_3 = new QVBoxLayout(tab_2);
        verticalLayout_3->setObjectName(QStringLiteral("verticalLayout_3"));
        verticalLayout_3->setContentsMargins(0, 0, 0, 0);
        objectMaterialList = new QListWidget(tab_2);
        objectMaterialList->setObjectName(QStringLiteral("objectMaterialList"));

        verticalLayout_3->addWidget(objectMaterialList);

        tabWidget->addTab(tab_2, QString());
        splitter->addWidget(tabWidget);
        previewWidget = new RenderDeviceWidget(splitter);
        previewWidget->setObjectName(QStringLiteral("previewWidget"));
        splitter->addWidget(previewWidget);
        widget_2 = new QWidget(splitter);
        widget_2->setObjectName(QStringLiteral("widget_2"));
        splitter->addWidget(widget_2);

        verticalLayout->addWidget(splitter);

        MaterialManager->setWidget(dockWidgetContents);

        retranslateUi(MaterialManager);

        tabWidget->setCurrentIndex(1);


        QMetaObject::connectSlotsByName(MaterialManager);
    } // setupUi

    void retranslateUi(QDockWidget *MaterialManager)
    {
        MaterialManager->setWindowTitle(QApplication::translate("MaterialManager", "Material manager", 0));
        tabWidget->setTabText(tabWidget->indexOf(tab), QApplication::translate("MaterialManager", "Files", 0));
        tabWidget->setTabText(tabWidget->indexOf(tab_2), QApplication::translate("MaterialManager", "Object", 0));
    } // retranslateUi

};

namespace Ui {
    class MaterialManager: public Ui_MaterialManager {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_MATERIALMANAGER_H
