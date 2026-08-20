cmake_minimum_required(VERSION 3.22.1)
project(native)

# ============================
# C++ 标准
# ============================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================
# 库目录变量
# ============================
set(FFMPEG_LIB_DIR "${CMAKE_SOURCE_DIR}/libs/${ANDROID_ABI}")
set(JNI_LIBS_DIR "${CMAKE_SOURCE_DIR}/../jniLibs/${ANDROID_ABI}")

# avcpp 源码目录（需将 avcpp 克隆到 3rdparty/avcpp）
set(AVCPP_DIR "${CMAKE_SOURCE_DIR}/3rdparty/avcpp")

# YsPlayer 源码目录
set(YSPLAYER_DIR "${CMAKE_SOURCE_DIR}/YsPlayer")

# ============================
# avcpp 源文件收集（必须在 add_library 之前收集）
# ============================
aux_source_directory("${AVCPP_DIR}/src/avcpp" AVCPP_SOURCES)

# ============================
# YsPlayer 源文件收集
# ============================
aux_source_directory("${YSPLAYER_DIR}" YSPLAYER_SOURCES)

message(STATUS "=== YsPlayer 源文件 ===")
foreach(src ${YSPLAYER_SOURCES})
    message(STATUS "  ${src}")
endforeach()

# 调试输出
message(STATUS "=== avcpp 源文件目录: ${AVCPP_DIR}/src/avcpp ===")
message(STATUS "=== 找到 ${AVCPP_SOURCES} 个 avcpp 源文件 ===")
foreach(src ${AVCPP_SOURCES})
    message(STATUS "  ${src}")
endforeach()

if(NOT AVCPP_SOURCES)
    message(FATAL_ERROR
            "avcpp 源文件未找到！路径: ${AVCPP_DIR}/src\n"
            "请执行:\n"
            "  cd app/src/main/cpp\n"
            "  git clone --depth 1 https://github.com/h4tr3d/avcpp.git 3rdparty/avcpp\n"
            "并将 avconfig.h 放到 app/src/main/cpp/avconfig/avcpp/ 目录下"
    )
endif()

# ============================
# ★ 先创建 target（必须在使用 target_xxx 之前）
# ============================
add_library(native SHARED
        native-lib.cpp
        functions.cpp
        FFmpeg.cpp
        ${AVCPP_SOURCES}
        ${YSPLAYER_SOURCES}
)

# ============================
# ★ target_xxx 必须在 add_library 之后
# ============================

# 添加源文件（如果后续需要追加）
# target_sources(native PRIVATE ...)  # 在这里用才对

# 头文件搜索路径
target_include_directories(native PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/ffmpeg
        ${AVCPP_DIR}/src
        ${CMAKE_SOURCE_DIR}/avconfig
        ${YSPLAYER_DIR}
)

# ============================
# FFmpeg 静态库
# ============================
add_library(avutil STATIC IMPORTED)
set_target_properties(avutil PROPERTIES
        IMPORTED_LOCATION "${FFMPEG_LIB_DIR}/libavutil.a"
)

add_library(swresample STATIC IMPORTED)
set_target_properties(swresample PROPERTIES
        IMPORTED_LOCATION "${FFMPEG_LIB_DIR}/libswresample.a"
)

add_library(avcodec STATIC IMPORTED)
set_target_properties(avcodec PROPERTIES
        IMPORTED_LOCATION "${FFMPEG_LIB_DIR}/libavcodec.a"
)

add_library(avformat STATIC IMPORTED)
set_target_properties(avformat PROPERTIES
        IMPORTED_LOCATION "${FFMPEG_LIB_DIR}/libavformat.a"
)

add_library(avfilter STATIC IMPORTED)
set_target_properties(avfilter PROPERTIES
        IMPORTED_LOCATION "${FFMPEG_LIB_DIR}/libavfilter.a"
)

add_library(swscale STATIC IMPORTED)
set_target_properties(swscale PROPERTIES
        IMPORTED_LOCATION "${FFMPEG_LIB_DIR}/libswscale.a"
)

# ============================
# 第三方 SHARED 库
# ============================
add_library(kgm_decoder SHARED IMPORTED)
set_target_properties(kgm_decoder PROPERTIES
        IMPORTED_LOCATION "${JNI_LIBS_DIR}/libkgm_decoder.so"
        IMPORTED_NO_SONAME TRUE
)

add_library(z SHARED IMPORTED)
set_target_properties(z PROPERTIES
        IMPORTED_LOCATION "${JNI_LIBS_DIR}/libz.so"
        IMPORTED_NO_SONAME TRUE
)

add_library(c++_shared SHARED IMPORTED)
set_target_properties(c++_shared PROPERTIES
        IMPORTED_LOCATION "${JNI_LIBS_DIR}/libc++_shared.so"
        IMPORTED_NO_SONAME TRUE
)

add_library(drm SHARED IMPORTED)
set_target_properties(drm PROPERTIES
        IMPORTED_LOCATION "${JNI_LIBS_DIR}/libdrm.so"
        IMPORTED_NO_SONAME TRUE
)

add_library(lzma SHARED IMPORTED)
set_target_properties(lzma PROPERTIES
        IMPORTED_LOCATION "${JNI_LIBS_DIR}/liblzma.so.5"
        IMPORTED_NO_SONAME TRUE
)

# ============================
# 链接
# ============================
target_link_libraries(native
        # 第三方动态库
        kgm_decoder
        drm
        lzma
        z
        c++_shared

        # FFmpeg 静态库（注意顺序！依赖关系从上到下）
        avformat      # 依赖 avcodec
        avfilter      # 依赖 swscale, avutil
        avcodec       # 依赖 swresample, avutil
        swscale       # 依赖 avutil
        swresample    # 依赖 avutil
        avutil        # 基础库，放最后

        # 系统库
        android
        log
        OpenSLES
)
