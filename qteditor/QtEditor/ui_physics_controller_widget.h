/********************************************************************************
** Form generated from reading UI file 'physics_controller_widget.ui'
**
** Created by: Qt User Interface Compiler version 5.2.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_PHYSICS_CONTROLLER_WIDGET_H
#define UI_PHYSICS_CONTROLLER_WIDGET_H

#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHeaderView>

QT_BEGIN_NAMESPACE

class Ui_PhysicsControllerWidget
{
public:

    void setupUi(QFrame *PhysicsControllerWidget)
    {
        if (PhysicsControllerWidget->objectName().isEmpty())
            PhysicsControllerWidget->setObjectName(QStringLiteral("PhysicsControllerWidget"));
        PhysicsControllerWidget->setFrameShadow(QFrame::Raised);
        PhysicsControllerWidget->resize(400, 300);
        PhysicsControllerWidget->setFrameShape(QFrame::StyledPanel);

        retranslateUi(PhysicsControllerWidget);

        QMetaObject::connectSlotsByName(PhysicsControllerWidget);
    } // setupUi

    void retranslateUi(QFrame *PhysicsControllerWidget)
    {
        PhysicsControllerWidget->setWindowTitle(QApplication::translate("PhysicsControllerWidget", "Frame", 0));
    } // retranslateUi

};

namespace Ui {
    class PhysicsControllerWidget: public Ui_PhysicsControllerWidget {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_PHYSICS_CONTROLLER_WIDGET_H
