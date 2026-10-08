#include "Json.h"

#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <cmath>
#include <limits>

namespace
{
// Guards against stack exhaustion on deeply nested input.
const int kMaxDepth = 128;

class Parser
{
public:
  explicit Parser(const QByteArray &text)
      : m_text(text),
        m_pos(0)
  {
  }

  QVariant parseDocument(bool *ok, QString *error)
  {
    m_error.clear();

    QVariant value;
    bool success = parseValue(value, 0);

    if (success)
    {
      skipWhitespace();

      if (m_pos != m_text.size())
      {
        success = fail("unexpected trailing characters");
      }
    }

    if (ok)
    {
      *ok = success;
    }

    if (!success)
    {
      if (error)
      {
        *error = QString::fromUtf8("%1 at offset %2").arg(m_error).arg(m_pos);
      }

      return QVariant();
    }

    return value;
  }

private:
  bool fail(const char *message)
  {
    if (m_error.isEmpty())
    {
      m_error = QString::fromUtf8(message);
    }

    return false;
  }

  bool atEnd() const
  {
    return m_pos >= m_text.size();
  }

  char peek() const
  {
    return atEnd() ? '\0' : m_text.at(m_pos);
  }

  void skipWhitespace()
  {
    while (!atEnd())
    {
      const char c = m_text.at(m_pos);

      if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
      {
        break;
      }

      ++m_pos;
    }
  }

  bool consumeLiteral(const char *literal)
  {
    const int length = int(qstrlen(literal));

    if (m_text.mid(m_pos, length) != QByteArray(literal))
    {
      return fail("invalid literal");
    }

    m_pos += length;
    return true;
  }

  bool parseValue(QVariant &out, int depth)
  {
    if (depth > kMaxDepth)
    {
      return fail("nesting too deep");
    }

    skipWhitespace();

    switch (peek())
    {
    case '{':
      return parseObject(out, depth);
    case '[':
      return parseArray(out, depth);
    case '"':
    {
      QString s;
      if (!parseString(s))
      {
        return false;
      }
      out = s;
      return true;
    }
    case 't':
      out = true;
      return consumeLiteral("true");
    case 'f':
      out = false;
      return consumeLiteral("false");
    case 'n':
      out = QVariant();
      return consumeLiteral("null");
    default:
      return parseNumber(out);
    }
  }

  bool parseObject(QVariant &out, int depth)
  {
    QVariantMap map;

    ++m_pos; // '{'
    skipWhitespace();

    if (peek() == '}')
    {
      ++m_pos;
      out = map;
      return true;
    }

    for (;;)
    {
      skipWhitespace();

      if (peek() != '"')
      {
        return fail("expected string key");
      }

      QString key;
      if (!parseString(key))
      {
        return false;
      }

      skipWhitespace();

      if (peek() != ':')
      {
        return fail("expected ':'");
      }
      ++m_pos;

      QVariant value;
      if (!parseValue(value, depth + 1))
      {
        return false;
      }
      map.insert(key, value);

      skipWhitespace();

      if (peek() == ',')
      {
        ++m_pos;
        continue;
      }

      if (peek() == '}')
      {
        ++m_pos;
        out = map;
        return true;
      }

      return fail("expected ',' or '}'");
    }
  }

  bool parseArray(QVariant &out, int depth)
  {
    QVariantList list;

    ++m_pos; // '['
    skipWhitespace();

    if (peek() == ']')
    {
      ++m_pos;
      out = list;
      return true;
    }

    for (;;)
    {
      QVariant value;
      if (!parseValue(value, depth + 1))
      {
        return false;
      }
      list.append(value);

      skipWhitespace();

      if (peek() == ',')
      {
        ++m_pos;
        continue;
      }

      if (peek() == ']')
      {
        ++m_pos;
        out = list;
        return true;
      }

      return fail("expected ',' or ']'");
    }
  }

  bool parseHex4(uint &out)
  {
    if (m_pos + 4 > m_text.size())
    {
      return fail("truncated \\u escape");
    }

    bool ok = false;
    out = m_text.mid(m_pos, 4).toUInt(&ok, 16);

    if (!ok)
    {
      return fail("invalid \\u escape");
    }

    m_pos += 4;
    return true;
  }

  bool parseString(QString &out)
  {
    ++m_pos; // opening quote

    // Collect raw UTF-8 bytes, decoding escapes, then convert once.
    QByteArray bytes;

    for (;;)
    {
      if (atEnd())
      {
        return fail("unterminated string");
      }

      const char c = m_text.at(m_pos++);

      if (c == '"')
      {
        break;
      }

      if (static_cast<unsigned char>(c) < 0x20)
      {
        return fail("control character in string");
      }

      if (c != '\\')
      {
        bytes.append(c);
        continue;
      }

      if (atEnd())
      {
        return fail("unterminated escape");
      }

      const char e = m_text.at(m_pos++);

      switch (e)
      {
      case '"':
        bytes.append('"');
        break;
      case '\\':
        bytes.append('\\');
        break;
      case '/':
        bytes.append('/');
        break;
      case 'b':
        bytes.append('\b');
        break;
      case 'f':
        bytes.append('\f');
        break;
      case 'n':
        bytes.append('\n');
        break;
      case 'r':
        bytes.append('\r');
        break;
      case 't':
        bytes.append('\t');
        break;
      case 'u':
      {
        uint code = 0;
        if (!parseHex4(code))
        {
          return false;
        }

        // Combine UTF-16 surrogate pairs.
        if (code >= 0xD800 && code <= 0xDBFF)
        {
          uint low = 0;
          if (m_text.mid(m_pos, 2) != "\\u")
          {
            return fail("unpaired surrogate");
          }
          m_pos += 2;
          if (!parseHex4(low))
          {
            return false;
          }
          if (low < 0xDC00 || low > 0xDFFF)
          {
            return fail("unpaired surrogate");
          }
          code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
        }
        else if (code >= 0xDC00 && code <= 0xDFFF)
        {
          return fail("unpaired surrogate");
        }

        appendUtf8(bytes, code);
        break;
      }
      default:
        return fail("invalid escape");
      }
    }

    out = QString::fromUtf8(bytes.constData(), bytes.size());
    return true;
  }

  static void appendUtf8(QByteArray &bytes, uint code)
  {
    if (code < 0x80)
    {
      bytes.append(char(code));
    }
    else if (code < 0x800)
    {
      bytes.append(char(0xC0 | (code >> 6)));
      bytes.append(char(0x80 | (code & 0x3F)));
    }
    else if (code < 0x10000)
    {
      bytes.append(char(0xE0 | (code >> 12)));
      bytes.append(char(0x80 | ((code >> 6) & 0x3F)));
      bytes.append(char(0x80 | (code & 0x3F)));
    }
    else
    {
      bytes.append(char(0xF0 | (code >> 18)));
      bytes.append(char(0x80 | ((code >> 12) & 0x3F)));
      bytes.append(char(0x80 | ((code >> 6) & 0x3F)));
      bytes.append(char(0x80 | (code & 0x3F)));
    }
  }

  static bool isDigit(char c)
  {
    return c >= '0' && c <= '9';
  }

  bool parseNumber(QVariant &out)
  {
    const int start = m_pos;
    bool integral = true;

    if (peek() == '-')
    {
      ++m_pos;
    }

    if (peek() == '0')
    {
      ++m_pos;
    }
    else if (isDigit(peek()))
    {
      while (isDigit(peek()))
      {
        ++m_pos;
      }
    }
    else
    {
      return fail("unexpected character");
    }

    if (peek() == '.')
    {
      integral = false;
      ++m_pos;
      if (!isDigit(peek()))
      {
        return fail("invalid number");
      }
      while (isDigit(peek()))
      {
        ++m_pos;
      }
    }

    if (peek() == 'e' || peek() == 'E')
    {
      integral = false;
      ++m_pos;
      if (peek() == '+' || peek() == '-')
      {
        ++m_pos;
      }
      if (!isDigit(peek()))
      {
        return fail("invalid number");
      }
      while (isDigit(peek()))
      {
        ++m_pos;
      }
    }

    const QByteArray token = m_text.mid(start, m_pos - start);
    bool ok = false;

    if (integral)
    {
      const qlonglong n = token.toLongLong(&ok);
      if (ok)
      {
        out = n;
        return true;
      }
      // Out of qlonglong range: fall back to double.
    }

    const double d = token.toDouble(&ok);
    if (!ok)
    {
      return fail("invalid number");
    }

    out = d;
    return true;
  }

  const QByteArray &m_text;
  int m_pos;
  QString m_error;
};

void writeString(QByteArray &out, const QString &s)
{
  const QByteArray utf8 = s.toUtf8();

  out.append('"');

  for (int i = 0; i < utf8.size(); ++i)
  {
    const char c = utf8.at(i);

    switch (c)
    {
    case '"':
      out.append("\\\"");
      break;
    case '\\':
      out.append("\\\\");
      break;
    case '\b':
      out.append("\\b");
      break;
    case '\f':
      out.append("\\f");
      break;
    case '\n':
      out.append("\\n");
      break;
    case '\r':
      out.append("\\r");
      break;
    case '\t':
      out.append("\\t");
      break;
    default:
      if (static_cast<unsigned char>(c) < 0x20)
      {
        static const char hex[] = "0123456789abcdef";
        out.append("\\u00");
        out.append(hex[(c >> 4) & 0xF]);
        out.append(hex[c & 0xF]);
      }
      else
      {
        out.append(c);
      }
    }
  }

  out.append('"');
}

void writeValue(QByteArray &out, const QVariant &value)
{
  switch (value.type())
  {
  case QVariant::Invalid:
    out.append("null");
    break;
  case QVariant::Bool:
    out.append(value.toBool() ? "true" : "false");
    break;
  case QVariant::Int:
  case QVariant::LongLong:
    out.append(QByteArray::number(value.toLongLong()));
    break;
  case QVariant::UInt:
  case QVariant::ULongLong:
    out.append(QByteArray::number(value.toULongLong()));
    break;
  case QVariant::Double:
  {
    const double d = value.toDouble();
    // JSON has no NaN/Infinity.
    if (d != d || std::fabs(d) > std::numeric_limits<double>::max())
    {
      out.append("null");
    }
    else
    {
      // Shortest form that reads back to the same double (0.1, not 0.10000000000000001).
      QByteArray text;
      for (int precision = 15; precision <= 17; ++precision)
      {
        text = QByteArray::number(d, 'g', precision);
        if (text.toDouble() == d)
        {
          break;
        }
      }
      out.append(text);
    }
    break;
  }
  case QVariant::List:
  case QVariant::StringList:
  {
    const QVariantList list = value.toList();
    out.append('[');
    for (int i = 0; i < list.size(); ++i)
    {
      if (i > 0)
      {
        out.append(',');
      }
      writeValue(out, list.at(i));
    }
    out.append(']');
    break;
  }
  case QVariant::Map:
  {
    const QVariantMap map = value.toMap();
    out.append('{');
    bool first = true;
    for (QVariantMap::const_iterator it = map.constBegin(); it != map.constEnd(); ++it)
    {
      if (!first)
      {
        out.append(',');
      }
      first = false;
      writeString(out, it.key());
      out.append(':');
      writeValue(out, it.value());
    }
    out.append('}');
    break;
  }
  default:
    writeString(out, value.toString());
  }
}
}

namespace Json
{
QVariant parse(const QByteArray &text, bool *ok, QString *error)
{
  Parser parser(text);
  return parser.parseDocument(ok, error);
}

QByteArray serialize(const QVariant &value)
{
  QByteArray out;
  writeValue(out, value);
  return out;
}
}
