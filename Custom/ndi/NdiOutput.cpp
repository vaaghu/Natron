#include "NdiOutput.h"

#include <QByteArray>
#include <QLibrary>
#include <QMutex>
#include <QMutexLocker>
#include <QProcessEnvironment>
#include <QStringList>

#include <stdint.h>

namespace
{
// ---- Minimal declarations of the NDI C API (Processing.NDI.Lib.h) ----
// Only what is used here; layouts match NDI SDK v4/v5/v6.

typedef void *NdiSendInstance;

struct NdiSendCreate
{
  const char *p_ndi_name;
  const char *p_groups;
  bool clock_video;
  bool clock_audio;
};

#define NDI_FOURCC(a, b, c, d) \
  ((uint32_t)(uint8_t)(a) | ((uint32_t)(uint8_t)(b) << 8) | ((uint32_t)(uint8_t)(c) << 16) | ((uint32_t)(uint8_t)(d) << 24))

const int32_t kNdiFourCCBGRA = (int32_t)NDI_FOURCC('B', 'G', 'R', 'A');
const int32_t kNdiFourCCBGRX = (int32_t)NDI_FOURCC('B', 'G', 'R', 'X');
const int32_t kNdiFrameFormatProgressive = 1;
const int64_t kNdiTimecodeSynthesize = INT64_MAX;

struct NdiVideoFrameV2
{
  int xres;
  int yres;
  int32_t FourCC;
  int frame_rate_N;
  int frame_rate_D;
  float picture_aspect_ratio;
  int32_t frame_format_type;
  int64_t timecode;
  uint8_t *p_data;
  int line_stride_in_bytes;
  const char *p_metadata;
  int64_t timestamp;
};

typedef bool (*NdiInitializeFn)();
typedef NdiSendInstance (*NdiSendCreateFn)(const NdiSendCreate *);
typedef void (*NdiSendDestroyFn)(NdiSendInstance);
typedef void (*NdiSendVideoV2Fn)(NdiSendInstance, const NdiVideoFrameV2 *);
typedef int (*NdiSendGetNoConnectionsFn)(NdiSendInstance, uint32_t);

struct NdiApi
{
  QLibrary library;
  bool tried;
  bool ok;
  QString error;
  NdiInitializeFn initialize;
  NdiSendCreateFn sendCreate;
  NdiSendDestroyFn sendDestroy;
  NdiSendVideoV2Fn sendVideo;
  NdiSendGetNoConnectionsFn sendConnections;

  NdiApi()
      : tried(false),
        ok(false),
        initialize(nullptr),
        sendCreate(nullptr),
        sendDestroy(nullptr),
        sendVideo(nullptr),
        sendConnections(nullptr)
  {
  }
};

NdiApi &api()
{
  static NdiApi instance;
  return instance;
}

QMutex &apiMutex()
{
  static QMutex mutex;
  return mutex;
}

QStringList candidateLibraries()
{
  QStringList names;
  const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

#if defined(Q_OS_WIN)
  // The NDI Runtime installer sets NDI_RUNTIME_DIR_Vn.
  const char *vars[] = {"NDI_RUNTIME_DIR_V6", "NDI_RUNTIME_DIR_V5", "NDI_RUNTIME_DIR_V4"};
  for (int i = 0; i < 3; ++i)
  {
    const QString dir = env.value(QString::fromUtf8(vars[i]));
    if (!dir.isEmpty())
    {
      names << dir + QString::fromUtf8("/Processing.NDI.Lib.x64.dll");
    }
  }
  names << QString::fromUtf8("Processing.NDI.Lib.x64.dll");
#elif defined(Q_OS_MAC)
  names << QString::fromUtf8("/usr/local/lib/libndi.dylib")
        << QString::fromUtf8("/Library/NDI SDK for Apple/lib/macOS/libndi.dylib")
        << QString::fromUtf8("libndi.dylib");
#else
  const QString dir = env.value(QString::fromUtf8("NDI_RUNTIME_DIR_V6"), env.value(QString::fromUtf8("NDI_RUNTIME_DIR_V5")));
  if (!dir.isEmpty())
  {
    names << dir + QString::fromUtf8("/libndi.so");
  }
  names << QString::fromUtf8("libndi.so.6") << QString::fromUtf8("libndi.so.5")
        << QString::fromUtf8("libndi.so") << QString::fromUtf8("/usr/local/lib/libndi.so")
        << QString::fromUtf8("/usr/lib/libndi.so");
#endif
  return names;
}

class NdiSink : public FrameSink
{
public:
  NdiSink()
      : m_send(nullptr),
        m_alpha(false)
  {
  }

  ~NdiSink()
  {
    if (m_send)
    {
      api().sendDestroy(m_send);
    }
  }

  bool open(const QString &name, bool alpha, QString *error)
  {
    if (!Ndi::load(error))
    {
      return false;
    }

    m_alpha = alpha;
    m_name = name.toUtf8();

    NdiSendCreate create;
    create.p_ndi_name = m_name.constData();
    create.p_groups = nullptr;
    create.clock_video = true; // send_video waits to keep the frame rate
    create.clock_audio = false;

    m_send = api().sendCreate(&create);
    if (!m_send && error)
    {
      *error = QString::fromUtf8("NDI could not create the source \"%1\"").arg(name);
    }
    return m_send != nullptr;
  }

  void sendFrame(const QByteArray &bgra, int width, int height, int fpsNum, int fpsDen)
  {
    if (!m_send || bgra.size() < width * height * 4)
    {
      return;
    }

    NdiVideoFrameV2 frame;
    frame.xres = width;
    frame.yres = height;
    frame.FourCC = m_alpha ? kNdiFourCCBGRA : kNdiFourCCBGRX;
    frame.frame_rate_N = fpsNum;
    frame.frame_rate_D = fpsDen;
    frame.picture_aspect_ratio = 0.0f; // square pixels
    frame.frame_format_type = kNdiFrameFormatProgressive;
    frame.timecode = kNdiTimecodeSynthesize;
    frame.p_data = reinterpret_cast<uint8_t *>(const_cast<char *>(bgra.constData()));
    frame.line_stride_in_bytes = width * 4;
    frame.p_metadata = nullptr;
    frame.timestamp = 0;

    api().sendVideo(m_send, &frame); // synchronous: NDI copies/sends before returning
  }

  bool isClocked() const
  {
    return true;
  }

  int connectionCount()
  {
    return m_send ? api().sendConnections(m_send, 0) : -1;
  }

private:
  NdiSendInstance m_send;
  QByteArray m_name;
  bool m_alpha;
};
}

namespace Ndi
{
bool load(QString *error)
{
  QMutexLocker locker(&apiMutex());
  NdiApi &a = api();

  if (!a.tried)
  {
    a.tried = true;
    const QStringList names = candidateLibraries();
    for (int i = 0; i < names.size() && !a.library.isLoaded(); ++i)
    {
      a.library.setFileName(names.at(i));
      a.library.load();
    }

    if (!a.library.isLoaded())
    {
      a.error = QString::fromUtf8("NDI Runtime not installed (get it from https://ndi.video/tools/)");
    }
    else
    {
      a.initialize = (NdiInitializeFn)a.library.resolve("NDIlib_initialize");
      a.sendCreate = (NdiSendCreateFn)a.library.resolve("NDIlib_send_create");
      a.sendDestroy = (NdiSendDestroyFn)a.library.resolve("NDIlib_send_destroy");
      a.sendVideo = (NdiSendVideoV2Fn)a.library.resolve("NDIlib_send_send_video_v2");
      a.sendConnections = (NdiSendGetNoConnectionsFn)a.library.resolve("NDIlib_send_get_no_connections");

      if (!a.initialize || !a.sendCreate || !a.sendDestroy || !a.sendVideo || !a.sendConnections)
      {
        a.error = QString::fromUtf8("%1 is not a supported NDI library").arg(a.library.fileName());
      }
      else if (!a.initialize())
      {
        a.error = QString::fromUtf8("NDI cannot run on this CPU");
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
}

bool isAvailable()
{
  return load(nullptr);
}

QString libraryPath()
{
  QMutexLocker locker(&apiMutex());
  return api().ok ? api().library.fileName() : QString();
}

FrameSink *createSink()
{
  return new NdiSink;
}
}
