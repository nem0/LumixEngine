/********************************************************************************
** Form generated from reading UI file 'camerawidget.ui'
**
** Created by: Qt User Interface Compiler version 5.2.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_CAMERAWIDGET_H
#define UI_CAMERAWIDGET_H

#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSpinBox>

QT_BEGIN_NAMESPACE

class Ui_CameraWidget
{
public:
    QFormLayout *formLayout;
    QLabel *label;
    QSpinBox *priorityInput;

    void setupUi(QFrame *CameraWidget)
    {
        if (CameraWidget->objectName().isEmpty())
            CameraWidget->setObjectName(QStringLiteral("CameraWidget"));
        CameraWidget->resize(346, 383);
        CameraWidget->setFrameShape(QFrame::StyledPanel);
        CameraWidget->setFrameShadow(QFrame::Raised);
        formLayout = new QFormLayout(CameraWidget);
        formLayout->setObjectName(QStringLiteral("formLayout"));
        label = new QLabel(CameraWidget);
        label->setObjectName(QStringLiteral("label"));

        formLayout->setWidget(0, QFormLayout::LabelRole, label);

        priorityInput = new QSpinBox(CameraWidget);
        priorityInput->setObjectName(QStringLiteral("priorityInput"));
        priorityInput->setMinimum(-1);
        priorityInput->setMaximum(999999999);
        priorityInput->setValue(-1);

        formLayout->setWidget(0, QFormLayout::FieldRole, priorityInput);


        retranslateUi(CameraWidget);

        QMetaObject::connectSlotsByName(CameraWidget);
    } // setupUi

    void retranslateUi(QFrame *CameraWidget)
    {
        CameraWidget->setWindowTitle(QApplication::translate("CameraWidget", "Frame", 0));
        label->setText(QApplication::translate("CameraWidget", "Priority", 0));
    } // retranslateUi

};

namespace Ui {
    class CameraWidget: public Ui_CameraWidget {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_CAMERAWIDGET_H
