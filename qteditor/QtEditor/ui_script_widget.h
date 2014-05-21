/********************************************************************************
** Form generated from reading UI file 'script_widget.ui'
**
** Created by: Qt User Interface Compiler version 5.2.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_SCRIPT_WIDGET_H
#define UI_SCRIPT_WIDGET_H

#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_ScriptWidget
{
public:
    QFormLayout *formLayout;
    QLabel *sourceLabel;
    QWidget *sourceWidget;
    QHBoxLayout *horizontalLayout;
    QLineEdit *sourceEdit;
    QPushButton *browseSourceButton;

    void setupUi(QFrame *ScriptWidget)
    {
        if (ScriptWidget->objectName().isEmpty())
            ScriptWidget->setObjectName(QStringLiteral("ScriptWidget"));
        ScriptWidget->resize(272, 250);
        ScriptWidget->setFrameShape(QFrame::StyledPanel);
        ScriptWidget->setFrameShadow(QFrame::Raised);
        formLayout = new QFormLayout(ScriptWidget);
        formLayout->setObjectName(QStringLiteral("formLayout"));
        formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        formLayout->setContentsMargins(2, 2, 2, 2);
        sourceLabel = new QLabel(ScriptWidget);
        sourceLabel->setObjectName(QStringLiteral("sourceLabel"));

        formLayout->setWidget(0, QFormLayout::LabelRole, sourceLabel);

        sourceWidget = new QWidget(ScriptWidget);
        sourceWidget->setObjectName(QStringLiteral("sourceWidget"));
        horizontalLayout = new QHBoxLayout(sourceWidget);
        horizontalLayout->setObjectName(QStringLiteral("horizontalLayout"));
        horizontalLayout->setContentsMargins(0, 0, 0, 0);
        sourceEdit = new QLineEdit(sourceWidget);
        sourceEdit->setObjectName(QStringLiteral("sourceEdit"));

        horizontalLayout->addWidget(sourceEdit);

        browseSourceButton = new QPushButton(sourceWidget);
        browseSourceButton->setObjectName(QStringLiteral("browseSourceButton"));

        horizontalLayout->addWidget(browseSourceButton);


        formLayout->setWidget(0, QFormLayout::FieldRole, sourceWidget);


        retranslateUi(ScriptWidget);

        QMetaObject::connectSlotsByName(ScriptWidget);
    } // setupUi

    void retranslateUi(QFrame *ScriptWidget)
    {
        ScriptWidget->setWindowTitle(QApplication::translate("ScriptWidget", "Frame", 0));
        sourceLabel->setText(QApplication::translate("ScriptWidget", "Source", 0));
        browseSourceButton->setText(QApplication::translate("ScriptWidget", "...", 0));
    } // retranslateUi

};

namespace Ui {
    class ScriptWidget: public Ui_ScriptWidget {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_SCRIPT_WIDGET_H
