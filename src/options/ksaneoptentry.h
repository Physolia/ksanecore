/* ============================================================
 *
 * This file is part of the KDE project
 *
 * Date        : 2009-01-21
 * Description : Sane interface for KDE
 *
 * Copyright (C) 2009 by Kare Sars <kare dot sars at iki dot fi>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3, or any
 * later version accepted by the membership of KDE e.V. (or its
 * successor approved by the membership of KDE e.V.), which shall
 * act as a proxy defined in Section 6 of version 3 of the license.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * ============================================================ */

#ifndef KSANE_OPT_ENTRY_H
#define KSANE_OPT_ENTRY_H

#include "ksaneoption.h"

namespace KSaneIface
{

class LabeledEntry;

class KSaneOptEntry : public KSaneOption
{
    Q_OBJECT

public:
    KSaneOptEntry(const SANE_Handle handle, const int index);

    void createWidget(QWidget *parent) override;

    void readValue() override;

    bool getValue(float &val) override;
    bool setValue(float val) override;
    bool getValue(QString &val) override;
    bool setValue(const QString &val) override;
    bool hasGui() override;

private Q_SLOTS:
    void entryChanged(const QString &text);

private:
    LabeledEntry *m_entry;
    QString       m_string;
};

}  // NameSpace KSaneIface

#endif
