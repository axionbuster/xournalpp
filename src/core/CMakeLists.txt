cmake_minimum_required(VERSION 3.12)
cmake_policy(VERSION 3.12)

file(GLOB_RECURSE xournalpp_sources *.cpp *.h)

if(NOT ENABLE_AUDIO)
    list(FILTER xournalpp_sources EXCLUDE REGEX "${PROJECT_SOURCE_DIR}/src/core/audio/.*" )
    list(FILTER xournalpp_sources EXCLUDE REGEX "${PROJECT_SOURCE_DIR}/src/core/control/AudioController.cpp" )
endif()
set(xournalpp_include_dirs "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(xournalpp-core STATIC ${xournalpp_sources})
target_compile_features(xournalpp-core PUBLIC ${PROJECT_CXX_FEATURES})
target_compile_features(xournalpp-core PUBLIC cxx_std_20)
target_link_libraries(xournalpp-core
        PRIVATE xoj::util ${xournalpp_LDFLAGS}
        PUBLIC xoj::defaults xoj::external_modules
        )
target_include_directories(xournalpp-core PUBLIC ${xournalpp_include_dirs})

# Parsing GTK's headers costs about six tenths of a second, and there are 357 files here that all
# start by doing it. Reading them once into a precompiled header and reusing that is the single
# biggest lever on how long an edit takes to see: without it, touching a widely included header like
# Control.h means paying for GTK 107 times over.
#
# PRIVATE on purpose -- nothing that links against this library should inherit our header set.
target_precompile_headers(xournalpp-core PRIVATE
        <gtk/gtk.h>
        <algorithm>
        <cstddef>
        <cstdint>
        <functional>
        <map>
        <memory>
        <optional>
        <string>
        <string_view>
        <utility>
        <vector>
        )

add_library(xoj::core ALIAS xournalpp-core)
