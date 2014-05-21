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
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>

QT_BEGIN_NAMESPACE

class Ui_CameraWidget
{
public:
    QFormLayout *formLayout;
    QLabel *label;
    QLabel *label_2;
    QLabel *label_3;
    QLabel *label_4;
    QDoubleSpinBox *nearInput;
    QDoubleSpinBox *fovInput;
    QDoubleSpinBox *farInput;
    QLineEdit *slotEdit;

    void setupUi(QFrame *CameraWidget)
    {
        if (CameraWidget->objectName().isEmpty())
            CameraWidget->setObjectName(QStringLiteral("CameraWidget"));
        CameraWidget->resize(346, 383);
        CameraWidget->setFrameShape(QFrame::StyledPanel);
        CameraWidget->setFrameShadow(QFrame::Raised);
        formLayout = new QFormLayout(CameraWidget);
        formLayout->setObjectName(QStringLiteral("formLayout"));
        formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        label = new QLabel(CameraWidget);
        label->setObjectName(QStringLiteral("label"));

        formLayout->setWidget(0, QFormLayout::LabelRole, label);

        label_2 = new QLabel(CameraWidget);
        label_2->setObjectName(QStringLiteral("label_2"));

        formLayout->setWidget(1, QFormLayout::LabelRole, label_2);

        label_3 = new QLabel(CameraWidget);
        label_3->setObjectName(QStringLiteral("label_3"));

        formLayout->setWidget(2, QFormLayout::LabelRole, label_3);

        label_4 = new QLabel(CameraWidget);
        label_4->setObjectName(QStringLiteral("label_4"));

        formLayout->setWidget(3, QFormLayout::LabelRole, label_4);

        nearInput = new QDoubleSpinBox(CameraWidget);
        nearInput->setObjectName(QStringLiteral("nearInput"));
        nearInput->setMinimum(-1e+09);
        nearInput->setMaximum(1e+09);

        formLayout->setWidget(1, QFormLayout::FieldRole, nearInput);

        fovInput = new QDoubleSpinBox(CameraWidget);
        fovInput->setObjectName(QStringLiteral("fovInput"));

        formLayout->setWidget(3, QFormLayout::FieldRole, fovInput);

        farInput = new QDoubleSpinBox(CameraWidget);
        farInput->setObjectName(QStringLiteral("farInput"));
        farInput->setMinimum(-1e+09);
        farInput->setMaximum(1e+09);

        formLayout->setWidget(2, QFormLayout::FieldRole, farInput);

        slotEdit = new QLineEdit(CameraWidget);
        slotEdit->setObjectName(QStringLiteral("slotEdit"));

        formLayout->setWidget(0, QFormLayout::FieldRole, slotEdit);


        retranslateUi(CameraWidget);

        QMetaObject::connectSlotsByName(CameraWidget);
    } // setupUi

    void retranslateUi(QFrame *CameraWidget)
    {
        CameraWidget->setWindowTitle(QApplication::translate("CameraWidget", "Frame", 0));
        label->setText(QApplication::translate("CameraWidget", "Slot", 0));
        label_2->setText(QApplication::translate("CameraWidget", "Near", 0));
        label_3->setText(QApplication::translate("CameraWidget", "Far", 0));
        label_4->setText(QApplication::translate("CameraWidget", "Field of view", 0));
    } // retranslateUi

};

namespace Ui {
    class CameraWidget: public Ui_CameraWidget {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_CAMERAWIDGET_H
