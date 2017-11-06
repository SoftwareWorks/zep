# ImGui
SET (IMGUI_SOURCE 
    m3rdparty/imgui/examples/sdl_opengl3_example/imgui_impl_sdl_gl3.cpp
    m3rdparty/imgui/examples/libs/gl3w/GL/gl3w.c
    m3rdparty/imgui/imgui_demo.cpp
    m3rdparty/imgui/imgui_draw.cpp
    m3rdparty/imgui/imgui.cpp)

SET (IMGUI_INCLUDE 
    m3rdparty/imgui/imgui.h
    m3rdparty/imgui/imgui_dock.h
    m3rdparty/imgui/imgui_orient.h)

LIST(APPEND M3RDPARTY_SOURCE 
    ${IMGUI_SOURCE}
    )

LIST(APPEND M3RDPARTY_INCLUDE
    m3rdparty
    ${CMAKE_BINARY_DIR}
    m3rdparty/imgui
    m3rdparty/imgui/examples/libs/gl3w
    m3rdparty/sdl/include
    m3rdparty/sdl
    m3rdparty/threadpool
    )

SET (M3RDPARTY_DIR ${CMAKE_CURRENT_LIST_DIR})

INCLUDE(ExternalProject)
ExternalProject_Add(
  sdl2
  PREFIX "m3rdparty"
  CMAKE_ARGS "" #-DSDL_SHARED=OFF"
  SOURCE_DIR "${M3RDPARTY_DIR}/sdl"
  TEST_COMMAND ""
  INSTALL_COMMAND "" 
  INSTALL_DIR ""
)

IF (TARGET_PC)
LIST(APPEND PLATFORM_LINKLIBS 
    SDL2
    SDL2main
)
ENDIF()
IF (TARGET_MAC)
LIST(APPEND PLATFORM_LINKLIBS 
    sdl2-2.0
    sdl2main
)
ENDIF()
IF (TARGET_LINUX)
LIST(APPEND PLATFORM_LINKLIBS 
    SDL2-2.0
    SDL2main
)
ENDIF()

LINK_DIRECTORIES(${CMAKE_BINARY_DIR}/m3rdparty/src/sdl2-build)
SOURCE_GROUP ("m3rdparty\\imgui" REGULAR_EXPRESSION "(imgui)+.*")
SOURCE_GROUP ("m3rdparty\\glfw" REGULAR_EXPRESSION "(glfw)+.*")

CONFIGURE_FILE(${CMAKE_CURRENT_LIST_DIR}/cmake/config_shared.h.cmake ${CMAKE_BINARY_DIR}/config_shared.h)
