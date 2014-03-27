/********************************************************************************
** Form generated from reading UI file 'property_view.ui'
**
** Created by: Qt User Interface Compiler version 5.2.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_PROPERTY_VIEW_H
#define UI_PROPERTY_VIEW_H

#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDockWidget>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QToolBox>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_PropertyView
{
public:
    QWidget *dockWidgetContents;
    QVBoxLayout *verticalLayout;
    QToolBox *components;
    QWidget *componentA;
    QWidget *componentB;
    QWidget *widget;
    QHBoxLayout *horizontalLayout;
    QComboBox *componentTypeCombo;
    QPushButton *addComponentButton;

    void setupUi(QDockWidget *PropertyView)
    {
        if (PropertyView->objectName().isEmpty())
            PropertyView->setObjectName(QStringLiteral("PropertyView"));
        PropertyView->resize(251, 446);
        dockWidgetContents = new QWidget();
        dockWidgetContents->setObjectName(QStringLiteral("dockWidgetContents"));
        verticalLayout = new QVBoxLayout(dockWidgetContents);
        verticalLayout->setObjectName(QStringLiteral("verticalLayout"));
        components = new QToolBox(dockWidgetContents);
        components->setObjectName(QStringLiteral("components"));
        componentA = new QWidget();
        componentA->setObjectName(QStringLiteral("componentA"));
        componentA->setGeometry(QRect(0, 0, 98, 28));
        components->addItem(componentA, QStringLiteral("Page 1"));
        componentB = new QWidget();
        componentB->setObjectName(QStringLiteral("componentB"));
        componentB->setGeometry(QRect(0, 0, 233, 315));
        components->addItem(componentB, QStringLiteral("Page 2"));

        verticalLayout->addWidget(components);

        widget = new QWidget(dockWidgetContents);
        widget->setObjectName(QStringLiteral("widget"));
        horizontalLayout = new QHBoxLayout(widget);
        horizontalLayout->setObjectName(QStringLiteral("horizontalLayout"));
        componentTypeCombo = new QComboBox(widget);
        componentTypeCombo->setObjectName(QStringLiteral("componentTypeCombo"));
        QSizePolicy sizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        sizePolicy.setHorizontalStretch(2);
        sizePolicy.setVerticalStretch(2);
        sizePolicy.setHeightForWidth(componentTypeCombo->sizePolicy().hasHeightForWidth());
        componentTypeCombo->setSizePolicy(sizePolicy);

        horizontalLayout->addWidget(componentTypeCombo);

        addComponentButton = new QPushButton(widget);
        addComponentButton->setObjectName(QStringLiteral("addComponentButton"));
        QSizePolicy sizePolicy1(QSizePolicy::Minimum, QSizePolicy::Fixed);
        sizePolicy1.setHorizontalStretch(1);
        sizePolicy1.setVerticalStretch(1);
        sizePolicy1.setHeightForWidth(addComponentButton->sizePolicy().hasHeightForWidth());
        addComponentButton->setSizePolicy(sizePolicy1);

        horizontalLayout->addWidget(addComponentButton);


        verticalLayout->addWidget(widget);

        PropertyView->setWidget(dockWidgetContents);

        retranslateUi(PropertyView);

        components->setCurrentIndex(1);
        components->layout()->setSpacing(1);


        QMetaObject::connectSlotsByName(PropertyView);
    } // setupUi

    void retranslateUi(QDockWidget *PropertyView)
    {
        PropertyView->setWindowTitle(QApplication::translate("PropertyView", "Properties", 0));
        components->setItemText(components->indexOf(componentA), QApplication::translate("PropertyView", "Page 1", 0));
        components->setItemText(components->indexOf(componentB), QApplication::translate("PropertyView", "Page 2", 0));
        componentTypeCombo->clear();
        componentTypeCombo->insertItems(0, QStringList()
         << QApplication::translate("PropertyView", "Renderable", 0)
         << QApplication::translate("PropertyView", "Animable", 0)
         << QApplication::translate("PropertyView", "Point Light", 0)
         << QApplication::translate("PropertyView", "Script", 0)
        );
        addComponentButton->setText(QApplication::translate("PropertyView", "Add", 0));
    } // retranslateUi

};

namespace Ui {
    class PropertyView: public Ui_PropertyView {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_PROPERTY_VIEW_H
