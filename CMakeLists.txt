cmake_minimum_required(VERSION 3.10.0)
project(gstreamer-tutorials VERSION 0.1.0 LANGUAGES C)

find_package(PkgConfig REQUIRED)

pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET gstreamer-1.0)


file(GLOB TUTORIAL_SOURCES "src/basic-tutorial-*.c")

if(NOT TUTORIAL_SOURCES)
    message(WARNING "No tutorial source files found matching the pattern.")
endif()

foreach(SOURCE_FILE ${TUTORIAL_SOURCES})
    # 소스 파일 이름에서 확장자를 제외한 기본 이름 추출
    get_filename_component(EXECUTABLE_NAME ${SOURCE_FILE} NAME_WE) # NAME_WE: Name Without Extension

    # 메시지 출력
    message(STATUS "Configuring tutorial: ${EXECUTABLE_NAME} from ${SOURCE_FILE}")

    # 실행 파일 정의
    add_executable(${EXECUTABLE_NAME} ${SOURCE_FILE})

    # GStreamer 라이브러리 링크
    target_link_libraries(${EXECUTABLE_NAME} PRIVATE PkgConfig::GSTREAMER)
endforeach()