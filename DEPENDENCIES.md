# Dependencies

The dashboard and the editor are the same `Natron` executable (the dashboard
window opens first, projects open in editor windows). Renders started from the
dashboard run in a separate `NatronRenderer` process, which loads the same
engine and OFX plug-ins as the editor.

"Linked" below means the library is part of the build; the sections say which
side actually uses it at run time.

## Common (dashboard and editor)

### Build tools

| Dependency | Version | Notes |
|---|---|---|
| C++ compiler | C++14 | GCC, Clang or MinGW-w64 (MSVC partly supported) |
| CMake | ≥ 3.16.7 | `CMakeLists.txt`; qmake (`Project.pro`) is also supported |
| Python 3 interpreter | | Runs `tools/utils/sourceList.py` at configure time |
| shiboken2 generator | 5.15 | Generates the Python bindings (`Engine/`, `Gui/` typesystems) |
| pkg-config | | Finds cairo, fontconfig, expat, PySide, shiboken |
| Docker | | `scripts/natron-build.sh` builds in `natrongithub/natron-sdk` or `natron-sdk-qt5` |

### Libraries

| Dependency | Version | Used by | Why |
|---|---|---|---|
| Qt Core, Gui, Network, Widgets, Concurrent | 5.15 (CMake); Qt 4.8 via qmake | all modules | UI, networking, threads, processes |
| Python 3 (libpython) | 3.10 in the SDK | Engine, Gui, App, Renderer | Python scripting; renders run a generated Python script inside `NatronRenderer` |
| PySide2 + libshiboken2 | 5.15 (PySide 1 with Qt 4) | Engine, Gui | Python bindings of Natron and Qt |
| Boost | headers + serialization | Engine | Project (`.ntp`) load and save |
| cairo, fontconfig | | Engine | Roto/shape rasterisation, fonts |
| expat | | HostSupport | OFX host XML |
| OpenFX headers | submodule `libs/OpenFX` | HostSupport, Engine | OFX plug-in host API |
| OpenFX extensions | `libs/OpenFX_extensions` | Engine | Natron/Nuke OFX extensions |
| SequenceParsing | submodule `libs/SequenceParsing` | Engine | Image-sequence file names |
| X11 (+ Wayland via ECM, optional) | | Linux only | Windowing |
| mpr | system | Windows only | Map network drive letters |
| qtpy | pip, in the installer | bundled Python | Qt-agnostic Python for scripts/PyPlugs |
| OpenColorIO configs | `OpenColorIO-Configs/` (downloaded) | Engine, plug-ins | Colour management |

### Plug-ins needed to render a scene

The dashboard renders through `NatronRenderer`, so these must be installed for
the dashboard as well as the editor (see [Plug-ins](#plug-ins) for the full list):

| Plug-in ID | Bundle | Used for |
|---|---|---|
| `fr.inria.openfx.Write*` (WriteFFmpeg, WriteOIIO, WritePNG…) | openfx-io | Scene outputs; `Custom/scene/ProjectInfo.cpp` lists Write nodes as outputs |
| `fr.inria.built-in.Read` / `fr.inria.openfx.Read*` | Natron + openfx-io | Image inputs (`Gui/ScenePanel.cpp`) |
| `net.fxarena.openfx.Text` | openfx-arena | Text inputs bound to state-store keys (`Custom/scene/SceneRenderer.cpp`) |

## Dashboard only

Code: `Custom/` (static library `NatronCustom`) and the dashboard files in
`Gui/`: `DashboardWindow`, `DashboardSettingsDialog`, `ScenePanel`,
`SwitcherPanel`, `SwitcherWidgets`, `SceneShortcutsDialog`,
`NotificationCenter`.

| Dependency | How it is found | Used by | Why |
|---|---|---|---|
| Qt Network (`QTcpServer`, `QUdpSocket`) | linked | `Custom/server/HttpServer`, `Custom/scoreboard/ScoreboardFeed` | HTTP API on port 3000 (`/state`, `/events`, `/scoreboard`); Ultra Score console feed on UDP 2800 |
| `NatronRenderer` | next to `Natron` | `Custom/scene/SceneRenderer` | Renders scenes in a child process |
| `ffmpeg` | bundled in `bin/` (`Custom/util/BundledTool`) | `SceneRenderer`, `ndi/PlayoutChannel`, `ndi/NdiManager` | First-frame thumbnails, decoding rendered media for playout |
| `ffprobe` | bundled in `bin/` | `ndi/MediaProbe`, `ndi/NdiManager` | Media duration, size, frame rate |
| NDI runtime (`libndi.so.6`/`.5`, `libndi.dylib`, `Processing.NDI.Lib.x64.dll`) | loaded at run time with `QLibrary`; `NDI_RUNTIME_DIR_V4/V5/V6` | `Custom/ndi/NdiOutput` | NDI output of Program and playout channels. Optional: the dashboard reports it as unavailable when missing |
| Blackmagic Desktop Video (`libDeckLinkAPI.so` / DeckLink API) | loaded at run time with `QLibrary` | `Custom/sdi/SdiOutput` | SDI output of Program. Optional, same as NDI |

Test-only: GoogleTest (`Custom/tests`, `Tests/google-test` submodule).

## Editor only

These are linked into every build but only the editor's features use them.

| Dependency | Location | Used by | Why |
|---|---|---|---|
| OpenGL (glad loader) | `Global/gladDeb`, `Global/gladRel` | Gui viewers, node graph, curve editor | GPU drawing in the editor |
| libmv | `libs/libmv` | `Engine/TrackerContext*` | Tracker |
| openMVG | `libs/openMVG` | Tracker | Transform/corner-pin solving from tracks |
| Ceres Solver | `libs/ceres` (or system with `NATRON_SYSTEM_LIBS`) | libmv, openMVG | Non-linear optimisation |
| glog, gflags | `libs/glog`, `libs/gflags` (or system) | Ceres, libmv | Logging, flags |
| Eigen 3 | `libs/Eigen3` | libmv, openMVG, Ceres | Linear algebra |
| libtess | `libs/libtess` | `Engine/RotoContext` | Roto shape tessellation |
| hoedown | `libs/hoedown` | `Engine/Markdown`, node documentation | Markdown in plug-in help |
| qhttpserver | `libs/qhttpserver` | `Gui/DocumentationManager` | Local documentation server (not the dashboard's HTTP API) |
| Google Breakpad (optional) | submodule `libs/google-breakpad`, `CONFIG+=enable-breakpad` | `BreakpadClient`, `CrashReporter` | Crash reports |
| GoogleTest / Google Mock | submodules `Tests/google-*` | `Tests/` | Engine unit tests |

The editor also uses the Custom state store for one feature: the State
binding tab in node settings (`Gui/StateBindingTab`, `Gui/KvGuiUtils`) binds
parameters to dashboard keys.

## Plug-ins

### OFX plug-in bundles

Built by `tools/jenkins/build-plugins.sh` (driven by
`scripts/natron-build.sh`) and installed in `Plugins/OFX/Natron`. Used by the
editor; the dashboard needs them only through `NatronRenderer`.

| Bundle | Repository | Built | Contents | Main third-party deps |
|---|---|---|---|---|
| `IO.ofx` | [openfx-io](https://github.com/NatronGitHub/openfx-io) | yes | Read/Write for FFmpeg, OIIO, EXR, PNG, PFM, PSD, raw; OCIO nodes; RunScript | FFmpeg, OpenImageIO, OpenEXR, OpenColorIO, LibRaw, SeExpr |
| `Misc.ofx` | [openfx-misc](https://github.com/NatronGitHub/openfx-misc) | yes | Merge, Transform, Grade, ColorCorrect, Roto, Shuffle, Retime, Keyers, Shadertoy… | OpenGL / OSMesa (Shadertoy) |
| `CImg.ofx` | openfx-misc (`CImg/`) | yes | Blur, Erode, Denoise, Bilateral… | CImg |
| `Arena.ofx` | [openfx-arena](https://github.com/NatronGitHub/openfx-arena) | yes | Text, ReadSVG, ReadPSD, ReadKrita, ReadCDR, ReadPDF, OpenRaster, effects | ImageMagick, librsvg, Pango/cairo, poppler, libcdr, LCMS |
| `GMIC.ofx` | [openfx-gmic](https://github.com/NatronGitHub/openfx-gmic) | yes | G'MIC filters | G'MIC, FFTW |
| `OpenCV.ofx` | [openfx-opencv](https://github.com/NatronGitHub/openfx-opencv) | no (`BUILD_CV=0`) | Inpaint, segmentation | OpenCV |

### Built-in nodes (inside Natron, no bundle)

IDs `fr.inria.built-in.*`: Read and Write (meta nodes that pick an openfx-io
reader/writer), ReadQt, WriteQt, Group, Input, Output, Dot, BackDrop,
DiskCache, Roto, RotoPaint, RotoSmear, Tracker, Precomp, Viewer, JoinViews,
OneView.

### PyPlugs

Python node groups shipped in `Gui/Resources/PyPlugs/` (they use Misc and CImg
nodes): AngleBlur, DropShadow, EdgeBlur, Fill, Glow, LightWrap, PIKColor,
SplitAndJoin, ZMask, ZRemap.
