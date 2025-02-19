/*
 * SPDX-FileCopyrightText: 2009 Kare Sars <kare dot sars at iki dot fi>
 * SPDX-FileCopyrightText: 2014 Gregor Mitsch : port to KDE5 frameworks
 *
 * SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
 */

#include "listoption.h"

#include <QVarLengthArray>

#include <ksanecore_debug.h>

namespace KSaneCore
{

ListOption::ListOption(const SANE_Handle handle, const int index)
    : BaseOption(handle, index)
{
    m_optionType = Option::TypeValueList;
}

void ListOption::readValue()
{
    if (BaseOption::state() == Option::StateHidden) {
        return;
    }

    // read that current value
    QVarLengthArray<unsigned char> data(m_optDesc->size);
    SANE_Status status;
    SANE_Int res;
    status = sane_control_option(m_handle, m_index, SANE_ACTION_GET_VALUE, data.data(), &res);
    if (status != SANE_STATUS_GOOD) {
        return;
    }

    QVariant newValue;
    switch (m_optDesc->type) {
    case SANE_TYPE_INT:
        newValue = static_cast<int>(toSANE_Word(data.data()));
        break;
    case SANE_TYPE_FIXED:
        newValue = SANE_UNFIX(toSANE_Word(data.data()));
        break;
    case SANE_TYPE_STRING:
        newValue = sane_i18n(reinterpret_cast<char *>(data.data()));
        break;
    default:
        break;
    }

    if (newValue != m_currentValue) {
        m_currentValue = newValue;
        Q_EMIT valueChanged(m_currentValue);
    }
}

void ListOption::readOption()
{
    beginOptionReload();
    countEntries();
    endOptionReload();
}

QVariantList ListOption::valueList() const
{
    int i;
    QVariantList list;
    list.reserve(m_entriesCount);

    switch (m_optDesc->type) {
    case SANE_TYPE_INT:
        for (i = 1; i <= m_optDesc->constraint.word_list[0]; ++i) {
            list << static_cast<int>(m_optDesc->constraint.word_list[i]);;
        }
        break;
    case SANE_TYPE_FIXED:
        for (i = 1; i <= m_optDesc->constraint.word_list[0]; ++i) {
            list << SANE_UNFIX(m_optDesc->constraint.word_list[i]);
        }
        break;
    case SANE_TYPE_STRING:
        i = 0;
        while (m_optDesc->constraint.string_list[i] != nullptr) {
            list << sane_i18n(m_optDesc->constraint.string_list[i]);
            i++;
        }
        break;
    default :
        qCDebug(KSANECORE_LOG) << "can not handle type:" << m_optDesc->type;
        break;
    }
    return list;
}

QVariantList ListOption::internalValueList() const
{
    int i;
    QVariantList list;
    list.reserve(m_entriesCount);

    switch (m_optDesc->type) {
    case SANE_TYPE_INT:
        for (i = 1; i <= m_optDesc->constraint.word_list[0]; ++i) {
            list << static_cast<int>(m_optDesc->constraint.word_list[i]);;
        }
        break;
    case SANE_TYPE_FIXED:
        for (i = 1; i <= m_optDesc->constraint.word_list[0]; ++i) {
            list << SANE_UNFIX(m_optDesc->constraint.word_list[i]);
        }
        break;
    case SANE_TYPE_STRING:
        i = 0;
        while (m_optDesc->constraint.string_list[i] != nullptr) {
            list << QString::fromLatin1(m_optDesc->constraint.string_list[i]);
            i++;
        }
        break;
    default :
        qCDebug(KSANECORE_LOG) << "can not handle type:" << m_optDesc->type;
        break;
    }
    return list;
}

bool ListOption::setValue(const QVariant &value)
{
    bool success = false;
    if (value.userType() == QMetaType::QString) {
        success = setValue(value.toString());
    } else {
        success = setValue(value.toDouble());
    }

    return success;
}

QVariant ListOption::minimumValue() const
{
    QVariant value;
    if (BaseOption::state() == Option::StateHidden) {
        return value;
    }
    double dValueMin;
    int iValueMin;
    switch (m_optDesc->type) {
    case SANE_TYPE_INT:
        iValueMin = static_cast<int>(m_optDesc->constraint.word_list[1]);
        for (int i = 2; i <= m_optDesc->constraint.word_list[0]; i++) {
            iValueMin = qMin(static_cast<int>(m_optDesc->constraint.word_list[i]), iValueMin);
        }
        value = iValueMin;
        break;
    case SANE_TYPE_FIXED:
        dValueMin = SANE_UNFIX(m_optDesc->constraint.word_list[1]);
        for (int i = 2; i <= m_optDesc->constraint.word_list[0]; i++) {
            dValueMin = qMin(SANE_UNFIX(m_optDesc->constraint.word_list[i]), dValueMin);
        }
        value = dValueMin;
        break;
    default:
        qCDebug(KSANECORE_LOG) << "can not handle type:" << m_optDesc->type;
        return value;
    }
    return value;
}

QVariant ListOption::value() const
{
    if (BaseOption::state() == Option::StateHidden) {
        return QVariant();
    }
    return m_currentValue;
}

bool ListOption::setValue(double value)
{
    unsigned char data[4];
    double tmp;
    double minDiff;
    int i;
    int minIndex = 1;

    switch (m_optDesc->type) {
    case SANE_TYPE_INT:
        tmp = static_cast<double>(m_optDesc->constraint.word_list[minIndex]);
        minDiff = qAbs(value - tmp);
        for (i = 2; i <= m_optDesc->constraint.word_list[0]; ++i) {
            tmp = static_cast<double>(m_optDesc->constraint.word_list[i]);
            if (qAbs(value - tmp) < minDiff) {
                minDiff = qAbs(value - tmp);
                minIndex = i;
            }
        }
        fromSANE_Word(data, m_optDesc->constraint.word_list[minIndex]);
        writeData(data);
        readValue();
        return (minDiff < 1.0);
    case SANE_TYPE_FIXED:
        tmp = SANE_UNFIX(m_optDesc->constraint.word_list[minIndex]);
        minDiff = qAbs(value - tmp);
        for (i = 2; i <= m_optDesc->constraint.word_list[0]; ++i) {
            tmp = SANE_UNFIX(m_optDesc->constraint.word_list[i]);
            if (qAbs(value - tmp) < minDiff) {
                minDiff = qAbs(value - tmp);
                minIndex = i;
            }
        }
        fromSANE_Word(data, m_optDesc->constraint.word_list[minIndex]);
        writeData(data);
        readValue();
        return (minDiff < 1.0);
    default:
        qCDebug(KSANECORE_LOG) << "can not handle type:" << m_optDesc->type;
        break;
    }
    return false;
}

QString ListOption::valueAsString() const
{
    if (BaseOption::state() == Option::StateHidden) {
        return QString();
    }
    return m_currentValue.toString();
}

bool ListOption::setValue(const QString &value)
{
    if (BaseOption::state() == Option::StateHidden) {
        return false;
    }

    unsigned char data[4];
    void* data_ptr = nullptr;
    SANE_Word fixed;
    int i;
    double d;
    bool ok;
    QString tmp;

    switch (m_optDesc->type) {
    case SANE_TYPE_INT:
        i = value.toInt(&ok);
        if (ok) {
            fromSANE_Word(data, i);
            data_ptr = data;
        } else {
            return false;
        }

        break;
    case SANE_TYPE_FIXED:
        d = value.toDouble(&ok);
        if (ok) {
            fixed = SANE_FIX(d);
            fromSANE_Word(data, fixed);
            data_ptr = data;
        } else {
            return false;
        }

        break;
    case SANE_TYPE_STRING:
        i = 0;
        while (m_optDesc->constraint.string_list[i] != nullptr) {
            tmp = QString::fromLatin1(m_optDesc->constraint.string_list[i]);
            if (value != tmp) {
                tmp = sane_i18n(m_optDesc->constraint.string_list[i]);
            }
            if (value == tmp) {
                data_ptr = (void *)m_optDesc->constraint.string_list[i];
                break;
            }
            i++;
        }
        if (m_optDesc->constraint.string_list[i] == nullptr) {
            return false;
        }
        break;
    default:
        qCDebug(KSANECORE_LOG) << "can only handle SANE_TYPE: INT, FIXED and STRING";
        return false;
    }
    writeData(data_ptr);

    readValue();
    return true;
}

void ListOption::countEntries()
{
    m_entriesCount = 0;

    switch (m_optDesc->type) {

    case SANE_TYPE_INT:
    case SANE_TYPE_FIXED:
        m_entriesCount = m_optDesc->constraint.word_list[0];
        break;

    case SANE_TYPE_STRING:
        while (m_optDesc->constraint.string_list[m_entriesCount] != nullptr) {
            m_entriesCount++;
        }
        break;

    default :
        qCDebug(KSANECORE_LOG) << "can not handle type:" << m_optDesc->type;
        break;
    }
}

Option::OptionState ListOption::state() const
{
    if (m_entriesCount <= 1) {
        return Option::StateHidden;
    } else {
        return BaseOption::state();
    }
}

} // namespace KSaneCore

#include "moc_listoption.cpp"
