cmake_minimum_required(VERSION 3.16)
project(dlp_engine_module LANGUAGES CXX)

set(DLP_ENGINE_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/libslic3r/DLP/DLPPrint.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/libslic3r/DLP/DLPGeometrySlicer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/libslic3r/DLP/DLPRasterizer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/libslic3r/DLP/DLPDirectoryArchive.cpp
)

function(dlp_engine_attach HOST_TARGET HOST_SOURCE_DIR)
    if (NOT TARGET ${HOST_TARGET})
        message(FATAL_ERROR "DLP host target does not exist: ${HOST_TARGET}")
    endif ()
    target_sources(${HOST_TARGET} PRIVATE ${DLP_ENGINE_SOURCES})
    target_include_directories(${HOST_TARGET} PRIVATE
        ${HOST_SOURCE_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/src
    )
    target_compile_features(${HOST_TARGET} PRIVATE cxx_std_17)
    message(STATUS "Attached independent DLP engine to ${HOST_TARGET}")
endfunction()
