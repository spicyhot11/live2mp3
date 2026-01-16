# Live2MP3 使用指南

## 1. 使用 Docker 部署 (推荐)

### 构建镜像
```bash
docker build -f docker/Dockerfile -t live2mp3:latest .
```

### 运行容器
```bash
docker run -d \
  -p 8080:8080 \
  -v /你的视频目录:/videos \
  -v /你的输出目录:/output \
  live2mp3:latest
```

## 2. 系统配置

1. 打开浏览器访问 `http://localhost:8080`
2. 进入配置面板：
   - **Video Roots**: 添加 `/videos`
   - **Extensions**: 添加 `.mp4`, `.ts`
   - **Output Root**: 设置为 `/output`
   - **Scheduler**: 配置自动扫描间隔 (默认 60秒) 和 合并窗口 (默认 2小时)
3. 保存配置。

有关配置文件的详细说明和示例，请参考 [配置文件示例](./config_example.md)。

## 3. 功能说明

- **扫描**: 自动扫描配置目录下的视频文件。
- **转换**: 将视频转换为 MP3，输出到 Output Root，保持原有目录结构。
- **合并**: 自动识别文件名中的时间戳。若相邻文件的结束时间与开始时间相差在 "Merge Window" 设定范围内，会自动合并为一个 MP3 文件。
- **API**: 提供 RESTful API 供外部调用。
