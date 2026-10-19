#include "ScoreboardCommon.h"

#include "../state/KvValue.h"
#include "../state/StateStore.h"

namespace Scoreboard
{
QString prefix()
{
  return QString::fromUtf8("scoreboard.");
}

QString periodText(int period)
{
  if (period <= 0)
  {
    return QString();
  }
  if (period <= 4)
  {
    return QString::fromUtf8("Q") + QString::number(period);
  }
  if (period == 5)
  {
    return QString::fromUtf8("OT");
  }
  return QString::number(period - 4) + QString::fromUtf8("OT");
}

QString twoDigits(int n)
{
  return QString::number(n).rightJustified(2, QLatin1Char('0'));
}

QString clockText(int minutes, int seconds, int tenths)
{
  if (minutes > 0)
  {
    return twoDigits(minutes) + QString::fromUtf8(":") + twoDigits(seconds);
  }
  return twoDigits(seconds) + QString::fromUtf8(".") + QString::number(tenths);
}

QString shotClockText(int seconds, int tenths)
{
  if (seconds * 10 + tenths > 50)
  {
    return QString::number(seconds);
  }
  return QString::number(seconds) + QString::fromUtf8(".") + QString::number(tenths);
}

QStringList applyChanged(StateStore *store, const QVariantMap &values)
{
  QStringList changed;
  for (QVariantMap::const_iterator it = values.constBegin(); it != values.constEnd(); ++it)
  {
    // Compared as shown: saved numbers come back as integers, not doubles.
    const QVariant current = store->get(it.key());
    if (current.isValid() && Kv::typeOf(current) == Kv::typeOf(it.value()) &&
        Kv::textValue(current) == Kv::textValue(it.value()))
    {
      continue;
    }
    store->set(it.key(), it.value());
    changed << it.key();
  }
  return changed;
}
}
