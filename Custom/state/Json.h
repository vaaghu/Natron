#pragma once

#include <QByteArray>
#include <QString>
#include <QVariant>

// Minimal JSON reader/writer on top of QVariant, usable with Qt4 and Qt5
// (Qt4 has no QJsonDocument).
//
// Mapping:
//   object -> QVariantMap      array  -> QVariantList
//   string -> QString          true/false -> bool
//   number -> qlonglong if integral and in range, double otherwise
//   null   -> invalid QVariant
namespace Json
{
// Parses UTF-8 JSON text. On failure returns an invalid QVariant, sets *ok
// to false and, if error is given, a short description.
QVariant parse(const QByteArray &text, bool *ok, QString *error = 0);

// Serializes a QVariant (maps, lists, strings, numbers, bools, null)
// to compact UTF-8 JSON. Other types are written as strings.
QByteArray serialize(const QVariant &value);
}
