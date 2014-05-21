/********************************************************************************
** Form generated from reading UI file 'animable_widget.ui'
**
** Created by: Qt User Interface Compiler version 5.2.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_ANIMABLE_WIDGET_H
#define UI_ANIMABLE_WIDGET_H

#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHeaderView>

QT_BEGIN_NAMESPACE

class Ui_AnimableWidget
{
public:

    void setupUi(QFrame *AnimableWidget)
    {
        if (AnimableWidget->objectName().isEmpty())
            AnimableWidget->setObjectName(QStringLiteral("AnimableWidget"));
        AnimableWidget->resize(255, 299);
        AnimableWidget->setFrameShape(QFrame::StyledPanel);
        AnimableWidget->setFrameShadow(QFrame::Raised);

        retranslateUi(AnimableWidget);

        QMetaObject::connectSlotsByName(AnimableWidget);
    } // setupUi

    void retranslateUi(QFrame *AnimableWidget)
    {
        AnimableWidget->setWindowTitle(QApplication::translate("AnimableWidget", "Frame", 0));
    } // retranslateUi

};

namespace Ui {
    class AnimableWidget: public Ui_AnimableWidget {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_ANIMABLE_WIDGET_H
