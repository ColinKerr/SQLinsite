cmake_minimum_required(VERSION 3.24)

project(SQLinsite LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(FetchContent)
include(embed_assets)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.12.0
)
FetchContent_MakeAvailable(nlohmann_json)

# cpp-httplib for `visualize serve` (header-only; no TLS/compression needed).
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.48.0
)
FetchContent_MakeAvailable(httplib)

# Embed the visualize front-end assets into the binary (plain canvas JS, no D3).
sqlinsite_embed_assets(SQLINSITE_EMBEDDED_ASSETS
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/index.html"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/app.js"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/style.css"
)

# --- Vendored SQLite (see third_party/sqlite/README.md, version 3.53.3) ---
add_library(sqlite3 STATIC third_party/sqlite/sqlite3.c)
target_include_directories(sqlite3 PUBLIC third_party/sqlite)
target_compile_definitions(sqlite3 PRIVATE
  SQLITE_ENABLE_COLUMN_METADATA
  SQLITE_THREADSAFE=1
  SQLITE_ENABLE_DBPAGE_VTAB
)
if(NOT MSVC)
  target_compile_options(sqlite3 PRIVATE -w)
endif()

# --- Core library (everything except the CLI entry point) ---
add_library(sqlinsite_core STATIC
  src/common/cli.cpp
  src/profile/vfs_shim.cpp
  src/profile/statements_file.cpp
  src/profile/csv_writer.cpp
  src/profile/profile_command.cpp
  src/map/sqlite_format.cpp
  src/map/db_file.cpp
  src/map/page_parser.cpp
  src/map/map_builder.cpp
  src/map/map_writer.cpp
  src/map/map_command.cpp
  src/visualize/profile_reader.cpp
  src/visualize/run_coalesce.cpp
  src/visualize/map_db.cpp
  src/visualize/visualize_command.cpp
  ${SQLINSITE_EMBEDDED_ASSETS}
)
target_include_directories(sqlinsite_core PUBLIC src)
target_link_libraries(sqlinsite_core PUBLIC
  sqlite3 nlohmann_json::nlohmann_json httplib::httplib)

# --- CLI binary ---
add_executable(sqlinsite src/main.cpp)
target_link_libraries(sqlinsite PRIVATE sqlinsite_core)

# --- Overhead benchmark (dev tool; not part of ctest) ---
add_executable(sqlinsite_bench bench/overhead.cpp)
target_link_libraries(sqlinsite_bench PRIVATE sqlinsite_core)

enable_testing()
add_subdirectory(tests)
