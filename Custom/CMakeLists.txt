add_library(NatronCustom STATIC
    server/HttpServer.h
    server/HttpServer.cpp
    state/StateStore.h
    state/StateStore.cpp
    state/Json.h
    state/Json.cpp
)
target_link_libraries(NatronCustom
    PUBLIC
        Qt5::Core
        Qt5::Network
)
target_include_directories(NatronCustom
    PUBLIC
        ..
)
