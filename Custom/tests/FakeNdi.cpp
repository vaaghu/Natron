// A stand-in for the NDI runtime (libndi.so) with the functions NdiOutput.cpp
// loads, recording what it is given; the fakeNdi* functions let the tests
// read it back.

#include <stdint.h>
#include <string.h>

#include <string>

#if defined(_WIN32)
#define FAKE_NDI_EXPORT extern "C" __declspec(dllexport)
#else
#define FAKE_NDI_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// The exported functions are only looked up with dlsym(): they have no header.
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#endif

namespace
{
// Same layouts as in NdiOutput.cpp.
struct NdiSendCreate
{
  const char *p_ndi_name;
  const char *p_groups;
  bool clock_video;
  bool clock_audio;
};

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

struct Sender
{
  std::string name;
};

int g_senders = 0;
int g_frames = 0;
int g_last[6] = {0, 0, 0, 0, 0, 0}; // xres, yres, FourCC, N, D, stride
std::string g_lastName;
int g_lastClock = -1;
}

FAKE_NDI_EXPORT bool NDIlib_initialize()
{
  return true;
}

FAKE_NDI_EXPORT void *NDIlib_send_create(const NdiSendCreate *create)
{
  Sender *sender = new Sender;
  sender->name = create && create->p_ndi_name ? create->p_ndi_name : "";
  g_lastName = sender->name;
  g_lastClock = create && create->clock_video ? 1 : 0;
  ++g_senders;
  return sender;
}

FAKE_NDI_EXPORT void NDIlib_send_destroy(void *instance)
{
  if (instance)
  {
    --g_senders;
    delete static_cast<Sender *>(instance);
  }
}

FAKE_NDI_EXPORT void NDIlib_send_send_video_v2(void *, const NdiVideoFrameV2 *frame)
{
  ++g_frames;
  g_last[0] = frame->xres;
  g_last[1] = frame->yres;
  g_last[2] = frame->FourCC;
  g_last[3] = frame->frame_rate_N;
  g_last[4] = frame->frame_rate_D;
  g_last[5] = frame->line_stride_in_bytes;
}

FAKE_NDI_EXPORT int NDIlib_send_get_no_connections(void *, uint32_t)
{
  return 3;
}

FAKE_NDI_EXPORT int fakeNdiSenders()
{
  return g_senders;
}

FAKE_NDI_EXPORT int fakeNdiFrames()
{
  return g_frames;
}

FAKE_NDI_EXPORT void fakeNdiLastFrame(int out[6])
{
  memcpy(out, g_last, sizeof(g_last));
}

FAKE_NDI_EXPORT const char *fakeNdiLastName()
{
  return g_lastName.c_str();
}

FAKE_NDI_EXPORT int fakeNdiLastClock()
{
  return g_lastClock;
}
