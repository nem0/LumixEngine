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
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_GameView
{
public:
    QWidget *dockWidgetContents;
    QVBoxLayout *verticalLayout;
    QPushButton *playButton;
    QWidget *viewFrame;

    void setupUi(QDockWidget *GameView)
    {
        if (GameView->objectName().isEmpty())
            GameView->setObjectName(QStringLiteral("GameView"));
        GameView->resize(400, 300);
        dockWidgetContents = new QWidget();
        dockWidgetContents->setObjectName(QStringLiteral("dockWidgetContents"));
        verticalLayout = new QVBoxLayout(dockWidgetContents);
        verticalLayout->setObjectName(QStringLiteral("verticalLayout"));
        playButton = new QPushButton(dockWidgetContents);
        playButton->setObjectName(QStringLiteral("playButton"));

        verticalLayout->addWidget(playButton);

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
