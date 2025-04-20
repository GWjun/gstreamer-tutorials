cmake_minimum_required(VERSION 3.10.0)
project(gstreamer-tutorials VERSION 0.1.0 LANGUAGES C)

find_package(PkgConfig REQUIRED)

# pkg_check_modules에 IMPORTED_TARGET 키워드를 추가
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET gstreamer-1.0)

add_executable(gstreamer-tutorials basic-tutorial-1.c)

# target_link_libraries에 PkgConfig::GSTREAMER 타겟을 직접 연결
target_link_libraries(gstreamer-tutorials PRIVATE PkgConfig::GSTREAMER)