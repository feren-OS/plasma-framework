/********************************************************************************
*   Copyright 2014 Marco Martin <mart@kde.org>                                  *
*                                                                               *
*   This library is free software; you can redistribute it and/or               *
*   modify it under the terms of the GNU Library General Public                 *
*   License as published by the Free Software Foundation; either                *
*   version 2 of the License, or (at your option) any later version.            *
*                                                                               *
*   This library is distributed in the hope that it will be useful,             *
*   but WITHOUT ANY WARRANTY; without even the implied warranty of              *
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU            *
*   Library General Public License for more details.                            *
*                                                                               *
*   You should have received a copy of the GNU Library General Public License   *
*   along with this library; see the file COPYING.LIB.  If not, write to        *
*   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,        *
*   Boston, MA 02110-1301, USA.                                                 *
*********************************************************************************/

#include "dialognativetest.h"
#include <KWindowSystem>


void DialogNativeTest::initTestCase()
{
    QStandardPaths::enableTestMode(true);
    m_cacheDir = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
    m_cacheDir.removeRecursively();

    m_dialog = new PlasmaQuick::Dialog;
    m_dialog->setLocation(Plasma::Types::TopEdge);

    m_panel = new QQuickView;
    m_panel->setGeometry(0, 0, 50, 50);
    m_panel->setFlags(Qt::FramelessWindowHint|Qt::WindowDoesNotAcceptFocus);

    m_panel2 = new QQuickView;
    m_panel2->setGeometry(100, 0, 50, 50);
    m_panel2->setFlags(Qt::FramelessWindowHint|Qt::WindowDoesNotAcceptFocus);

    m_content = new QQuickItem;
    m_content->setWidth(100);
    m_content->setHeight(100);
    m_dialog->setMainItem(m_content);

    m_panel->show();
    m_panel2->show();
    KWindowSystem::setType(m_panel->winId(), NET::Dock);
    m_dialog->setVisualParent(m_panel->contentItem());
    m_dialog->show();
}

void DialogNativeTest::cleanupTestCase()
{
    delete m_dialog;
    delete m_panel;
    delete m_panel2;

    m_cacheDir.removeRecursively();
}

void DialogNativeTest::size()
{
    QTest::qWaitForWindowExposed(m_dialog);

    QCOMPARE(m_content->width(), (qreal)100);
    QCOMPARE(m_content->height(), (qreal)100);
    QCOMPARE(m_dialog->width(), 112);
    QCOMPARE(m_dialog->height(), 112);

    QCOMPARE(m_dialog->margins()->property("left").value<qreal>(), (qreal)6.0);
    QCOMPARE(m_dialog->margins()->property("top").value<qreal>(), (qreal)6.0);
    QCOMPARE(m_dialog->margins()->property("right").value<qreal>(), (qreal)6.0);
    QCOMPARE(m_dialog->margins()->property("bottom").value<qreal>(), (qreal)6.0);
}

void DialogNativeTest::position()
{
    QTest::qWaitForWindowExposed(m_dialog);

    QCOMPARE(m_dialog->x(), 0);
    QCOMPARE(m_dialog->y(), 49);

#if QT_VERSION >= QT_VERSION_CHECK(5, 3, 0)
    m_dialog->setVisualParent(m_panel2->contentItem());
    QCOMPARE(m_dialog->x(), 69);
    QCOMPARE(m_dialog->y(), 49);
#endif
}

QTEST_MAIN(DialogNativeTest)
