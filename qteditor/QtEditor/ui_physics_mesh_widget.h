/********************************************************************************
** Form generated from reading UI file 'physics_mesh_widget.ui'
**
** Created by: Qt User Interface Compiler version 5.2.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_PHYSICS_MESH_WIDGET_H
#define UI_PHYSICS_MESH_WIDGET_H

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

class Ui_PhysicsMeshWidget
{
public:
    QFormLayout *formLayout;
    QLabel *label;
    QWidget *widget;
    QHBoxLayout *horizontalLayout;
    QLineEdit *sourceEdit;
    QPushButton *browseButton;

    void setupUi(QFrame *PhysicsMeshWidget)
    {
        if (PhysicsMeshWidget->objectName().isEmpty())
            PhysicsMeshWidget->setObjectName(QStringLiteral("PhysicsMeshWidget"));
        PhysicsMeshWidget->resize(364, 395);
        PhysicsMeshWidget->setFrameShape(QFrame::StyledPanel);
        PhysicsMeshWidget->setFrameShadow(QFrame::Raised);
        formLayout = new QFormLayout(PhysicsMeshWidget);
        formLayout->setObjectName(QStringLiteral("formLayout"));
        formLayout->setContentsMargins(2, 2, 2, 2);
        label = new QLabel(PhysicsMeshWidget);
        label->setObjectName(QStringLiteral("label"));

        formLayout->setWidget(0, QFormLayout::LabelRole, label);

        widget = new QWidget(PhysicsMeshWidget);
        widget->setObjectName(QStringLiteral("widget"));
        horizontalLayout = new QHBoxLayout(widget);
        horizontalLayout->setObjectName(QStringLiteral("horizontalLayout"));
        horizontalLayout->setContentsMargins(0, 0, 0, 0);
        sourceEdit = new QLineEdit(widget);
        sourceEdit->setObjectName(QStringLiteral("sourceEdit"));

        horizontalLayout->addWidget(sourceEdit);

        browseButton = new QPushButton(widget);
        browseButton->setObjectName(QStringLiteral("browseButton"));

        horizontalLayout->addWidget(browseButton);


        formLayout->setWidget(0, QFormLayout::FieldRole, widget);


        retranslateUi(PhysicsMeshWidget);

        QMetaObject::connectSlotsByName(PhysicsMeshWidget);
    } // setupUi

    void retranslateUi(QFrame *PhysicsMeshWidget)
    {
        PhysicsMeshWidget->setWindowTitle(QApplication::translate("PhysicsMeshWidget", "Frame", 0));
        label->setText(QApplication::translate("PhysicsMeshWidget", "Source", 0));
        browseButton->setText(QApplication::translate("PhysicsMeshWidget", "...", 0));
    } // retranslateUi

};

namespace Ui {
    class PhysicsMeshWidget: public Ui_PhysicsMeshWidget {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_PHYSICS_MESH_WIDGET_H
