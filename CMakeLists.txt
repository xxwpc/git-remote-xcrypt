cmake_minimum_required(VERSION 3.20)

project(git-remote-xcrypt LANGUAGES CXX)

# Require GCC >= 14 (g++) for C++23 support level used by this project.
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS "14")
    message(FATAL_ERROR "GCC (g++) >= 14 is required. Detected: ${CMAKE_CXX_COMPILER_VERSION}")
  endif()
endif()

# --- build options (match old Makefile: -std=gnu++23 -O0 -g) ---
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# --- target ---
file(GLOB SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(git-remote-xcrypt ${SRCS})
target_include_directories(git-remote-xcrypt PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

# --- dependencies (equivalent to: -lgit2 -lbzip3 -lcrypto -lboost_system -lboost_filesystem) ---
find_package(PkgConfig REQUIRED)

pkg_check_modules(LIBGIT2 REQUIRED IMPORTED_TARGET libgit2)
pkg_check_modules(BZIP3 REQUIRED IMPORTED_TARGET bzip3>=1.4.1)

# Derive LIBBZ3_VERSION from pkg-config version string, e.g. 2.5.3 -> 20503
execute_process(
  COMMAND "${PKG_CONFIG_EXECUTABLE}" --modversion bzip3
  OUTPUT_VARIABLE BZIP3_MODVERSION
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if (NOT BZIP3_MODVERSION)
  message(FATAL_ERROR "Failed to query bzip3 version via pkg-config.")
endif()

string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" _ "${BZIP3_MODVERSION}")
if (NOT CMAKE_MATCH_1)
  message(FATAL_ERROR "Unexpected bzip3 version format: '${BZIP3_MODVERSION}'")
endif()

math(EXPR LIBBZ3_VERSION "${CMAKE_MATCH_1} * 10000 + ${CMAKE_MATCH_2} * 100 + ${CMAKE_MATCH_3}")

find_package(OpenSSL REQUIRED)          # provides OpenSSL::Crypto

# Find Boost with CMP0167 handling
cmake_policy(SET CMP0167 NEW)
find_package(Boost QUIET)
if (NOT Boost_FOUND)
   cmake_policy(SET CMP0167 OLD)
   find_package( Boost QUIET )
   if (NOT Boost_FOUND)
      message(FATAL_ERROR "Boost not found via CMP0167=NEW nor CMP0167=OLD. Please install Boost (dev) and ensure CMake can find it.")
   endif()
endif()

find_package(Boost REQUIRED COMPONENTS context system filesystem)

# boost 1.70.0 之后, 才有 process 独立库
if (Boost_VERSION VERSION_GREATER_EQUAL "1.70.0")
  find_package(Boost REQUIRED COMPONENTS process)
  target_link_libraries(git-remote-xcrypt PRIVATE Boost::process)
endif()

target_compile_definitions(git-remote-xcrypt PRIVATE
  LIBBZ3_VERSION=${LIBBZ3_VERSION}
)

target_link_libraries(git-remote-xcrypt PRIVATE
  PkgConfig::LIBGIT2
  PkgConfig::BZIP3
  OpenSSL::Crypto
  Boost::system
  Boost::filesystem
)

# --- install (equivalent to: install -C git-remote-xcrypt /usr/bin/) ---
install(TARGETS git-remote-xcrypt RUNTIME DESTINATION bin)
