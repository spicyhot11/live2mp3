cmake_minimum_required(VERSION 3.10)
project(live2mp3 CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 包含 Conan 生成的 Toolchain
find_package(Drogon REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(fmt REQUIRED)
find_package(tomlplusplus REQUIRED)

# ==========================================
# 标准化输出路径
# ==========================================
# 将所有可执行文件输出到项目根目录下的 bin/ 文件夹
# CMAKE_SOURCE_DIR 是 backend 目录，这里假设项目根目录是 backend 的上一级
get_filename_component(PROJECT_ROOT ${CMAKE_SOURCE_DIR} DIRECTORY)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_ROOT}/bin)

# 包含头文件目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/controllers)

# 显式指定源文件查找范围，防止抓取到 build 目录下的临时文件
file(GLOB SRC_FILES_ROOT "*.cc" "*.cpp")
file(GLOB_RECURSE SRC_FILES_CONTROLLERS "controllers/*.cc" "controllers/*.cpp")
file(GLOB_RECURSE SRC_FILES_SERVICES "services/*.cc" "services/*.cpp")
file(GLOB_RECURSE SRC_FILES_UTILS "utils/*.cc" "utils/*.cpp")
file(GLOB_RECURSE SRC_FILES_REPOS "repos/*.cc" "repos/*.cpp")
file(GLOB_RECURSE SRC_FILES_MODELS "models/*.cc" "models/*.cpp")

# 合并源文件列表
set(SRC_FILES ${SRC_FILES_ROOT} ${SRC_FILES_CONTROLLERS} ${SRC_FILES_SERVICES} ${SRC_FILES_UTILS} ${SRC_FILES_REPOS} ${SRC_FILES_MODELS})

add_executable(live2mp3 ${SRC_FILES})

# 链接依赖 (使用 Conan 提供的 Target 名称)
find_package(SQLite3 REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(xxHash REQUIRED)

target_link_libraries(live2mp3 PRIVATE 
    Drogon::Drogon
    nlohmann_json::nlohmann_json
    fmt::fmt
    SQLite::SQLite3
    OpenSSL::SSL
    OpenSSL::Crypto
    tomlplusplus::tomlplusplus
    xxHash::xxhash
)