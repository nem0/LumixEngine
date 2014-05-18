/********************************************************************************
** Form generated from reading UI file 'profilerui.ui'
**
** Created by: Qt User Interface Compiler version 5.2.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_PROFILERUI_H
#define UI_PROFILERUI_H

#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDockWidget>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QSlider>
#include <QtWidgets/QTreeView>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_ProfilerUI
{
public:
    QWidget *dockWidgetContents;
    QVBoxLayout *verticalLayout;
    QTreeView *profileTreeView;
    QWidget *widget;
    QHBoxLayout *horizontalLayout;
    QCheckBox *recordCheckBox;
    QSlider *frameSlider;

    void setupUi(QDockWidget *ProfilerUI)
    {
        if (ProfilerUI->objectName().isEmpty())
            ProfilerUI->setObjectName(QStringLiteral("ProfilerUI"));
        ProfilerUI->resize(766, 548);
        dockWidgetContents = new QWidget();
        dockWidgetContents->setObjectName(QStringLiteral("dockWidgetContents"));
        verticalLayout = new QVBoxLayout(dockWidgetContents);
        verticalLayout->setObjectName(QStringLiteral("verticalLayout"));
        verticalLayout->setContentsMargins(2, 2, 2, 2);
        profileTreeView = new QTreeView(dockWidgetContents);
        profileTreeView->setObjectName(QStringLiteral("profileTreeView"));
        profileTreeView->setAlternatingRowColors(true);
        profileTreeView->header()->setVisible(true);
        profileTreeView->header()->setDefaultSectionSize(200);
        profileTreeView->header()->setStretchLastSection(false);

        verticalLayout->addWidget(profileTreeView);

        widget = new QWidget(dockWidgetContents);
        widget->setObjectName(QStringLiteral("widget"));
        horizontalLayout = new QHBoxLayout(widget);
        horizontalLayout->setObjectName(QStringLiteral("horizontalLayout"));
        horizontalLayout->setContentsMargins(2, 2, 2, 2);
        recordCheckBox = new QCheckBox(widget);
        recordCheckBox->setObjectName(QStringLiteral("recordCheckBox"));

        horizontalLayout->addWidget(recordCheckBox);

        frameSlider = new QSlider(widget);
        frameSlider->setObjectName(QStringLiteral("frameSlider"));
        frameSlider->setOrientation(Qt::Horizontal);

        horizontalLayout->addWidget(frameSlider);


        verticalLayout->addWidget(widget);

        ProfilerUI->setWidget(dockWidgetContents);

        retranslateUi(ProfilerUI);

        QMetaObject::connectSlotsByName(ProfilerUI);
    } // setupUi

    void retranslateUi(QDockWidget *ProfilerUI)
    {
        ProfilerUI->setWindowTitle(QApplication::translate("ProfilerUI", "Profiler", 0));
        recordCheckBox->setText(QApplication::translate("ProfilerUI", "Record", 0));
    } // retranslateUi

};

namespace Ui {
    class ProfilerUI: public Ui_ProfilerUI {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_PROFILERUI_H
