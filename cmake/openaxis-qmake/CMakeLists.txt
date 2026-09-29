cmake_minimum_required(VERSION 3.24)
project(OpenAxisQmake LANGUAGES CXX)
include(FetchContent)
set(OPENAXIS_BUILD_TESTS OFF CACHE BOOL "SDK tests")
set(OPENAXIS_BUILD_DEMO OFF CACHE BOOL "SDK demo")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
FetchContent_Declare(openaxis
  GIT_REPOSITORY https://github.com/rotatrix/openaxis.git
  GIT_TAG acc4da095cde6747556245b4b6c110c16b968b6b
  SOURCE_SUBDIR cpp)
FetchContent_MakeAvailable(openaxis)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/openaxis-sdk.pri" CONTENT
"INCLUDEPATH += \"${openaxis_SOURCE_DIR}/cpp/include\" \"$<JOIN:$<TARGET_PROPERTY:nlohmann_json::nlohmann_json,INTERFACE_INCLUDE_DIRECTORIES>,\" \">\"
LIBS += \"$<TARGET_FILE:openaxis>\" \"$<TARGET_FILE:openaxis_ixwebsocket>\"
")
if(NOT WIN32)
  file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/openaxis-crypto.pri" CONTENT
    "LIBS += \"$<TARGET_FILE:OpenSSL::Crypto>\"\n")
endif()
