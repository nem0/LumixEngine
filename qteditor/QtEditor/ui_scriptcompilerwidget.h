/********************************************************************************
** Form generated from reading UI file 'scriptcompilerwidget.ui'
**
** Created by: Qt User Interface Compiler version 5.2.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_SCRIPTCOMPILERWIDGET_H
#define UI_SCRIPTCOMPILERWIDGET_H

#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QDockWidget>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListView>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QTextBrowser>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_ScriptCompilerWidget
{
public:
    QWidget *dockWidgetContents;
    QVBoxLayout *verticalLayout_2;
    QSplitter *splitter;
    QWidget *widget;
    QVBoxLayout *verticalLayout;
    QLabel *label;
    QListView *scriptListView;
    QWidget *widget_3;
    QVBoxLayout *verticalLayout_3;
    QLabel *label_3;
    QTextBrowser *compilerOutputView;
    QWidget *widget_4;
    QHBoxLayout *horizontalLayout;
    QPushButton *compileAllButton;
    QSpacerItem *horizontalSpacer;

    void setupUi(QDockWidget *ScriptCompilerWidget)
    {
        if (ScriptCompilerWidget->objectName().isEmpty())
            ScriptCompilerWidget->setObjectName(QStringLiteral("ScriptCompilerWidget"));
        ScriptCompilerWidget->resize(598, 417);
        dockWidgetContents = new QWidget();
        dockWidgetContents->setObjectName(QStringLiteral("dockWidgetContents"));
        verticalLayout_2 = new QVBoxLayout(dockWidgetContents);
        verticalLayout_2->setObjectName(QStringLiteral("verticalLayout_2"));
        verticalLayout_2->setContentsMargins(0, 0, 0, 0);
        splitter = new QSplitter(dockWidgetContents);
        splitter->setObjectName(QStringLiteral("splitter"));
        QSizePolicy sizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(splitter->sizePolicy().hasHeightForWidth());
        splitter->setSizePolicy(sizePolicy);
        splitter->setOrientation(Qt::Horizontal);
        widget = new QWidget(splitter);
        widget->setObjectName(QStringLiteral("widget"));
        verticalLayout = new QVBoxLayout(widget);
        verticalLayout->setObjectName(QStringLiteral("verticalLayout"));
        verticalLayout->setContentsMargins(0, 0, 0, 0);
        label = new QLabel(widget);
        label->setObjectName(QStringLiteral("label"));

        verticalLayout->addWidget(label);

        scriptListView = new QListView(widget);
        scriptListView->setObjectName(QStringLiteral("scriptListView"));

        verticalLayout->addWidget(scriptListView);

        splitter->addWidget(widget);
        widget_3 = new QWidget(splitter);
        widget_3->setObjectName(QStringLiteral("widget_3"));
        verticalLayout_3 = new QVBoxLayout(widget_3);
        verticalLayout_3->setSpacing(6);
        verticalLayout_3->setObjectName(QStringLiteral("verticalLayout_3"));
        verticalLayout_3->setContentsMargins(0, 0, 0, 0);
        label_3 = new QLabel(widget_3);
        label_3->setObjectName(QStringLiteral("label_3"));

        verticalLayout_3->addWidget(label_3);

        compilerOutputView = new QTextBrowser(widget_3);
        compilerOutputView->setObjectName(QStringLiteral("compilerOutputView"));

        verticalLayout_3->addWidget(compilerOutputView);

        splitter->addWidget(widget_3);

        verticalLayout_2->addWidget(splitter);

        widget_4 = new QWidget(dockWidgetContents);
        widget_4->setObjectName(QStringLiteral("widget_4"));
        horizontalLayout = new QHBoxLayout(widget_4);
        horizontalLayout->setObjectName(QStringLiteral("horizontalLayout"));
        horizontalLayout->setContentsMargins(0, 0, 0, 0);
        compileAllButton = new QPushButton(widget_4);
        compileAllButton->setObjectName(QStringLiteral("compileAllButton"));

        horizontalLayout->addWidget(compileAllButton);

        horizontalSpacer = new QSpacerItem(478, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout->addItem(horizontalSpacer);


        verticalLayout_2->addWidget(widget_4);

        ScriptCompilerWidget->setWidget(dockWidgetContents);

        retranslateUi(ScriptCompilerWidget);

        QMetaObject::connectSlotsByName(ScriptCompilerWidget);
    } // setupUi

    void retranslateUi(QDockWidget *ScriptCompilerWidget)
    {
        ScriptCompilerWidget->setWindowTitle(QApplication::translate("ScriptCompilerWidget", "Script compiler", 0));
        label->setText(QApplication::translate("ScriptCompilerWidget", "Scripts", 0));
        label_3->setText(QApplication::translate("ScriptCompilerWidget", "Compiler output", 0));
        compileAllButton->setText(QApplication::translate("ScriptCompilerWidget", "Compile All", 0));
    } // retranslateUi

};

namespace Ui {
    class ScriptCompilerWidget: public Ui_ScriptCompilerWidget {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_SCRIPTCOMPILERWIDGET_H
