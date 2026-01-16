# 后端开发文档 (Backend)

## 这里的技术栈
- **语言**: C++17
- **框架**: Drogon (Web Framework)
- **依赖管理**: Conan, CMake
- **工具**: FFmpeg (运行时依赖)

## 目录结构
- `controllers/`: API 控制器 (SystemController)
- `services/`: 核心业务逻辑 (Scanner, Converter, Merger, Config, Scheduler)
- `config.json`: 配置文件
- `conanfile.txt`: 依赖定义

## 编译方式

### 1. 开发环境编译 (Local Development)

你需要安装 `conan`, `cmake`, `make`, `g++`, `python3`。

```bash
cd backend

# 安装依赖
conan install . --output-folder=build --build=missing -s build_type=Debug

# 配置 CMake (使用 Conan 生成的 Toolchain)
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Debug

# 编译
make -j$(nproc)

# 运行
./bin/live2mp3
```

### 2. 发布版编译 (Docker Release)

使用 Docker 进行多阶段构建，生成最小运行镜像。

```bash
# 在项目根目录执行
docker build -f docker/Dockerfile -t live2mp3:latest .
```

## API 接口说明

- `GET /api/status`: 检查服务状态
- `GET /api/config`: 获取当前配置
- `POST /api/config`: 更新配置 (JSON Body)
- `POST /api/trigger`: 手动触发一次扫描任务
