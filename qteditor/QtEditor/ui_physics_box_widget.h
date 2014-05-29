/********************************************************************************
** Form generated from reading UI file 'physics_box_widget.ui'
**
** Created by: Qt User Interface Compiler version 5.2.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_PHYSICS_BOX_WIDGET_H
#define UI_PHYSICS_BOX_WIDGET_H

#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>

QT_BEGIN_NAMESPACE

class Ui_PhysicsBoxWidget
{
public:
    QFormLayout *formLayout;
    QLabel *label;
    QCheckBox *isDynamicCheckBox;
    QLabel *label_2;
    QLabel *label_3;
    QDoubleSpinBox *ySizeInput;
    QLabel *label_4;
    QDoubleSpinBox *zSizeInput;
    QDoubleSpinBox *xSizeInput;

    void setupUi(QFrame *PhysicsBoxWidget)
    {
        if (PhysicsBoxWidget->objectName().isEmpty())
            PhysicsBoxWidget->setObjectName(QStringLiteral("PhysicsBoxWidget"));
        PhysicsBoxWidget->resize(397, 350);
        PhysicsBoxWidget->setFrameShape(QFrame::StyledPanel);
        PhysicsBoxWidget->setFrameShadow(QFrame::Raised);
        formLayout = new QFormLayout(PhysicsBoxWidget);
        formLayout->setObjectName(QStringLiteral("formLayout"));
        formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        label = new QLabel(PhysicsBoxWidget);
        label->setObjectName(QStringLiteral("label"));

        formLayout->setWidget(0, QFormLayout::LabelRole, label);

        isDynamicCheckBox = new QCheckBox(PhysicsBoxWidget);
        isDynamicCheckBox->setObjectName(QStringLiteral("isDynamicCheckBox"));

        formLayout->setWidget(0, QFormLayout::FieldRole, isDynamicCheckBox);

        label_2 = new QLabel(PhysicsBoxWidget);
        label_2->setObjectName(QStringLiteral("label_2"));

        formLayout->setWidget(1, QFormLayout::LabelRole, label_2);

        label_3 = new QLabel(PhysicsBoxWidget);
        label_3->setObjectName(QStringLiteral("label_3"));

        formLayout->setWidget(2, QFormLayout::LabelRole, label_3);

        ySizeInput = new QDoubleSpinBox(PhysicsBoxWidget);
        ySizeInput->setObjectName(QStringLiteral("ySizeInput"));

        formLayout->setWidget(2, QFormLayout::FieldRole, ySizeInput);

        label_4 = new QLabel(PhysicsBoxWidget);
        label_4->setObjectName(QStringLiteral("label_4"));

        formLayout->setWidget(3, QFormLayout::LabelRole, label_4);

        zSizeInput = new QDoubleSpinBox(PhysicsBoxWidget);
        zSizeInput->setObjectName(QStringLiteral("zSizeInput"));

        formLayout->setWidget(3, QFormLayout::FieldRole, zSizeInput);

        xSizeInput = new QDoubleSpinBox(PhysicsBoxWidget);
        xSizeInput->setObjectName(QStringLiteral("xSizeInput"));

        formLayout->setWidget(1, QFormLayout::FieldRole, xSizeInput);


        retranslateUi(PhysicsBoxWidget);

        QMetaObject::connectSlotsByName(PhysicsBoxWidget);
    } // setupUi

    void retranslateUi(QFrame *PhysicsBoxWidget)
    {
        PhysicsBoxWidget->setWindowTitle(QApplication::translate("PhysicsBoxWidget", "Frame", 0));
        label->setText(QApplication::translate("PhysicsBoxWidget", "Dynamic", 0));
        isDynamicCheckBox->setText(QString());
        label_2->setText(QApplication::translate("PhysicsBoxWidget", "X", 0));
        label_3->setText(QApplication::translate("PhysicsBoxWidget", "Y", 0));
        label_4->setText(QApplication::translate("PhysicsBoxWidget", "Z", 0));
    } // retranslateUi

};

namespace Ui {
    class PhysicsBoxWidget: public Ui_PhysicsBoxWidget {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_PHYSICS_BOX_WIDGET_H
