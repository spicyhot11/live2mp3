#pragma once

#include <drogon/plugins/Plugin.h>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

// 状态值: "pending"(待处理), "stable"(稳定), "processing"(处理中),
// "staged"(已暂存), "completed"(已完成), "deprecated"(已废弃-同名文件中较小者)
struct PendingFile {
  int id;
  std::string filepath;
  std::string fingerprint;
  int stable_count;
  std::string status;
  std::string temp_mp4_path;
  std::string temp_mp3_path;
  std::string start_time;
  std::string end_time;
};

void to_json(nlohmann::json &j, const PendingFile &p);

/**
 * @brief 待处理文件服务类
 *
 * 负责管理待处理文件的生命周期，包括文件的添加、状态跟踪、
 * 稳定性检测以及转换状态的流转。
 */
class PendingFileService : public drogon::Plugin<PendingFileService> {
public:
  PendingFileService() = default;
  ~PendingFileService() = default;
  PendingFileService(const PendingFileService &) = delete;
  PendingFileService &operator=(const PendingFileService &) = delete;

  // 初始化并启动插件
  void initAndStart(const Json::Value &config) override;
  // 关闭插件
  void shutdown() override;

  /**
   * @brief 添加或更新文件信息
   *
   * 如果文件已存在，更新其MD5和时间戳；如果不存在则创建新记录。
   *
   * @param filepath 文件路径
   * @param md5 文件的MD5哈希值
   * @return int 新的稳定计数(stable_count)
   */
  int addOrUpdateFile(const std::string &filepath, const std::string &md5);

  /**
   * @brief 获取满足稳定性条件的文件
   *
   * 获取所有状态为 "pending" 且稳定计数达到 minCount 的文件。
   *
   * @param minCount 最小稳定计数
   * @return std::vector<PendingFile> 满足条件的文件列表
   */
  std::vector<PendingFile> getStableFiles(int minCount);

  /**
   * @brief 标记文件为稳定状态
   *
   * 将文件状态从 "pending" 更新为 "stable"，表示已经通过稳定性检查，
   * 准备进入转换队列。
   *
   * @param filepath 文件路径
   * @return true 操作成功
   * @return false 操作失败
   */
  bool markAsStable(const std::string &filepath);

  /**
   * @brief 获取所有稳定状态的文件
   *
   * @return std::vector<PendingFile> 状态为 "stable" 的文件列表
   */
  std::vector<PendingFile> getAllStableFiles();

  /**
   * @brief 原子性地获取并标记稳定文件为处理中
   *
   * 在同一个事务中查询并标记文件，避免并发任务获取相同文件。
   * 这是解决并发竞态条件的关键方法。
   *
   * @return std::vector<PendingFile> 成功标记为 "processing" 的文件列表
   */
  std::vector<PendingFile> getAndClaimStableFiles();

  /**
   * @brief 标记文件为处理中
   *
   * 将文件状态更新为 "processing"，表示该文件正在被调度器处理。
   * 扫描阶段会自动跳过处于此状态的文件。
   *
   * @param filepath 文件路径
   * @return true 操作成功
   * @return false 操作失败
   */
  bool markAsProcessing(const std::string &filepath);

  /**
   * @brief 批量标记文件为处理中
   *
   * 在事务中将多个文件状态更新为 "processing"，确保原子性。
   *
   * @param filepaths 文件路径列表
   * @return true 全部标记成功
   * @return false 操作失败（事务回滚）
   */
  bool markAsProcessingBatch(const std::vector<std::string> &filepaths);

  /**
   * @brief 批量回滚文件状态到稳定
   *
   * 当处理失败时，将文件状态从 "processing" 回滚到 "stable"。
   *
   * @param filepaths 文件路径列表
   * @return true 全部回滚成功
   * @return false 操作失败
   */
  bool rollbackToStable(const std::vector<std::string> &filepaths);

  /**
   * @brief 标记文件为转换中
   *
   * @param filepath 文件路径
   * @return true 操作成功
   * @return false 操作失败
   */
  bool markAsConverting(const std::string &filepath);

  /**
   * @brief 标记文件为已暂存
   *
   * 转换完成后，将文件标记为 staged，并记录临时文件路径。
   *
   * @param filepath 原文件路径
   * @param tempMp4Path 生成的 MP4 临时文件路径
   * @return true 操作成功
   * @return false 操作失败
   */
  bool markAsStaged(const std::string &filepath,
                    const std::string &tempMp4Path);

  /**
   * @brief 标记文件为已完成
   *
   * @param filepath 文件路径
   * @return true 操作成功
   * @return false 操作失败
   */
  bool markAsCompleted(const std::string &filepath);

  /**
   * @brief 获取超过指定时间的已暂存文件
   *
   *用于查找准备合并的文件。
   *
   * @param seconds 超过多少秒
   * @return std::vector<PendingFile> 文件列表
   */
  std::vector<PendingFile> getStagedFilesOlderThan(int seconds);

  /**
   * @brief 获取所有已暂存文件
   *
   * 通常用于手动触发合并操作。
   *
   * @return std::vector<PendingFile> 所有状态为 staged 的文件
   */
  std::vector<PendingFile> getAllStagedFiles();

  /**
   * @brief 根据路径移除文件记录
   *
   * @param filepath 文件路径
   * @return true 删除成功
   * @return false 删除失败
   */
  bool removeFile(const std::string &filepath);

  /**
   * @brief 根据ID移除文件记录
   *
   * @param id 文件记录ID
   * @return true 删除成功
   * @return false 删除失败
   */
  bool removeFileById(int id);

  /**
   * @brief 获取特定文件的信息
   *
   * @param filepath 文件路径
   * @return std::optional<PendingFile>
   * 文件存在则返回PendingFile对象，否则返回nullopt
   */
  std::optional<PendingFile> getFile(const std::string &filepath);

  /**
   * @brief 检查文件是否已处理
   *
   * 通过MD5检查文件是否已经在历史记录中（防止重复处理）。
   *
   * @param md5 文件MD5
   * @return true 已处理
   * @return false 未处理
   */
  bool isProcessed(const std::string &md5);

  /**
   * @brief 获取所有已完成的文件
   *
   * @return std::vector<PendingFile> 完成的文件列表
   */
  std::vector<PendingFile> getCompletedFiles();

  /**
   * @brief 获取数据库中所有文件记录
   *
   * 主要用于调试。
   *
   * @return std::vector<PendingFile> 所有文件记录
   */
  std::vector<PendingFile> getAll();

  /**
   * @brief 标记文件为废弃状态
   *
   * 用于标记同名不同扩展名文件中较小的一方。
   * 废弃的文件不会参与后续的合并和转换。
   *
   * @param filepath 文件路径
   * @return true 操作成功
   * @return false 操作失败
   */
  bool markAsDeprecated(const std::string &filepath);

  /**
   * @brief 解决同名不同扩展名的文件冲突
   *
   * 检查是否存在同 stem（文件名不含扩展名）但不同扩展名的 stable 文件，
   * 如果存在则将较小的文件标记为 deprecated。
   * 应在文件变为 stable 状态后调用。
   *
   * @param filepath 刚变为 stable 状态的文件路径
   */
  void resolveDuplicateExtensions(const std::string &filepath);

private:
  /**
   * @brief 启动时清理操作
   *
   * 恢复 processing 状态的记录，清理临时文件和 _writing 文件
   */
  void cleanupOnStartup();

  /**
   * @brief 恢复 processing 状态的记录
   *
   * 将数据库中所有 processing 状态的记录恢复为 stable（如果文件存在）
   * 或删除记录（如果文件不存在）
   */
  void recoverProcessingRecords();

  /**
   * @brief 清理临时目录
   *
   * 清理输出目录下的 tmp 目录中的所有文件
   *
   * @param outputRoot 输出根目录路径
   */
  void cleanupTempDirectory(const std::string &outputRoot);

  /**
   * @brief 清理输出目录中的 _writing 文件
   *
   * @param outputRoot 输出根目录路径
   */
  void cleanupWritingFiles(const std::string &outputRoot);
};
