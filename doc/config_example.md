# 配置文件示例 (config.json)

Live2MP3 使用 JSON 格式进行配置。默认情况下，程序会在启动目录下查找 `config.json`。

## 完整示例

```json
{
    "document_root": "./dist",
    "home_page": "index.html",
    "app": {
        "server_port": 8080,
        "number_of_threads": 0,
        "enable_server_header": true,
        "server_header_field": "Live2MP3-Server",
        "log": {
            "logfile_base_name": "live2mp3",
            "log_path": "./logs",
            "log_level": "DEBUG"
        },
        "scanner": {
            "video_roots": ["/path/to/your/videos"],
            "extensions": [".mp4", ".ts", ".mkv"],
            "allow_list": [],
            "deny_list": [],
            "simple_allow_list": [],
            "simple_deny_list": []
        },
        "output": {
            "output_root": "./output",
            "keep_original": false
        },
        "scheduler": {
            "scan_interval_seconds": 60,
            "merge_window_hours": 2
        }
    },
    "plugins": [
        {
            "name": "ConfigService",
            "config": {
                "config_path": "./config.json"
            }
        },
        {
            "name": "DatabaseService",
            "config": {
                "db_path": "./live2mp3.db"
            }
        },
        {
            "name": "HistoryService",
            "config": {}
        },
        {
            "name": "ConverterService",
            "config": {},
            "dependencies": ["ConfigService", "HistoryService"]
        },
        {
            "name": "MergerService",
            "config": {},
            "dependencies": ["ConfigService", "HistoryService"]
        },
        {
            "name": "ScannerService",
            "config": {},
            "dependencies": ["ConfigService", "HistoryService"]
        },
        {
            "name": "SchedulerService",
            "config": {},
            "dependencies": ["ConfigService", "MergerService", "ScannerService", "ConverterService"]
        }
    ]
}
```

## 关键字段说明

### 核心配置 (`app` 节点)

| 字段 | 类型 | 说明 |
| :--- | :--- | :--- |
| `server_port` | Integer | **端口配置**。后端服务监听的端口号，默认为 8080。 |
| `log.log_level` | String | 日志级别，可选：`TRACE`, `DEBUG`, `INFO`, `WARN`, `ERROR`。 |

### 扫描器配置 (`app.scanner`)

| 字段 | 类型 | 说明 |
| :--- | :--- | :--- |
| `video_roots` | Array | 监控视频的根目录列表。 |
| `extensions` | Array | 关注的文件后缀名（带点），例如 `[".mp4", ".ts"]`。 |
| `allow_list` | Array | 正则表达式白名单。 |
| `deny_list` | Array | 正则表达式黑名单。 |

### 输出配置 (`app.output`)

| 字段 | 类型 | 说明 |
| :--- | :--- | :--- |
| `output_root` | String | MP3 文件生成的根目录。 |
| `keep_original` | Boolean | 转换完成后是否保留源视频文件（暂时建议设为 `false` 以节省空间）。 |

### 调度器配置 (`app.scheduler`)

| 字段 | 类型 | 说明 |
| :--- | :--- | :--- |
| `scan_interval_seconds` | Integer | 目录扫描间隔（秒）。 |
| `merge_window_hours` | Integer | 合并窗口（小时）。如果相邻文件的结束与开始时间在这个范围内，则自动合并。 |
