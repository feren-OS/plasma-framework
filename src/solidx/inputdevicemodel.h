/*
 *   Copyright (C) 2013 Ivan Cukic <ivan.cukic(at)kde.org>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License version 2,
 *   or (at your option) any later version, as published by the Free
 *   Software Foundation
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details
 *
 *   You should have received a copy of the GNU General Public
 *   License along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef SOLIDX_INPUTDEVICEMODEL_H
#define SOLIDX_INPUTDEVICEMODEL_H

#include <QAbstractListModel>
#include <QString>

#include "utils/d_ptr.h"
#include "inputdevice.h"

namespace solidx {

/**
 * InputDevices
 */
class InputDeviceModel: public QAbstractListModel {
public:
    explicit InputDeviceModel(
            InputDevice::Type type = InputDevice::Type::Any,
            InputDevice::Subtype subtype = InputDevice::Subtype::Any,
            QObject * parent = Q_NULLPTR
        );
    explicit InputDeviceModel(QObject * parent);
    virtual ~InputDeviceModel();

    int rowCount(const QModelIndex & parent = QModelIndex()) const;
    QModelIndex sibling(int row, int column, const QModelIndex & index) const;

    QVariant data(const QModelIndex & index, int role) const;

private:
    D_PTR;
};

} // namespace solidx

#endif /* SOLIDX_INPUTDEVICEMODEL_H */

