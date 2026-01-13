# ========== Stage 1: Build Frontend ==========
FROM node:20-alpine AS frontend_builder

WORKDIR /app
COPY frontend/package.json frontend/package-lock.json* ./
# 安装依赖
RUN npm install

# 复制源码并构建
COPY frontend/ .
RUN npm run build

# ========== Stage 2: Build Backend ==========
FROM ubuntu:22.04 AS backend_builder

# 防止交互式提示
ENV DEBIAN_FRONTEND=noninteractive

# 安装基础编译工具和 Python (Conan 需要)
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    python3 \
    python3-pip \
    && rm -rf /var/lib/apt/lists/*

# 安装 Conan
RUN pip3 install conan && conan profile detect

# 准备 Conan 依赖构建环境
WORKDIR /app
COPY backend/conanfile.txt .

# 安装依赖 (Release 模式，构建缺失的二进制包)
# 注意: --build=missing 会自动编译 Conan Center 中没有预编译二进制的库 (Drogon 经常需要现编)
RUN conan install . --output-folder=build --build=missing -s build_type=Release

# 编译应用
COPY backend/ .
WORKDIR /app/build
# 使用 Conan 生成的 Toolchain 文件
RUN cmake .. -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)

# ========== Stage 3: Runtime Environment ==========
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# 安装运行时依赖
# Conan 生成的 Drogon 通常会静态链接大部分库，或者依赖基本的系统库
# 这里保留 ffmpeg 和基本的 SSL/Zlib 支持
RUN apt-get update && apt-get install -y \
    ffmpeg \
    libssl3 \
    zlib1g \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# 从构建阶段复制文件
# 1. 配置文件
COPY backend/config.json ./
# 2. 编译好的后端二进制
COPY --from=backend_builder /app/build/live2mp3 ./
# 3. 编译好的前端静态文件 (放到 dist 目录)
COPY --from=frontend_builder /app/dist ./dist

# 暴露端口
EXPOSE 8080

# 启动命令
CMD ["./live2mp3"]
