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

# --- Front-end (React + TS + Vite) -----------------------------------------
# The front-end is built as part of the normal build (never committed): the
# `frontend` target runs the Vite build into web_build/ (gitignored), then a
# build-time step embeds that directory into a C++ byte-array source. Requires
# Node/npm. Build just the front-end with `cmake --build build --target frontend`.
find_program(NPM_EXECUTABLE npm REQUIRED)

set(WEB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/web")
set(WEB_BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/web_build")
set(FRONTEND_STAMP "${CMAKE_CURRENT_BINARY_DIR}/frontend.stamp")
file(GLOB_RECURSE WEB_SOURCES CONFIGURE_DEPENDS "${WEB_DIR}/src/*")

add_custom_command(
  OUTPUT "${FRONTEND_STAMP}"
  COMMAND ${NPM_EXECUTABLE} install --no-audit --no-fund
  COMMAND ${NPM_EXECUTABLE} run build
  COMMAND ${CMAKE_COMMAND} -E touch "${FRONTEND_STAMP}"
  WORKING_DIRECTORY "${WEB_DIR}"
  DEPENDS ${WEB_SOURCES}
          "${WEB_DIR}/package.json" "${WEB_DIR}/vite.config.ts"
          "${WEB_DIR}/tsconfig.json" "${WEB_DIR}/index.html"
  COMMENT "Building front-end (web/ -> web_build/)"
  USES_TERMINAL
  VERBATIM)
add_custom_target(frontend DEPENDS "${FRONTEND_STAMP}")

# Embed web_build/ into a generated C++ source (served by filename from /static/).
set(SQLINSITE_EMBEDDED_ASSETS "${CMAKE_CURRENT_BINARY_DIR}/embedded_assets_generated.cpp")
add_custom_command(
  OUTPUT "${SQLINSITE_EMBEDDED_ASSETS}"
  COMMAND ${CMAKE_COMMAND} -DIN_DIR=${WEB_BUILD_DIR} -DOUT=${SQLINSITE_EMBEDDED_ASSETS}
          -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_embedded_assets.cmake"
  DEPENDS "${FRONTEND_STAMP}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_embedded_assets.cmake"
  COMMENT "Embedding front-end assets (web_build/ -> binary)"
  VERBATIM)

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
  src/visualize/query_augment.cpp
  src/visualize/cell_page_map.cpp
  src/visualize/page_content.cpp
  src/visualize/query_engine.cpp
  src/visualize/visualize_command.cpp
  ${SQLINSITE_EMBEDDED_ASSETS}
)
target_include_directories(sqlinsite_core PUBLIC src)
target_link_libraries(sqlinsite_core PUBLIC
  sqlite3 nlohmann_json::nlohmann_json httplib::httplib)

# --- CLI binary ---
add_executable(sqlinsite src/main.cpp)
target_link_libraries(sqlinsite PRIVATE sqlinsite_core)

# Build just the native backend (pulls the front-end only if it is out of date):
#   cmake --build build --target backend
add_custom_target(backend DEPENDS sqlinsite)

# --- Overhead benchmark (dev tool; not part of ctest) ---
add_executable(sqlinsite_bench bench/overhead.cpp)
target_link_libraries(sqlinsite_bench PRIVATE sqlinsite_core)

enable_testing()
add_subdirectory(tests)
