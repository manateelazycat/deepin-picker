/* -*- Mode: C++; indent-tabs-mode: nil; tab-width: 4 -*-
 * -*- coding: utf-8 -*-
 *
 * Copyright (C) 2011 ~ 2018 Deepin, Inc.
 *               2011 ~ 2018 Wang Yong
 *
 * Author:     Wang Yong <wangyong@deepin.com>
 * Maintainer: Wang Yong <wangyong@deepin.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "picker.h"
#include "animation.h"
#include "settings.h"
#include "utils.h"
#include "colormenu.h"
#include <QPainter>
#include <QBitmap>
#include <QPixmap>
#include <QMenu>
#include <QAction>
#include <QClipboard>
#include <QStyleFactory>
#include <QScreen>
#include <QApplication>
#include <QDesktopWidget>
#include <QDebug>

Picker::Picker(bool launchByDBus)
{
    // Init app id.
    isLaunchByDBus = launchByDBus;
    
    // Init window flags.
    setWindowFlags(Qt::X11BypassWindowManagerHint | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMouseTracking(true);
    installEventFilter(this);

    // Init attributes.
    blockHeight = 20;
    blockWidth = 20;
    displayCursorDot = false;
    height = 220;
    screenshotSize = 11;
    width = 220;
    windowHeight = 236;
    windowWidth = 236;

    shadowPixmap = new QPixmap(Utils::getQrcPath("shadow.png"));

    // Init update screenshot timer.
    updateScreenshotTimer = new QTimer(this);
    updateScreenshotTimer->setSingleShot(true);
    connect(updateScreenshotTimer, SIGNAL(timeout()), this, SLOT(updateScreenshot()));

    // Init window size and position.
    screenPixmap = QApplication::primaryScreen()->grabWindow(0);
    resize(screenPixmap.size());
    move(0, 0);
}

Picker::~Picker()
{
    delete animation;
    delete menu;
    delete shadowPixmap;
    delete updateScreenshotTimer;
}

void Picker::paintEvent(QPaintEvent *)
{
}

void Picker::handleMouseMove(int, int)
{
    // Update screenshot.
    if (updateScreenshotTimer->isActive()) {
        updateScreenshotTimer->stop();
    }
    updateScreenshotTimer->start(5);
}

void Picker::updateScreenshot()
{
    if (!displayCursorDot && isVisible()) {
        // Get cursor coordinate.
        cursorX = QCursor::pos().x();
        cursorY = QCursor::pos().y();

        // Need add offset to make drop shadow's position correctly.
        int offsetX = (windowWidth - width) / 2;
        int offsetY = (windowHeight - height) / 2;

        // Get image under cursor.
        qreal devicePixelRatio = qApp->devicePixelRatio();
        int size = screenshotSize / devicePixelRatio;
        screenshotPixmap = QApplication::primaryScreen()->grabWindow(
            0,
            cursorX - size / 2,
            cursorY - size / 2,
            size,
            size).scaled(width * devicePixelRatio, height * devicePixelRatio);

        // Clip screenshot pixmap to circle.
        // NOTE: need copy pixmap here, otherwise we will got bad circle.
        QPixmap cursorPixmap = *shadowPixmap;
        QPainter painter(&cursorPixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);

        painter.save();
        QPainterPath circlePath;
        circlePath.addEllipse(2 + offsetX, 2 + offsetY, width - 4, height - 4);
        painter.setClipPath(circlePath);
        painter.drawPixmap(1 + offsetX, 1 + offsetY, screenshotPixmap);
        painter.restore();

        // Draw circle bound.
        int outsidePenWidth = 1;
        QPen outsidePen("#000000");
        outsidePen.setWidth(outsidePenWidth);
        painter.setOpacity(0.05);
        painter.setPen(outsidePen);
        painter.drawEllipse(1 + offsetX, 1 + offsetY, width - 2, height - 2);

        int insidePenWidth = 4;
        QPen insidePen("#ffffff");
        insidePen.setWidth(insidePenWidth);
        painter.setOpacity(0.5);
        painter.setPen(insidePen);
        painter.drawEllipse(3 + offsetX, 3 + offsetY, width - 6, height - 6);

        // Draw focus block.
        painter.setOpacity(1);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setOpacity(0.2);
        painter.setPen("#000000");
        painter.drawRect(QRect(width / 2 - blockWidth / 2 + offsetX, height / 2 - blockHeight / 2 + offsetY, blockWidth, blockHeight));
        painter.setOpacity(1);
        painter.setPen("#ffffff");
        painter.drawRect(QRect(width / 2 - blockWidth / 2 + 1 + offsetX, height / 2 - blockHeight / 2 + 1 + offsetY, blockWidth - 2, blockHeight - 2));

        // Set screenshot as cursor.
        QApplication::setOverrideCursor(QCursor(cursorPixmap));
    }
}

void Picker::handleLeftButtonPress(int x, int y)
{
    if (!displayCursorDot && isVisible()) {
        // Rest cursor and hide window.
        // NOTE: Don't call hide() at here, let process die,
        // Otherwise mouse event will pass to application window under picker.
        QApplication::setOverrideCursor(Qt::ArrowCursor);

        // Rest color type to hex if config file not exist.
        Settings *settings = new Settings();

        // Emit copyColor signal to copy color to system clipboard.
        cursorColor = getColorAtCursor(x, y);
        copyColor(cursorColor, settings->getOption("color_type", "HEX").toString());
        
        // Send colorPicked signal when call by DBus and no empty appid.
        if (isLaunchByDBus && appid != "") {
            colorPicked(appid, Utils::colorToHex(cursorColor));
        }
    }
}

void Picker::handleRightButtonRelease(int x, int y)
{
    if (!displayCursorDot && isVisible()) {
        // Set displayCursorDot flag when click right button.
        displayCursorDot = true;

        cursorColor = getColorAtCursor(x, y);

        // Popup color menu window.
        qreal devicePixelRatio = qApp->devicePixelRatio();
        menu = new ColorMenu(
            x / devicePixelRatio - blockWidth / 2,
            y / devicePixelRatio - blockHeight / 2,
            blockWidth,
            cursorColor);
        connect(menu, &ColorMenu::copyColor, this, &Picker::copyColor, Qt::QueuedConnection);
        connect(menu, &ColorMenu::exit, this, &Picker::exit, Qt::QueuedConnection);
        menu->show();
        menu->setFocus();       // set focus to monitor 'aboutToHide' signal of color menu

        // Display animation before poup color menu.
        animation = new Animation(x / devicePixelRatio, y / devicePixelRatio, screenshotPixmap, cursorColor);
        connect(animation, &Animation::finish, this, &Picker::popupColorMenu, Qt::QueuedConnection);

        // Rest cursor to default cursor.
        QApplication::setOverrideCursor(Qt::ArrowCursor);

        // Show animation after rest cursor to avoid flash screen.
        animation->show();
    }
}

QColor Picker::getColorAtCursor(int x, int y)
{
    screenPixmap = QApplication::primaryScreen()->grabWindow(0);
    return QColor(screenPixmap.copy(x, y, 1, 1).toImage().pixel(0, 0));
}

void Picker::popupColorMenu()
{
    // Hide picker main window and popup color menu.
    // NOTE: Don't call hide() at here, let process die,
    // Otherwise mouse event will pass to application window under picker.
    menu->showMenu();
}

void Picker::StartPick(QString id)
{
    // Update app id.
    appid = id;
    
    // Show window.
    show();

    // Update screenshot when start.
    updateScreenshot();
}
