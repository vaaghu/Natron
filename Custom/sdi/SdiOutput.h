#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// SDI output through Blackmagic DeckLink cards, loaded at runtime from the
// installed Desktop Video drivers (libDeckLinkAPI). Natron does not link or
// ship them: if they are not installed, SDI is simply unavailable.
// Get them from https://www.blackmagicdesign.com/support (Desktop Video).
namespace Sdi
{
// Loads the drivers' API once. Returns false (and why) if not available.
bool load(QString *error = nullptr);
bool isAvailable();

// A video mode a port can play out.
struct Mode
{
  quint32 id; // BMDDisplayMode
  QString name;
  int width;
  int height;
  double fps;
  bool interlaced;

  Mode()
      : id(0),
        width(0),
        height(0),
        fps(0),
        interlaced(false)
  {
  }
};

// An SDI output: a DeckLink device, or one connector of a card with several
// (each is its own device, "DeckLink Duo (1)", "DeckLink Duo (2)"...).
struct Port
{
  QString name; // display name, unique on this machine
  QString model;
  QList<Mode> modes;
};

// The ports that can play out (empty if none, or no drivers: *error set).
QList<Port> ports(QString *error = nullptr);

// Plays frames out of one port, each shown as soon as it is sent (the
// caller keeps the frame rate). Used from one thread.
class Output
{
public:
  Output();
  ~Output();

  // Opens a port by name in a mode by id; mode 0: the mode of that size and
  // frame rate (progressive preferred).
  bool open(const QString &port, quint32 mode, int width, int height, double fps, QString *error);
  void close();
  bool isOpen() const;
  QString modeName() const; // the mode playing

  // 8-bit BGRA, top row first, tightly packed; scaled to the mode's size.
  void sendFrame(const QByteArray &bgra, int width, int height);

private:
  struct Private;
  Private *d;

  Output(const Output &);
  Output &operator=(const Output &);
};
}
