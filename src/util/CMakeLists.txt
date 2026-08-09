cmake_minimum_required(VERSION 3.12)
cmake_policy(VERSION 3.12)

# Util is pretty small so GLOB_RECURSE is sufficient
unset(util_SOURCES)
file(GLOB_RECURSE util_SOURCES *.cpp *.h)

add_library(util STATIC ${util_SOURCES})
target_link_libraries(util PUBLIC xoj::defaults xoj::external_modules)
target_compile_features(util PUBLIC ${PROJECT_CXX_FEATURES})
target_compile_features(util PUBLIC cxx_std_20)
target_include_directories(util PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# Same reasoning as in src/core: these files all read GTK's headers too. See that CMakeLists.
target_precompile_headers(util PRIVATE
        <gtk/gtk.h>
        <algorithm>
        <memory>
        <string>
        <vector>
        )

add_library(xoj::util ALIAS util)
