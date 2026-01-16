# 前端开发文档 (Frontend)

## 技术栈
- **框架**: Vue 3 (Composition API)
- **构建工具**: Vite
- **HTTP 客户端**: Axios
- **样式**: CSS (Custom Styles)

## 目录结构
- `src/components/`: Vue 组件 (ConfigPanel, StatusPanel)
- `src/App.vue`: 主布局
- `src/api.js`: API 封装

## 编译方式

### 1. 开发环境运行 (Development)

需要安装 Node.js (推荐 v18+)。

```bash
cd frontend

# 安装依赖
npm install

# 启动开发服务器 (带有热重载)
npm run dev
```
访问提供的 Local URL (通常是 http://localhost:5173)。
注意：开发模式下需要配置 Vite Proxy 转发 `/api` 请求到后端 `:8080`，或者确保后端允许 CORS。

### 2. 发布版编译 (Production Build)

```bash
cd frontend

# 构建静态资源
npm run build
```

构建产物位于 `frontend/dist` 目录。
后端 Drogon 服务器已配置为托管此目录下的静态文件。
