/********************************************************************************
** Form generated from reading UI file 'gameview.ui'
**
** Created by: Qt User Interface Compiler version 5.2.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_GAMEVIEW_H
#define UI_GAMEVIEW_H

#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QDockWidget>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_GameView
{
public:
    QWidget *dockWidgetContents;
    QVBoxLayout *verticalLayout;
    QWidget *widget;
    QHBoxLayout *horizontalLayout;
    QSpacerItem *horizontalSpacer;
    QPushButton *playButton;
    QSpacerItem *horizontalSpacer_2;
    QWidget *viewFrame;

    void setupUi(QDockWidget *GameView)
    {
        if (GameView->objectName().isEmpty())
            GameView->setObjectName(QStringLiteral("GameView"));
        GameView->resize(400, 300);
        dockWidgetContents = new QWidget();
        dockWidgetContents->setObjectName(QStringLiteral("dockWidgetContents"));
        verticalLayout = new QVBoxLayout(dockWidgetContents);
        verticalLayout->setSpacing(0);
        verticalLayout->setObjectName(QStringLiteral("verticalLayout"));
        verticalLayout->setContentsMargins(0, 0, 0, 0);
        widget = new QWidget(dockWidgetContents);
        widget->setObjectName(QStringLiteral("widget"));
        QSizePolicy sizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(widget->sizePolicy().hasHeightForWidth());
        widget->setSizePolicy(sizePolicy);
        horizontalLayout = new QHBoxLayout(widget);
        horizontalLayout->setSpacing(0);
        horizontalLayout->setObjectName(QStringLiteral("horizontalLayout"));
        horizontalLayout->setContentsMargins(0, 0, 0, 0);
        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout->addItem(horizontalSpacer);

        playButton = new QPushButton(widget);
        playButton->setObjectName(QStringLiteral("playButton"));

        horizontalLayout->addWidget(playButton);

        horizontalSpacer_2 = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout->addItem(horizontalSpacer_2);


        verticalLayout->addWidget(widget);

        viewFrame = new QWidget(dockWidgetContents);
        viewFrame->setObjectName(QStringLiteral("viewFrame"));

        verticalLayout->addWidget(viewFrame);

        GameView->setWidget(dockWidgetContents);

        retranslateUi(GameView);

        QMetaObject::connectSlotsByName(GameView);
    } // setupUi

    void retranslateUi(QDockWidget *GameView)
    {
        GameView->setWindowTitle(QApplication::translate("GameView", "Game View", 0));
        playButton->setText(QApplication::translate("GameView", "Play", 0));
    } // retranslateUi

};

namespace Ui {
    class GameView: public Ui_GameView {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_GAMEVIEW_H
