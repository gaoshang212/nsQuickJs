cmake_minimum_required(VERSION 3.24)
project(nsQuickJs VERSION 0.1.0 LANGUAGES C CXX)
# 插件 ABI 与兼容补丁面向 Windows/MSVC，拒绝未经支持的工具链。
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "请使用 Windows 和 Visual Studio 2022 或更新的 MSVC 工具链")
endif()
# 宿主使用 C++17；静态 CRT 使最终安装器无需部署额外 VC 运行库。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
include(FetchContent)
# 固定 QuickJS-NG 提交和归档校验值，避免上游移动标签导致构建漂移。
FetchContent_Declare(quickjs
  URL https://codeload.github.com/quickjs-ng/quickjs/tar.gz/6d46d07d04041b40f4f49eaa7fdebe44c314c699
  URL_HASH SHA256=a62cf1ff7d6d2f82b90a2d247a57e9eb56b81c03feb1f372a53923426e358cb0
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
# 仅嵌入引擎，不构建 libc 宿主扩展、示例或上游安装目标。
set(QJS_BUILD_LIBC OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(QJS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(quickjs)
# 在构建目录修补模块引用释放顺序，保留下载的上游源码原样。
include(cmake/PatchQuickJs.cmake)
target_compile_options(qjs PRIVATE /utf-8)
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
  # x86 MSVC 缺少 64 位位扫描 intrinsic，仅在该架构强制包含兼容实现。
  target_compile_options(qjs PRIVATE "/FI${CMAKE_CURRENT_SOURCE_DIR}/src/quickjs_msvc_x86.h")
endif()
# 运行时独立成静态库，供 DLL 和原生运行时测试复用。
add_library(nsq_runtime STATIC src/runtime.cpp src/paths.cpp src/files.cpp)
target_include_directories(nsq_runtime PUBLIC src)
target_link_libraries(nsq_runtime PUBLIC qjs)
target_compile_definitions(nsq_runtime PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(nsq_runtime PRIVATE /W4 /utf-8 /EHsc)
# DLL 只负责 NSIS ABI，.def 确保两种架构导出相同的公共名称。
add_library(nsQuickJs SHARED src/plugin.cpp src/plugin.def)
target_include_directories(nsQuickJs PRIVATE third_party/nsis)
target_link_libraries(nsQuickJs PRIVATE nsq_runtime)
target_compile_options(nsQuickJs PRIVATE /W4 /utf-8 /EHsc)
# NSQ_TARGET 对应 NSIS 的插件目录名，不直接采用 VS 平台名。
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(NSQ_TARGET amd64-unicode)
else()
  set(NSQ_TARGET x86-unicode)
endif()
set_target_properties(nsQuickJs PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${NSQ_TARGET}")
# 两个原生测试分别检查引擎行为与真实 DLL 的栈协议；安装器测试由脚本运行。
include(CTest)
if(BUILD_TESTING)
  add_executable(runtime_tests tests/runtime_tests.cpp tests/file_tests.cpp)
  target_link_libraries(runtime_tests PRIVATE nsq_runtime)
  target_compile_options(runtime_tests PRIVATE /utf-8 /EHsc)
  add_test(NAME runtime COMMAND runtime_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
  set_tests_properties(runtime PROPERTIES TIMEOUT 60)
  add_executable(plugin_tests tests/plugin_tests.cpp)
  target_include_directories(plugin_tests PRIVATE third_party/nsis)
  target_compile_definitions(plugin_tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(plugin_tests PRIVATE /utf-8 /EHsc)
  add_test(NAME plugin COMMAND plugin_tests "$<TARGET_FILE:nsQuickJs>" "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
  set_tests_properties(plugin PROPERTIES TIMEOUT 60)
endif()
# 两种 Release 构建可安装到同一前缀；公共文档、示例和许可证一起分发。
install(TARGETS nsQuickJs RUNTIME DESTINATION "Plugins/${NSQ_TARGET}")
install(DIRECTORY include/ DESTINATION Include)
install(DIRECTORY examples/ DESTINATION Examples)
install(DIRECTORY docs/ DESTINATION docs)
install(FILES README.md LICENSE THIRD_PARTY_NOTICES.md DESTINATION .)
install(FILES "${quickjs_SOURCE_DIR}/LICENSE" DESTINATION licenses RENAME QuickJS-NG.txt)
install(FILES third_party/nsis/COPYING DESTINATION licenses RENAME NSIS.txt)
