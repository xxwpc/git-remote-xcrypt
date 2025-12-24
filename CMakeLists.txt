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

find_package(OpenSSL REQUIRED)          # provides OpenSSL::Crypto
find_package(Boost REQUIRED COMPONENTS system filesystem)

target_compile_options(git-remote-xcrypt PRIVATE
   -O2
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