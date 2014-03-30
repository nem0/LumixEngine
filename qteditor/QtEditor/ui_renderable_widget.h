/********************************************************************************
** Form generated from reading UI file 'renderable_widget.ui'
**
** Created by: Qt User Interface Compiler version 5.2.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_RENDERABLE_WIDGET_H
#define UI_RENDERABLE_WIDGET_H

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

class Ui_RenderableWidget
{
public:
    QFormLayout *formLayout;
    QLabel *label;
    QWidget *source;
    QHBoxLayout *horizontalLayout;
    QLineEdit *sourceEdit;
    QPushButton *browseSource;

    void setupUi(QFrame *RenderableWidget)
    {
        if (RenderableWidget->objectName().isEmpty())
            RenderableWidget->setObjectName(QStringLiteral("RenderableWidget"));
        RenderableWidget->resize(311, 300);
        RenderableWidget->setFrameShape(QFrame::StyledPanel);
        RenderableWidget->setFrameShadow(QFrame::Raised);
        formLayout = new QFormLayout(RenderableWidget);
        formLayout->setObjectName(QStringLiteral("formLayout"));
        formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        label = new QLabel(RenderableWidget);
        label->setObjectName(QStringLiteral("label"));

        formLayout->setWidget(1, QFormLayout::LabelRole, label);

        source = new QWidget(RenderableWidget);
        source->setObjectName(QStringLiteral("source"));
        horizontalLayout = new QHBoxLayout(source);
        horizontalLayout->setSpacing(6);
        horizontalLayout->setObjectName(QStringLiteral("horizontalLayout"));
        horizontalLayout->setContentsMargins(0, 0, 0, 0);
        sourceEdit = new QLineEdit(source);
        sourceEdit->setObjectName(QStringLiteral("sourceEdit"));

        horizontalLayout->addWidget(sourceEdit);

        browseSource = new QPushButton(source);
        browseSource->setObjectName(QStringLiteral("browseSource"));

        horizontalLayout->addWidget(browseSource);


        formLayout->setWidget(1, QFormLayout::FieldRole, source);


        retranslateUi(RenderableWidget);

        QMetaObject::connectSlotsByName(RenderableWidget);
    } // setupUi

    void retranslateUi(QFrame *RenderableWidget)
    {
        RenderableWidget->setWindowTitle(QApplication::translate("RenderableWidget", "Frame", 0));
        label->setText(QApplication::translate("RenderableWidget", "Source", 0));
        browseSource->setText(QApplication::translate("RenderableWidget", "...", 0));
    } // retranslateUi

};

namespace Ui {
    class RenderableWidget: public Ui_RenderableWidget {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_RENDERABLE_WIDGET_H
