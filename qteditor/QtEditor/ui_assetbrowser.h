/********************************************************************************
** Form generated from reading UI file 'assetbrowser.ui'
**
** Created by: Qt User Interface Compiler version 5.2.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_ASSETBROWSER_H
#define UI_ASSETBROWSER_H

#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QDockWidget>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QTreeView>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_AssetBrowser
{
public:
    QWidget *dockWidgetContents;
    QVBoxLayout *verticalLayout;
    QLineEdit *searchInput;
    QTreeView *treeView;
    QListWidget *listWidget;

    void setupUi(QDockWidget *AssetBrowser)
    {
        if (AssetBrowser->objectName().isEmpty())
            AssetBrowser->setObjectName(QStringLiteral("AssetBrowser"));
        AssetBrowser->resize(298, 438);
        dockWidgetContents = new QWidget();
        dockWidgetContents->setObjectName(QStringLiteral("dockWidgetContents"));
        verticalLayout = new QVBoxLayout(dockWidgetContents);
        verticalLayout->setSpacing(2);
        verticalLayout->setObjectName(QStringLiteral("verticalLayout"));
        verticalLayout->setContentsMargins(2, 2, 2, 2);
        searchInput = new QLineEdit(dockWidgetContents);
        searchInput->setObjectName(QStringLiteral("searchInput"));

        verticalLayout->addWidget(searchInput);

        treeView = new QTreeView(dockWidgetContents);
        treeView->setObjectName(QStringLiteral("treeView"));
        treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
        treeView->setDragEnabled(true);
        treeView->setDragDropMode(QAbstractItemView::DragOnly);
        treeView->setUniformRowHeights(true);
        treeView->setHeaderHidden(true);
        treeView->header()->setVisible(false);

        verticalLayout->addWidget(treeView);

        listWidget = new QListWidget(dockWidgetContents);
        listWidget->setObjectName(QStringLiteral("listWidget"));
        listWidget->setDragEnabled(true);

        verticalLayout->addWidget(listWidget);

        AssetBrowser->setWidget(dockWidgetContents);

        retranslateUi(AssetBrowser);

        QMetaObject::connectSlotsByName(AssetBrowser);
    } // setupUi

    void retranslateUi(QDockWidget *AssetBrowser)
    {
        AssetBrowser->setWindowTitle(QApplication::translate("AssetBrowser", "Asset browser", 0));
    } // retranslateUi

};

namespace Ui {
    class AssetBrowser: public Ui_AssetBrowser {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_ASSETBROWSER_H
