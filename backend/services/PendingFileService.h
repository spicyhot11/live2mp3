#pragma once

#include <drogon/plugins/Plugin.h>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

// 状态值: "pending"(待处理), "stable"(稳定), "converting"(转换中),
// "staged"(已暂存), "completed"(已完成)
struct PendingFile {
  int id;
  std::string filepath;
  std::string current_md5;
  int stable_count;
  std::string status;
  std::string temp_mp4_path;
  std::string temp_mp3_path;
  std::string created_at;
  std::string updated_at;
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
};
