/********************************************************************************
** Form generated from reading UI file 'property_widget_base.ui'
**
** Created by: Qt User Interface Compiler version 5.2.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_PROPERTY_WIDGET_BASE_H
#define UI_PROPERTY_WIDGET_BASE_H

#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHeaderView>

QT_BEGIN_NAMESPACE

class Ui_PropertyWidgetBase
{
public:

    void setupUi(QFrame *PropertyWidgetBase)
    {
        if (PropertyWidgetBase->objectName().isEmpty())
            PropertyWidgetBase->setObjectName(QStringLiteral("PropertyWidgetBase"));
        PropertyWidgetBase->resize(245, 324);
        PropertyWidgetBase->setFrameShape(QFrame::StyledPanel);
        PropertyWidgetBase->setFrameShadow(QFrame::Raised);

        retranslateUi(PropertyWidgetBase);

        QMetaObject::connectSlotsByName(PropertyWidgetBase);
    } // setupUi

    void retranslateUi(QFrame *PropertyWidgetBase)
    {
        PropertyWidgetBase->setWindowTitle(QApplication::translate("PropertyWidgetBase", "Frame", 0));
    } // retranslateUi

};

namespace Ui {
    class PropertyWidgetBase: public Ui_PropertyWidgetBase {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_PROPERTY_WIDGET_BASE_H
