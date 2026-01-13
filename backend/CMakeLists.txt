cmake_minimum_required(VERSION 3.10)
project(live2mp3 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 包含 Conan 生成的 Toolchain (实际由 `conan install` 自动注入，此处主要是声明查找包)
find_package(Drogon REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(fmt REQUIRED)

# 包含头文件目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/controllers)

# 自动查找源文件
file(GLOB_RECURSE SRC_FILES 
    "*.cc"
    "*.cpp"
)

add_executable(live2mp3 ${SRC_FILES})

# 链接依赖 (使用 Conan 提供的 Target 名称)
target_link_libraries(live2mp3 PRIVATE 
    Drogon::Drogon
    nlohmann_json::nlohmann_json
    fmt::fmt
)
