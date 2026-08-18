if (POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif ()

find_package(Boost 1.83 REQUIRED COMPONENTS
    atomic chrono date_time filesystem log nowide regex thread
)
find_package(Eigen3 3.3.7 REQUIRED)
find_package(TBB REQUIRED)
find_package(PNG REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Catch2 3.8 REQUIRED)

configure_file(
    ${PROJECT_SOURCE_DIR}/src/libslic3r/libslic3r_version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/libslic3r_version.h
    @ONLY
)

add_library(dlp_core STATIC
    ${PROJECT_SOURCE_DIR}/src/libslic3r/DLP/DLPPrint.cpp
    ${PROJECT_SOURCE_DIR}/src/libslic3r/DLP/DLPRasterizer.cpp
    ${PROJECT_SOURCE_DIR}/src/libslic3r/DLP/DLPDirectoryArchive.cpp
    ${PROJECT_SOURCE_DIR}/src/libslic3r/DLP/DLPBinarySTL.cpp
    ${PROJECT_SOURCE_DIR}/src/libslic3r/PNGReadWrite.cpp
)

target_compile_features(dlp_core PUBLIC cxx_std_17)
target_compile_definitions(dlp_core PRIVATE SLIC3R_DLP_CORE_ONLY)
if (WIN32)
    target_compile_definitions(dlp_core PUBLIC NOMINMAX _USE_MATH_DEFINES BOOST_ALL_NO_LIB)
endif ()
target_include_directories(dlp_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}
        ${PROJECT_SOURCE_DIR}/bundled_deps/semver
        ${PROJECT_SOURCE_DIR}/bundled_deps/localesutils
        ${PROJECT_SOURCE_DIR}/bundled_deps/fast_float
)
if (WIN32)
    target_link_directories(dlp_core PUBLIC "${CMAKE_PREFIX_PATH}/lib")
endif ()
target_link_libraries(dlp_core
    PUBLIC
        TBB::tbbmalloc
        TBB::tbb
        Eigen3::Eigen
        Boost::headers
    PRIVATE
        Boost::filesystem
        Boost::log
        Boost::nowide
        PNG::PNG
        nlohmann_json::nlohmann_json
)

add_executable(dlp_print_tests
    ${PROJECT_SOURCE_DIR}/tests/dlp_print/dlp_print_tests.cpp
)
target_compile_definitions(dlp_print_tests PRIVATE SLIC3R_DLP_CORE_ONLY)
target_link_libraries(dlp_print_tests PRIVATE dlp_core Catch2::Catch2WithMain)

add_test(NAME dlp_print_tests COMMAND dlp_print_tests)

add_executable(dlp-slicer ${PROJECT_SOURCE_DIR}/src/dlp-slicer.cpp)
target_link_libraries(dlp-slicer PRIVATE dlp_core)
