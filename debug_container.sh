#!/bin/bash
echo "Starting container in verify mode..."
# 覆盖入口点，直接进入 sh
docker run -it --rm \
  --entrypoint /bin/sh \
  live2mp3:latest
