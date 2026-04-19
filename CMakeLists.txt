cmake_minimum_required(VERSION 3.20)

project(codewatch LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Ninja is the day-to-day generator; single-tree CMake still works with Unix Makefiles.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(SQLite3 REQUIRED)

set(CODEWATCH_TS_VENDOR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/tree-sitter")
set(CODEWATCH_XXH_VENDOR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/xxhash")

include(FetchContent)

# tree-sitter: prefer a vendored checkout; otherwise FetchContent-populate upstream sources
# and compile the core C library from lib/src (upstream does not rely on our stub CMake).
if(EXISTS "${CODEWATCH_TS_VENDOR}/lib/src/lib.c")
  set(CODEWATCH_TREE_SITTER_ROOT "${CODEWATCH_TS_VENDOR}")
  message(STATUS "codewatch: tree-sitter from vendored ${CODEWATCH_TREE_SITTER_ROOT}")
else()
  FetchContent_Declare(
    codewatch_tree_sitter
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
    GIT_TAG v0.24.6
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(codewatch_tree_sitter)
  set(CODEWATCH_TREE_SITTER_ROOT "${codewatch_tree_sitter_SOURCE_DIR}")
  message(STATUS "codewatch: tree-sitter from FetchContent at ${CODEWATCH_TREE_SITTER_ROOT}")
endif()

# Upstream ships one TU: lib.c #includes the other translation units.
set(CODEWATCH_TREE_SITTER_LIB_C "${CODEWATCH_TREE_SITTER_ROOT}/lib/src/lib.c")
if(NOT EXISTS "${CODEWATCH_TREE_SITTER_LIB_C}")
  message(
    FATAL_ERROR
    "tree-sitter lib.c missing at ${CODEWATCH_TREE_SITTER_LIB_C}. "
    "Vendor tree-sitter under third_party/tree-sitter or allow CMake to FetchContent it."
  )
endif()

add_library(codewatch_tree_sitter STATIC "${CODEWATCH_TREE_SITTER_LIB_C}")
target_include_directories(
  codewatch_tree_sitter
  PUBLIC
    "${CODEWATCH_TREE_SITTER_ROOT}/lib/include"
    "${CODEWATCH_TREE_SITTER_ROOT}/lib/src"
)

# xxHash: prefer vendored xxhash.c; otherwise FetchContent the upstream repo root.
if(EXISTS "${CODEWATCH_XXH_VENDOR}/xxhash.c")
  set(CODEWATCH_XXHASH_ROOT "${CODEWATCH_XXH_VENDOR}")
  message(STATUS "codewatch: xxHash from vendored ${CODEWATCH_XXHASH_ROOT}")
else()
  FetchContent_Declare(
    codewatch_xxhash
    GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
    GIT_TAG v0.8.3
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(codewatch_xxhash)
  set(CODEWATCH_XXHASH_ROOT "${codewatch_xxhash_SOURCE_DIR}")
  message(STATUS "codewatch: xxHash from FetchContent at ${CODEWATCH_XXHASH_ROOT}")
endif()

if(NOT EXISTS "${CODEWATCH_XXHASH_ROOT}/xxhash.c")
  message(
    FATAL_ERROR
    "xxhash.c missing at ${CODEWATCH_XXHASH_ROOT}/xxhash.c. "
    "Vendor xxHash under third_party/xxhash or allow CMake to FetchContent it."
  )
endif()

add_library(codewatch_xxhash STATIC "${CODEWATCH_XXHASH_ROOT}/xxhash.c")
target_include_directories(codewatch_xxhash PUBLIC "${CODEWATCH_XXHASH_ROOT}")

add_executable(codewatch main.cpp)

if(TARGET SQLite3::SQLite3)
  target_link_libraries(codewatch PRIVATE SQLite3::SQLite3)
elseif(TARGET SQLite::SQLite3)
  target_link_libraries(codewatch PRIVATE SQLite::SQLite3)
else()
  target_include_directories(codewatch PRIVATE "${SQLite3_INCLUDE_DIRS}")
  target_link_libraries(codewatch PRIVATE "${SQLite3_LIBRARIES}")
endif()

target_link_libraries(
  codewatch
  PRIVATE
    codewatch_tree_sitter
    codewatch_xxhash
)
