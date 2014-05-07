/********************************************************************************
** Form generated from reading UI file 'lightwidget.ui'
**
** Created by: Qt User Interface Compiler version 5.2.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_LIGHTWIDGET_H
#define UI_LIGHTWIDGET_H

#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHeaderView>

QT_BEGIN_NAMESPACE

class Ui_LightWidget
{
public:

    void setupUi(QFrame *LightWidget)
    {
        if (LightWidget->objectName().isEmpty())
            LightWidget->setObjectName(QStringLiteral("LightWidget"));
        LightWidget->resize(352, 481);
        LightWidget->setFrameShape(QFrame::StyledPanel);
        LightWidget->setFrameShadow(QFrame::Raised);

        retranslateUi(LightWidget);

        QMetaObject::connectSlotsByName(LightWidget);
    } // setupUi

    void retranslateUi(QFrame *LightWidget)
    {
        LightWidget->setWindowTitle(QApplication::translate("LightWidget", "Frame", 0));
    } // retranslateUi

};

namespace Ui {
    class LightWidget: public Ui_LightWidget {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_LIGHTWIDGET_H
