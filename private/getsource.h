/*
 *   Copyright © 2009 Rob Scheepmaker <r.scheepmaker@student.utwente.nl>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License version 2 as
 *   published by the Free Software Foundation
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

#ifndef GETSOURCE_H
#define GETSOURCE_H

#include "dataengineservice.h"

#include "../dataengine.h"
#include "../servicejob.h"

namespace Plasma
{

class DataEngine;

class GetSource : public Plasma::ServiceJob
{
    Q_OBJECT

    public:
        GetSource(DataEngine *engine,
                  const QString& operation,
                  QMap<QString,QVariant>& parameters,
                  DataEngineService *service = 0)
        : ServiceJob(QString("publickey"), operation, parameters, service),
          m_engine(engine),
          m_service(service)
        {
        }

        void start();

    private:
        DataEngine          *m_engine;
        DataEngineService   *m_service;
};

}

#endif //JOBVIEW_H
