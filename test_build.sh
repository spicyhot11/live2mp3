#!/bin/bash
set -e

echo "➡️  [1/3] Building Docker image (this may take a while for the first time due to Conan)..."
# 使用 linux/arm64 确保在 M4 Mac 上原生运行，如果是在 x86 Linux 上会自动切换
docker build -t live2mp3:latest -f docker/Dockerfile .

echo "➡️  [2/3] Starting container..."
# 检查是否已有旧容器并删除
if [ "$(docker ps -aq -f name=live2mp3_test)" ]; then
    echo "Removed old container"
    docker rm -f live2mp3_test
fi

# 运行容器
# 映射端口 8080
# 挂载 logs 目录以便观察
docker run -d \
  --name live2mp3_test \
  -p 8080:8080 \
  live2mp3:latest

echo "✅ [3/3] Deployment successful!"
echo "Server is running at: http://localhost:8080"
echo "Check logs with: docker logs -f live2mp3_test"
