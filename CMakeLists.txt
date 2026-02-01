cmake_minimum_required(VERSION 3.20)

project(git-remote-xcrypt LANGUAGES CXX)

# 要求 GCC >= 14 (g++) 以支持本项目使用的 C++23 特性
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS "14")
    message(FATAL_ERROR "GCC (g++) >= 14 is required. Detected: ${CMAKE_CXX_COMPILER_VERSION}")
  endif()
endif()

# --- 构建选项 (匹配旧 Makefile: -std=gnu++23 -O0 -g) ---
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# --- 目标 ---
file(GLOB SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(git-remote-xcrypt ${SRCS})
target_include_directories(git-remote-xcrypt PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

# --- 依赖项 (等价于: -lgit2 -lbzip3 -lcrypto -lboost_system -lboost_filesystem) ---
find_package(PkgConfig REQUIRED)

pkg_check_modules(LIBGIT2 REQUIRED IMPORTED_TARGET libgit2)
pkg_check_modules(BZIP3 REQUIRED IMPORTED_TARGET bzip3>=1.4.1)

# 从 pkg-config 版本字符串派生 LIBBZ3_VERSION，例如 2.5.3 -> 20503
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

find_package(OpenSSL REQUIRED)          # 提供 OpenSSL::Crypto

# 使用 CMP0167 策略查找 Boost
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

# --- 安装 (等价于: install -C git-remote-xcrypt /usr/bin/) ---
install(TARGETS git-remote-xcrypt RUNTIME DESTINATION bin)

# --- 卸载目标 (CMake 未内置) ---
if (NOT TARGET uninstall)
  # 获取目标输出名称（默认为目标名）
  get_target_property(TARGET_OUTPUT_NAME git-remote-xcrypt OUTPUT_NAME)
  if (NOT TARGET_OUTPUT_NAME)
    set(TARGET_OUTPUT_NAME "git-remote-xcrypt")
  endif()

  # 构建完整的安装路径
  set(INSTALLED_FILE "${CMAKE_INSTALL_PREFIX}/bin/${TARGET_OUTPUT_NAME}${CMAKE_EXECUTABLE_SUFFIX}")

  # 创建卸载脚本
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake" "
if(EXISTS \"${INSTALLED_FILE}\" OR IS_SYMLINK \"${INSTALLED_FILE}\")
  file(REMOVE \"${INSTALLED_FILE}\")
  message(STATUS \"Removed: ${INSTALLED_FILE}\")
else()
  message(STATUS \"File not found: ${INSTALLED_FILE}\")
endif()
")

  add_custom_target(uninstall
    COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    COMMENT "Uninstalling"
  )
endif()
