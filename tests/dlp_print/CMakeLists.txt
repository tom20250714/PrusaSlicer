set(_TEST_NAME dlp_print)

add_executable(${_TEST_NAME}_tests
    dlp_print_tests.cpp
)

target_link_libraries(${_TEST_NAME}_tests PRIVATE libslic3r test_common)
set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

if (WIN32)
    prusaslicer_copy_dlls(${_TEST_NAME}_tests)
endif ()

add_test(NAME ${_TEST_NAME}_tests COMMAND ${_TEST_NAME}_tests ${CATCH_EXTRA_ARGS})
