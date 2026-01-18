cmake_minimum_required(VERSION 3.10)
project(MouseLine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# === 配置 SDL3 路径 ===
# 这里硬编码了你之前使用的路径，如果移动了库位置需要修改这里
set(SDL3_ROOT_PATH "D:/SDL3-3.4.0/x86_64-w64-mingw32")
set(SDL3_INCLUDE_DIR "${SDL3_ROOT_PATH}/include")
set(SDL3_LIBRARY_DIR "${SDL3_ROOT_PATH}/lib")

# 包含头文件
include_directories(${SDL3_INCLUDE_DIR})

# 添加库目录
link_directories(${SDL3_LIBRARY_DIR})

# === 定义可执行文件 ===
# 将源代码和资源文件(.rc)一起编译
add_executable(MouseLine mouse_line.cpp resources.rc)

# === 链接设置 ===
target_link_libraries(MouseLine PRIVATE SDL3)

# Windows 平台特定设置
if(WIN32)
    # -mwindows: 隐藏控制台窗口
    # -static-*: 静态链接 C++ 运行时，避免依赖额外的 mingw DLL
    target_link_options(MouseLine PRIVATE -mwindows -static-libgcc -static-libstdc++)
endif()

# === 自动复制资源 ===
# 编译完成后，自动将 图标 和 SDL3.dll 复制到 exe 所在目录
add_custom_command(TARGET MouseLine POST_BUILD
    # 复制图标
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/philia093_smile.bmp"
        $<TARGET_FILE_DIR:MouseLine>
    # 复制 SDL3.dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SDL3_ROOT_PATH}/bin/SDL3.dll"
        $<TARGET_FILE_DIR:MouseLine>
    COMMENT "Copying assets and DLLs..."
)
