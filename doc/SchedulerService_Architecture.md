# SchedulerService 任务执行逻辑与交互结构

## 整体架构

```mermaid
graph TB
    subgraph Scheduler["SchedulerService"]
        START["start() / triggerNow()"]
        TASK["runTaskAsync()"]
        PHASE1["阶段1: runStabilityScan()"]
        PHASE2["阶段2: runConversionAsync()"]
        PHASE3["阶段3: runMergeAndOutputAsync()"]
        BATCH["processBatchAsync()"]
    end

    subgraph Services["依赖服务"]
        CONFIG["ConfigService"]
        SCANNER["ScannerService"]
        PENDING["PendingFileService"]
        FFMPEG["FfmpegTaskService"]
        THREAD["CommonThreadService"]
    end

    START --> TASK
    TASK --> PHASE1
    PHASE1 --> PHASE2
    PHASE2 --> PHASE3
    PHASE3 --> BATCH

    PHASE1 -.-> THREAD
    PHASE1 -.-> SCANNER
    PHASE1 -.-> PENDING

    PHASE2 -.-> PENDING
    PHASE2 -.-> FFMPEG

    PHASE3 -.-> PENDING
    PHASE3 -.-> BATCH
    BATCH -.-> FFMPEG
```

---

## 任务执行流程详解

```mermaid
flowchart TD
    subgraph Entry["入口"]
        A1["定时触发 (runEvery)"]
        A2["手动触发 (triggerNow)"]
    end

    B["runTaskAsync(immediate)"]
    
    subgraph Phase1["阶段1: 稳定性扫描"]
        C1["扫描输入目录文件"]
        C2["计算文件 MD5"]
        C3["更新 PendingFileService"]
        C4{"stable_count >= 阈值?"}
        C5["markAsStable()"]
    end

    subgraph Phase2["阶段2: 视频转换"]
        D1["获取所有 stable 文件"]
        D2["提交 CONVERT_MP4 任务"]
        D3{"使用临时目录?"}
        D4["markAsStaged()"]
        D5["提交 CONVERT_MP3 任务"]
        D6["markAsCompleted()"]
    end

    subgraph Phase3["阶段3: 合并输出"]
        E1["获取所有 staged 文件"]
        E2["按父目录分组"]
        E3["按时间排序"]
        E4["检测时间间隔分批"]
        E5["processBatchAsync()"]
    end

    subgraph Batch["批次处理"]
        F1["提交 MERGE 任务"]
        F2["移动 MP4 到输出目录"]
        F3["提交 CONVERT_MP3 任务"]
        F4["markAsCompleted()"]
        F5["清理临时文件"]
    end

    A1 --> B
    A2 --> B
    
    B --> C1
    C1 --> C2
    C2 --> C3
    C3 --> C4
    C4 -->|是| C5
    C4 -->|否| C1
    
    C5 --> D1
    D1 --> D2
    D2 --> D3
    D3 -->|是| D4
    D3 -->|否| D5
    D5 --> D6
    D4 --> E1
    D6 --> E1
    
    E1 --> E2
    E2 --> E3
    E3 --> E4
    E4 --> E5
    
    E5 --> F1
    F1 --> F2
    F2 --> F3
    F3 --> F4
    F4 --> F5
```

---

## 文件状态流转

```mermaid
stateDiagram-v2
    [*] --> pending: 扫描发现新文件
    pending --> pending: MD5变化 (重置计数)
    pending --> stable: stable_count >= 阈值
    stable --> converting: 开始转换
    converting --> staged: 转换完成 (临时目录)
    converting --> completed: 转换完成 (直接输出)
    staged --> completed: 合并完成
    completed --> [*]
```

---

## 服务交互关系

| 阶段 | 服务调用 | 执行方式 | 说明 |
|------|----------|----------|------|
| **阶段1** | `ScannerService::scan()` | 同步 | 扫描输入目录获取文件列表 |
| | `FileUtils::calculateMD5()` | 同步 (线程池) | CPU密集型，在线程池执行 |
| | `PendingFileService::addOrUpdateFile()` | 同步 | 更新文件状态和MD5 |
| | `PendingFileService::markAsStable()` | 同步 | 标记达到稳定阈值的文件 |
| **阶段2** | `PendingFileService::getAllStableFiles()` | 同步 | 获取待转换文件 |
| | `FfmpegTaskService::submitTask(CONVERT_MP4)` | **协程** | 提交MP4转换任务 |
| | `FfmpegTaskService::submitTask(CONVERT_MP3)` | **协程** | 提交MP3提取任务 |
| | `PendingFileService::markAsStaged/Completed()` | 同步 | 更新文件状态 |
| **阶段3** | `PendingFileService::getAllStagedFiles()` | 同步 | 获取已暂存文件 |
| | `MergerService::parseTime()` | 同步 | 解析文件名中的时间 |
| | `FfmpegTaskService::submitTask(MERGE)` | **协程** | 提交视频合并任务 |
| | `FfmpegTaskService::submitTask(CONVERT_MP3)` | **协程** | 从合并结果提取MP3 |

---

## 关键设计要点

### 1. 协程调度
`runTaskAsync()` 使用 `drogon::Task<void>` 实现非阻塞执行，三个阶段顺序执行但不阻塞事件循环。

### 2. 阻塞任务隔离
MD5计算等CPU密集操作通过 `CommonThreadService` 在独立线程池执行，避免阻塞协程调度器。

```cpp
co_await live2mp3::utils::awaitFuture(
    commonThreadServicePtr_->runTaskAsync([this]() { runStabilityScan(); }));
```

### 3. FFmpeg任务委托
所有FFmpeg操作通过 `FfmpegTaskService::submitTask()` 统一提交，支持三种任务类型：
- `CONVERT_MP4`: FLV转MP4 (AV1编码)
- `CONVERT_MP3`: 从MP4提取音频
- `MERGE`: 合并多个MP4文件

### 4. 并发控制
使用 `running_` 原子变量防止任务重叠执行：

```cpp
if (running_.exchange(true)) {
    LOG_INFO << "Task already running, skipping";
    co_return;
}
```

### 5. 批次合并逻辑
根据 `merge_window_seconds` 配置将时间相近的文件分组合并：
- 解析文件名中的时间戳
- 按时间排序
- 时间间隔超过阈值则分为新批次
- 立即模式(`immediate=true`)下不等待，直接处理所有文件

---

## 相关文件

- [SchedulerService.h](file:///home/code-dev/src/live2mp3/backend/services/SchedulerService.h)
- [SchedulerService.cc](file:///home/code-dev/src/live2mp3/backend/services/SchedulerService.cc)
- [FfmpegTaskService.h](file:///home/code-dev/src/live2mp3/backend/services/FfmpegTaskService.h)
- [PendingFileService.h](file:///home/code-dev/src/live2mp3/backend/services/PendingFileService.h)
- [CommonThreadService.h](file:///home/code-dev/src/live2mp3/backend/services/CommonThreadService.h)
