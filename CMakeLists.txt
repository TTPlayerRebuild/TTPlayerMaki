cmake_minimum_required(VERSION 3.24)
project(TtpMakiVM VERSION 0.1.0 LANGUAGES CXX RC)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "ttp_maki requires Windows/MSVC and -A Win32")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(cmake/legacy_windows.cmake)
include(cmake/size_release.cmake)
include(cmake/version.cmake)
add_library(ttp_maki SHARED src/vm.cpp src/ttp_maki.def "${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc")
add_dependencies(ttp_maki ttp_maki_version)
target_include_directories(ttp_maki PUBLIC include)
target_compile_definitions(ttp_maki PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0501)
target_compile_options(ttp_maki PRIVATE /W4 /permissive- /EHsc /utf-8)
target_link_libraries(ttp_maki PRIVATE ole32)
set_target_properties(ttp_maki PROPERTIES PREFIX "")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(TARGET ttp_maki POST_BUILD
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_legacy_imports.py"
    "$<TARGET_FILE:ttp_maki>"
    --exports "${ttplayer_yy_thunks_SOURCE_DIR}/Config/x86/5.1.2600.txt"
    --exports "${ttplayer_yy_thunks_SOURCE_DIR}/Config/x86/6.1.7600.txt"
    --report "$<TARGET_FILE_DIR:ttp_maki>/ttp_maki-imports.json" VERBATIM)
install(TARGETS ttp_maki RUNTIME DESTINATION AddIn COMPONENT Runtime)
install(FILES include/ttp_maki.h DESTINATION include)
install(FILES "${ttplayer_yy_thunks_SOURCE_DIR}/LICENSE"
  DESTINATION licenses/ttp_maki RENAME YY-Thunks-LICENSE.txt)
install(FILES docs/licenses/VC-LTL-LICENSE.txt DESTINATION licenses/ttp_maki)
