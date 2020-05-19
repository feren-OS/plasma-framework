/*
 *   Copyright 2013 Marco Martin <notmart@gmail.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License as
 *   published by the Free Software Foundation; either version 2, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef PACKAGEURLINTERCEPTOR_H
#define PACKAGEURLINTERCEPTOR_H

#include <QQmlAbstractUrlInterceptor>

#include <plasmaquick/plasmaquick_export.h>

#include <KPackage/Package>

//
//  W A R N I N G
//  -------------
//
// This file is not part of the public Plasma API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

class QQmlEngine;

namespace PlasmaQuick
{

class PackageUrlInterceptorPrivate;

//depends from https://codereview.qt-project.org/#change,65626
class PLASMAQUICK_EXPORT PackageUrlInterceptor: public QQmlAbstractUrlInterceptor
{
public:
    PackageUrlInterceptor(QQmlEngine *engine, const KPackage::Package &p);
    ~PackageUrlInterceptor() override;

    void addAllowedPath(const QString &path);
    void removeAllowedPath(const QString &path);
    QStringList allowedPaths() const;

    bool forcePlasmaStyle() const;
    void setForcePlasmaStyle(bool force);

    QUrl intercept(const QUrl &path, QQmlAbstractUrlInterceptor::DataType type) override;

    static inline QString prefixForType(QQmlAbstractUrlInterceptor::DataType type, const QString &fileName)
    {
        switch (type) {
        case QQmlAbstractUrlInterceptor::QmlFile:
            return QStringLiteral("ui");
        case QQmlAbstractUrlInterceptor::JavaScriptFile:
            return QStringLiteral("code");
        default:
            break;
        }

        //failed by type, let's try by extension
        const QString &extension = fileName.mid(fileName.lastIndexOf(QLatin1Char('.')) + 1).toLower();

        if (extension == QLatin1String("svg") || extension == QLatin1String("svgz")
            || extension == QLatin1String("png") || extension == QLatin1String("gif")
            || extension == QLatin1String("jpg") || extension == QLatin1String("jpeg")) {
            return QStringLiteral("images");
            //FIXME: are those necessary? are they *always* caught by type?
        } else if (extension == QLatin1String("js")) {
            return QStringLiteral("code");
        } else if (extension == QLatin1String("qml") || extension == QLatin1String("qmldir")) {
            return QStringLiteral("ui");
            //everything else, throw it in "data"
        } else {
            return QStringLiteral("data");
        }
    }

private:
    PackageUrlInterceptorPrivate *const d;
};

}

#endif
