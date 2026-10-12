#include "SdiOutput.h"

#include <QImage>
#include <QLibrary>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>

#include <cmath>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace
{
#if defined(Q_OS_LINUX)
// ---- Minimal declarations of the DeckLink API (DeckLinkAPI.h, SDK 10.11) ----
// Only what is used here; the interfaces (and their IIDs) of SDK 10.11 are
// kept by newer drivers. The vtables must match the SDK's exactly: methods
// that are not used are declared too, in their order.

typedef int HRESULT;
typedef unsigned long ULONG;
const HRESULT kOk = 0;

struct Iid
{
  unsigned char bytes[16];
};

const Iid kIidOutput = {{0xCC, 0x5C, 0x8A, 0x6E, 0x3F, 0x2F, 0x4B, 0x3A, 0x87, 0xEA, 0xFD, 0x78, 0xAF, 0x30, 0x05, 0x64}};

typedef uint32_t BMDDisplayMode;
typedef uint32_t BMDPixelFormat;
typedef uint32_t BMDVideoOutputFlags;
typedef uint32_t BMDFrameFlags;
typedef uint32_t BMDDisplayModeSupport;
typedef uint32_t BMDFieldDominance;
typedef uint32_t BMDDisplayModeFlags;
typedef int64_t BMDTimeValue;
typedef int64_t BMDTimeScale;

const BMDPixelFormat kFormat8BitYUV = 0x32767579;  // '2vuy'
const BMDPixelFormat kFormat8BitBGRA = 0x42475241; // 'BGRA'
const BMDVideoOutputFlags kVideoOutputFlagDefault = 0;
const BMDFrameFlags kFrameFlagDefault = 0;
const BMDDisplayModeSupport kDisplayModeNotSupported = 0;
const BMDFieldDominance kLowerFieldFirst = 0x6C6F7772; // 'lowr'
const BMDFieldDominance kUpperFieldFirst = 0x75707072; // 'uppr'

class IUnknown
{
public:
  virtual HRESULT QueryInterface(Iid iid, void **out) = 0;
  virtual ULONG AddRef() = 0;
  virtual ULONG Release() = 0;
};

class IDeckLinkDisplayMode : public IUnknown
{
public:
  virtual HRESULT GetName(const char **name) = 0;
  virtual BMDDisplayMode GetDisplayMode() = 0;
  virtual long GetWidth() = 0;
  virtual long GetHeight() = 0;
  virtual HRESULT GetFrameRate(BMDTimeValue *frameDuration, BMDTimeScale *timeScale) = 0;
  virtual BMDFieldDominance GetFieldDominance() = 0;
  virtual BMDDisplayModeFlags GetFlags() = 0;

protected:
  virtual ~IDeckLinkDisplayMode() {}
};

class IDeckLinkDisplayModeIterator : public IUnknown
{
public:
  virtual HRESULT Next(IDeckLinkDisplayMode **mode) = 0;

protected:
  virtual ~IDeckLinkDisplayModeIterator() {}
};

class IDeckLinkVideoFrame : public IUnknown
{
public:
  virtual long GetWidth() = 0;
  virtual long GetHeight() = 0;
  virtual long GetRowBytes() = 0;
  virtual BMDPixelFormat GetPixelFormat() = 0;
  virtual BMDFrameFlags GetFlags() = 0;
  virtual HRESULT GetBytes(void **buffer) = 0;
  virtual HRESULT GetTimecode(uint32_t format, void **timecode) = 0;
  virtual HRESULT GetAncillaryData(void **ancillary) = 0;

protected:
  virtual ~IDeckLinkVideoFrame() {}
};

class IDeckLinkMutableVideoFrame : public IDeckLinkVideoFrame
{
public:
  virtual HRESULT SetFlags(BMDFrameFlags flags) = 0;
  virtual HRESULT SetTimecode(uint32_t format, void *timecode) = 0;
  virtual HRESULT SetTimecodeFromComponents(uint32_t format, uint8_t hours, uint8_t minutes, uint8_t seconds, uint8_t frames, uint32_t flags) = 0;
  virtual HRESULT SetAncillaryData(void *ancillary) = 0;
  virtual HRESULT SetTimecodeUserBits(uint32_t format, uint32_t userBits) = 0;

protected:
  virtual ~IDeckLinkMutableVideoFrame() {}
};

class IDeckLinkOutput : public IUnknown
{
public:
  virtual HRESULT DoesSupportVideoMode(BMDDisplayMode mode, BMDPixelFormat format, BMDVideoOutputFlags flags,
                                       BMDDisplayModeSupport *result, IDeckLinkDisplayMode **resultMode) = 0;
  virtual HRESULT GetDisplayModeIterator(IDeckLinkDisplayModeIterator **iterator) = 0;
  virtual HRESULT SetScreenPreviewCallback(void *callback) = 0;
  virtual HRESULT EnableVideoOutput(BMDDisplayMode mode, BMDVideoOutputFlags flags) = 0;
  virtual HRESULT DisableVideoOutput() = 0;
  virtual HRESULT SetVideoOutputFrameMemoryAllocator(void *allocator) = 0;
  virtual HRESULT CreateVideoFrame(int32_t width, int32_t height, int32_t rowBytes, BMDPixelFormat format,
                                   BMDFrameFlags flags, IDeckLinkMutableVideoFrame **frame) = 0;
  virtual HRESULT CreateAncillaryData(BMDPixelFormat format, void **buffer) = 0;
  virtual HRESULT DisplayVideoFrameSync(IDeckLinkVideoFrame *frame) = 0;
  // (scheduled playback, audio and timing follow; not used)

protected:
  virtual ~IDeckLinkOutput() {}
};

class IDeckLink : public IUnknown
{
public:
  virtual HRESULT GetModelName(const char **name) = 0;
  virtual HRESULT GetDisplayName(const char **name) = 0;

protected:
  virtual ~IDeckLink() {}
};

class IDeckLinkIterator : public IUnknown
{
public:
  virtual HRESULT Next(IDeckLink **deckLink) = 0;
};

typedef IDeckLinkIterator *(*CreateIteratorFn)();

struct DeckLinkApi
{
  QLibrary library;
  bool tried;
  bool ok;
  QString error;
  CreateIteratorFn createIterator;

  DeckLinkApi()
      : tried(false),
        ok(false),
        createIterator(nullptr)
  {
  }
};

DeckLinkApi &api()
{
  static DeckLinkApi instance;
  return instance;
}

QMutex &apiMutex()
{
  static QMutex mutex;
  return mutex;
}

// Strings the API returns are the caller's to free (Linux).
QString takeString(const char *s)
{
  const QString result = s ? QString::fromUtf8(s) : QString();
  free(const_cast<char *>(s));
  return result;
}

template <class T>
void release(T *&p)
{
  if (p)
  {
    p->Release();
    p = nullptr;
  }
}

Sdi::Mode modeInfo(IDeckLinkDisplayMode *m)
{
  Sdi::Mode mode;
  const char *name = nullptr;
  if (m->GetName(&name) == kOk)
  {
    mode.name = takeString(name);
  }
  mode.id = m->GetDisplayMode();
  mode.width = int(m->GetWidth());
  mode.height = int(m->GetHeight());
  BMDTimeValue duration = 0;
  BMDTimeScale scale = 0;
  if (m->GetFrameRate(&duration, &scale) == kOk && duration > 0)
  {
    mode.fps = double(scale) / double(duration);
  }
  const BMDFieldDominance field = m->GetFieldDominance();
  mode.interlaced = field == kLowerFieldFirst || field == kUpperFieldFirst;
  return mode;
}

QList<Sdi::Mode> modesOf(IDeckLinkOutput *output)
{
  QList<Sdi::Mode> modes;
  IDeckLinkDisplayModeIterator *it = nullptr;
  if (output->GetDisplayModeIterator(&it) != kOk || !it)
  {
    return modes;
  }
  IDeckLinkDisplayMode *m = nullptr;
  while (it->Next(&m) == kOk && m)
  {
    modes << modeInfo(m);
    release(m);
  }
  release(it);
  return modes;
}

// Visits the devices that can play out; stops when visit() returns true
// (it then keeps the references it took).
template <class Visit>
bool forEachOutput(Visit visit, QString *error)
{
  if (!Sdi::load(error))
  {
    return false;
  }
  IDeckLinkIterator *it = api().createIterator();
  if (!it)
  {
    if (error)
    {
      *error = QString::fromUtf8("DeckLink drivers did not list the devices");
    }
    return false;
  }
  QStringList names;
  bool found = false;
  IDeckLink *deckLink = nullptr;
  while (!found && it->Next(&deckLink) == kOk && deckLink)
  {
    IDeckLinkOutput *output = nullptr;
    if (deckLink->QueryInterface(kIidOutput, reinterpret_cast<void **>(&output)) == kOk && output)
    {
      const char *s = nullptr;
      QString name = deckLink->GetDisplayName(&s) == kOk ? takeString(s) : QString();
      if (name.isEmpty())
      {
        name = QString::fromUtf8("DeckLink");
      }
      // Unique names (identical cards).
      QString unique = name;
      for (int n = 2; names.contains(unique); ++n)
      {
        unique = QString::fromUtf8("%1 #%2").arg(name).arg(n);
      }
      names << unique;
      s = nullptr;
      const QString model = deckLink->GetModelName(&s) == kOk ? takeString(s) : QString();
      found = visit(deckLink, output, unique, model);
      if (!found)
      {
        release(output);
      }
    }
    if (!found)
    {
      release(deckLink);
    }
  }
  release(it);
  return found;
}

struct PortLister
{
  QList<Sdi::Port> *ports;

  bool operator()(IDeckLink *, IDeckLinkOutput *output, const QString &name, const QString &model) const
  {
    Sdi::Port port;
    port.name = name;
    port.model = model;
    port.modes = modesOf(output);
    *ports << port;
    return false;
  }
};

struct PortFinder
{
  QString wanted;
  IDeckLink **deckLink;
  IDeckLinkOutput **output;

  bool operator()(IDeckLink *d, IDeckLinkOutput *o, const QString &name, const QString &) const
  {
    if (name != wanted)
    {
      return false;
    }
    *deckLink = d;
    *output = o;
    return true;
  }
};

// BGRA -> 8-bit 4:2:2 UYVY ('2vuy'), video range, Rec. 709 or 601.
void toUyvy(const uchar *bgra, int width, int height, uchar *dst, int rowBytes, bool rec601)
{
  // Coefficients * 256: Y, Cb, Cr from R, G, B.
  const int yr = rec601 ? 66 : 47, yg = rec601 ? 129 : 157, yb = rec601 ? 25 : 16;
  const int ur = rec601 ? -38 : -26, ug = rec601 ? -74 : -87, ub = 112;
  const int vr = 112, vg = rec601 ? -94 : -102, vb = rec601 ? -18 : -10;
  for (int y = 0; y < height; ++y)
  {
    const uchar *s = bgra + y * width * 4;
    uchar *d = dst + y * rowBytes;
    for (int x = 0; x + 1 < width; x += 2, s += 8, d += 4)
    {
      const int b0 = s[0], g0 = s[1], r0 = s[2];
      const int b1 = s[4], g1 = s[5], r1 = s[6];
      const int r = (r0 + r1) >> 1, g = (g0 + g1) >> 1, b = (b0 + b1) >> 1;
      d[0] = uchar(((ur * r + ug * g + ub * b + 128) >> 8) + 128);
      d[1] = uchar(((yr * r0 + yg * g0 + yb * b0 + 128) >> 8) + 16);
      d[2] = uchar(((vr * r + vg * g + vb * b + 128) >> 8) + 128);
      d[3] = uchar(((yr * r1 + yg * g1 + yb * b1 + 128) >> 8) + 16);
    }
  }
}
#endif // Q_OS_LINUX
}

namespace Sdi
{
bool load(QString *error)
{
#if defined(Q_OS_LINUX)
  QMutexLocker locker(&apiMutex());
  DeckLinkApi &a = api();

  if (!a.tried)
  {
    a.tried = true;
    a.library.setFileName(QString::fromUtf8("libDeckLinkAPI.so"));
    if (!a.library.load())
    {
      a.error = QString::fromUtf8("Blackmagic Desktop Video drivers not installed (get them from https://www.blackmagicdesign.com/support)");
    }
    else
    {
      a.createIterator = (CreateIteratorFn)a.library.resolve("CreateDeckLinkIteratorInstance_0003");
      if (!a.createIterator)
      {
        a.error = QString::fromUtf8("%1 is not a supported DeckLink library").arg(a.library.fileName());
      }
      else
      {
        a.ok = true;
      }
    }
  }

  if (!a.ok && error)
  {
    *error = a.error;
  }
  return a.ok;
#else
  if (error)
  {
    *error = QString::fromUtf8("SDI output is only available on Linux");
  }
  return false;
#endif
}

bool isAvailable()
{
  return load(nullptr);
}

QList<Port> ports(QString *error)
{
  QList<Port> result;
#if defined(Q_OS_LINUX)
  PortLister lister;
  lister.ports = &result;
  QString why;
  forEachOutput(lister, &why);
  if (result.isEmpty() && error)
  {
    *error = why.isEmpty() ? QString::fromUtf8("No DeckLink output found") : why;
  }
#else
  load(error);
#endif
  return result;
}

struct Output::Private
{
#if defined(Q_OS_LINUX)
  IDeckLink *deckLink;
  IDeckLinkOutput *output;
  IDeckLinkMutableVideoFrame *frame;
  bool yuv;    // the frame is UYVY (the card cannot take BGRA in this mode)
  bool rec601; // SD
#endif
  Mode mode;

  Private()
#if defined(Q_OS_LINUX)
      : deckLink(nullptr),
        output(nullptr),
        frame(nullptr),
        yuv(false),
        rec601(false)
#endif
  {
  }
};

Output::Output()
    : d(new Private)
{
}

Output::~Output()
{
  close();
  delete d;
}

bool Output::open(const QString &port, quint32 mode, int width, int height, double fps, QString *error)
{
  close();
#if defined(Q_OS_LINUX)
  PortFinder finder;
  finder.wanted = port;
  finder.deckLink = &d->deckLink;
  finder.output = &d->output;
  QString why;
  if (!forEachOutput(finder, &why))
  {
    if (error)
    {
      *error = why.isEmpty() ? QString::fromUtf8("SDI port \"%1\" not found").arg(port) : why;
    }
    return false;
  }

  // The mode: by id, or by size and frame rate.
  const QList<Mode> modes = modesOf(d->output);
  int chosen = -1;
  for (int i = 0; i < modes.size(); ++i)
  {
    const Mode &m = modes.at(i);
    if (mode != 0)
    {
      if (m.id == mode)
      {
        chosen = i;
        break;
      }
    }
    else if (m.width == width && m.height == height && std::fabs(m.fps - fps) < 0.01 &&
             (chosen < 0 || (modes.at(chosen).interlaced && !m.interlaced)))
    {
      chosen = i;
    }
  }
  if (chosen < 0)
  {
    if (error)
    {
      *error = (mode != 0) ? QString::fromUtf8("%1 does not have the chosen video mode").arg(port)
                           : QString::fromUtf8("%1 has no video mode of %2x%3 at %4 fps").arg(port).arg(width).arg(height).arg(fps);
    }
    close();
    return false;
  }
  d->mode = modes.at(chosen);

  BMDDisplayModeSupport support = kDisplayModeNotSupported;
  d->yuv = d->output->DoesSupportVideoMode(d->mode.id, kFormat8BitBGRA, kVideoOutputFlagDefault, &support, nullptr) != kOk ||
           support == kDisplayModeNotSupported;
  d->rec601 = d->mode.height < 720;

  if (d->output->EnableVideoOutput(d->mode.id, kVideoOutputFlagDefault) != kOk)
  {
    if (error)
    {
      *error = QString::fromUtf8("%1 could not start %2 (in use by another application, or set as an input?)").arg(port).arg(d->mode.name);
    }
    close();
    return false;
  }

  const int w = d->mode.width;
  const int h = d->mode.height;
  const int rowBytes = d->yuv ? w * 2 : w * 4;
  if (d->output->CreateVideoFrame(w, h, rowBytes, d->yuv ? kFormat8BitYUV : kFormat8BitBGRA, kFrameFlagDefault, &d->frame) != kOk || !d->frame)
  {
    if (error)
    {
      *error = QString::fromUtf8("%1: could not create a video frame").arg(port);
    }
    close();
    return false;
  }
  return true;
#else
  Q_UNUSED(port);
  Q_UNUSED(mode);
  Q_UNUSED(width);
  Q_UNUSED(height);
  Q_UNUSED(fps);
  load(error);
  return false;
#endif
}

void Output::close()
{
#if defined(Q_OS_LINUX)
  release(d->frame);
  if (d->output)
  {
    d->output->DisableVideoOutput();
  }
  release(d->output);
  release(d->deckLink);
#endif
  d->mode = Mode();
}

bool Output::isOpen() const
{
#if defined(Q_OS_LINUX)
  return d->frame != nullptr;
#else
  return false;
#endif
}

QString Output::modeName() const
{
  return d->mode.name;
}

void Output::sendFrame(const QByteArray &bgra, int width, int height)
{
#if defined(Q_OS_LINUX)
  if (!d->frame || width <= 0 || height <= 0 || bgra.size() < width * height * 4)
  {
    return;
  }
  void *bytes = nullptr;
  if (d->frame->GetBytes(&bytes) != kOk || !bytes)
  {
    return;
  }
  const int w = d->mode.width;
  const int h = d->mode.height;

  // To the mode's size (BGRA bytes = ARGB32 on little-endian machines).
  QImage scaled;
  const uchar *src = reinterpret_cast<const uchar *>(bgra.constData());
  if (width != w || height != h)
  {
    const QImage image(src, width, height, width * 4, QImage::Format_ARGB32);
    scaled = image.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32);
    src = scaled.constBits();
  }

  const int rowBytes = int(d->frame->GetRowBytes());
  uchar *dst = static_cast<uchar *>(bytes);
  if (d->yuv)
  {
    toUyvy(src, w, h, dst, rowBytes, d->rec601);
  }
  else
  {
    for (int y = 0; y < h; ++y)
    {
      memcpy(dst + y * rowBytes, src + y * w * 4, w * 4);
    }
  }
  d->output->DisplayVideoFrameSync(d->frame);
#else
  Q_UNUSED(bgra);
  Q_UNUSED(width);
  Q_UNUSED(height);
#endif
}
}
