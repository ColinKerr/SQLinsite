include(FetchContent)

FetchContent_Declare(
  doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG v2.5.2
)
FetchContent_MakeAvailable(doctest)

add_executable(sqlinsite_tests
  test_main.cpp
  test_cli.cpp
  test_statements_file.cpp
  test_csv_writer.cpp
  test_aggregating_sink.cpp
  test_page_index.cpp
  test_profile_integration.cpp
  test_sqlite_format.cpp
  test_db_file.cpp
  test_map_integration.cpp
  test_profile_reader.cpp
  test_run_coalesce.cpp
  test_query_augment.cpp
  test_cell_page_map.cpp
  test_page_content.cpp
  test_query_engine.cpp
  test_visualize_server.cpp
)
target_link_libraries(sqlinsite_tests PRIVATE sqlinsite_core doctest::doctest)


# Test fixtures are written to the build dir at test time.
target_compile_definitions(sqlinsite_tests PRIVATE
  SQLINSITE_TEST_TMPDIR="${CMAKE_CURRENT_BINARY_DIR}")

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(sqlinsite_tests)
