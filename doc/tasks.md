# 任务清单 (Task Checklist)

## 设置与基础设施 (Setup & Infrastructure)
- [x] 更新 `backend/config.json` 应用设置的 Schema
- [x] 检查/更新 Dockerfile 以包含 `ffmpeg` 依赖

## 后端核心 (Backend Core)
- [x] 实现 `ConfigService` (加载/保存/验证配置)
- [x] 实现 `ScannerService` (目录遍历、文件过滤)
- [x] 实现 `ConverterService` (FFmpeg 封装、文件检查)
- [x] 实现 `MergerService` (时间戳逻辑、文件合并)
- [x] 实现 `SchedulerService` (定时器/调度逻辑)
- [x] 将所有服务集成到 `main.cc`

## 后端 API (Backend API)
- [x] 创建 `SystemController` 接口
    - [x] `GET /api/config` (获取配置)
    - [x] `POST /api/config` (更新配置)
    - [x] `POST /api/trigger` (手动触发任务)
    - [x] `GET /api/status` (获取状态)

## 前端 (Frontend)
- [x] 设置 Axios / API 客户端
- [x] 创建配置页面 (Configuration Page) - 路径、规则管理
- [x] 创建仪表盘页面 (Dashboard Page) - 状态、日志显示
- [x] 连接前端与后端 API

## 验证 (Verification)
- [x] 测试视频扫描逻辑
- [x] 测试 MP3 转换与修复
- [x] 测试时间戳合并逻辑
