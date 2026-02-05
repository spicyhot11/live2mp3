#!/bin/bash
set -e

# 颜色定义
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${GREEN}==> 开始 macOS (M4) 构建流程...${NC}"

# 1. 检查 Homebrew
if ! command -v brew &> /dev/null; then
    echo -e "${RED}错误: 未找到 Homebrew。请先安装 Homebrew: https://brew.sh/${NC}"
    exit 1
fi

echo -e "${GREEN}==> [1/4] 检查并安装系统依赖...${NC}"
DEPENDENCIES=(cmake python3 ffmpeg pkg-config)
for dep in "${DEPENDENCIES[@]}"; do
    if brew list "$dep" &>/dev/null; then
        echo -e "${YELLOW}$dep 已经安装${NC}"
    else
        echo -e "${GREEN}正在安装 $dep ...${NC}"
        brew install "$dep"
    fi
done

# 2. 安装 Conan
echo -e "${GREEN}==> [2/4] 检查并安装 Conan...${NC}"
if ! command -v conan &> /dev/null; then
    echo -e "${GREEN}正在安装 Conan...${NC}"
    pip3 install conan --break-system-packages || pip3 install conan
else
    echo -e "${YELLOW}Conan 已经安装${NC}"
fi

# 初始化 Conan 如果需要
if [ ! -f ~/.conan2/profiles/default ]; then
    echo -e "${GREEN}检测 Conan 配置文件...${NC}"
    conan profile detect --force
fi

# 3. 准备构建目录
echo -e "${GREEN}==> [3/4] 准备构建环境...${NC}"
PROJECT_ROOT=$(pwd)
BUILD_DIR="${PROJECT_ROOT}/build"

if [ -d "$BUILD_DIR" ]; then
    echo -e "${YELLOW}清理旧的构建目录...${NC}"
    rm -rf "$BUILD_DIR"
fi
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# 4. Conan 安装依赖
echo -e "${GREEN}==> 执行 Conan 安装...${NC}"
# 注意: backend 目录在上一级
conan install ../backend --output-folder=. --build=missing -s build_type=Release

# 5. CMake 构建
echo -e "${GREEN}==> [4/4] 开始 CMake 构建...${NC}"
# 查找 conan_toolchain.cmake
TOOLCHAIN_FILE=$(find . -name "conan_toolchain.cmake" -print -quit)

if [ -z "$TOOLCHAIN_FILE" ]; then
    echo -e "${RED}错误: 未找到 conan_toolchain.cmake${NC}"
    exit 1
fi

echo -e "使用工具链文件: $TOOLCHAIN_FILE"

cmake ../backend \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
    -DCMAKE_BUILD_TYPE=Release

echo -e "${GREEN}==>开始编译...${NC}"
cmake --build . -j$(sysctl -n hw.ncpu)

# 6. 复制配置文件
echo -e "${GREEN}==> [5/4] 复制配置文件...${NC}"
mkdir -p "${PROJECT_ROOT}/bin"
cp "${PROJECT_ROOT}/backend/user_config.toml" "${PROJECT_ROOT}/bin/"
cp "${PROJECT_ROOT}/backend/config.json" "${PROJECT_ROOT}/bin/"

echo -e "${GREEN}SUCCESS! 构建完成。${NC}"
echo -e "${GREEN}可执行文件位于: ${PROJECT_ROOT}/bin/live2mp3${NC}"
