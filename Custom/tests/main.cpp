#include <QCoreApplication>

#include "gtest/gtest.h"

int main(int argc, char **argv)
{
#ifdef FAKE_NDI_DIR
  // Ndi::load() then finds the fake NDI runtime (see FakeNdi.cpp) first.
  qputenv("NDI_RUNTIME_DIR_V6", FAKE_NDI_DIR);
#endif
  ::testing::InitGoogleTest(&argc, argv);
  QCoreApplication app(argc, argv);
  return RUN_ALL_TESTS();
}
