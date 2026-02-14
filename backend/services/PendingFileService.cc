#include "PendingFileService.h"
#include "../utils/FfmpegUtils.h"
#include "ConfigService.h"
#include "MergerService.h"
#include <drogon/drogon.h>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

// PendingFile 便捷方法
std::string PendingFile::getFilepath() const {
  if (dir_path.empty())
    return filename;
  if (dir_path.back() == '/')
    return dir_path + filename;
  return dir_path + "/" + filename;
}

void to_json(nlohmann::json &j, const PendingFile &p) {
  j = nlohmann::json{
      {"id", p.id},
      {"dir_path", p.dir_path},
      {"filename", p.filename},
      {"filepath", p.getFilepath()},
      {"fingerprint", p.fingerprint},
      {"stable_count", p.stable_count},
      {"status", p.status},
      {"temp_mp4_path", p.temp_mp4_path},
      {"temp_mp3_path", p.temp_mp3_path},
      {"start_time", p.start_time},
      {"end_time", p.end_time},
  };
}

void PendingFileService::initAndStart(const Json::Value &config) {
  LOG_INFO << "PendingFileService initialized";
}

void PendingFileService::shutdown() {}

int PendingFileService::addOrUpdateFile(const std::string &filepath,
                                        const std::string &fingerprint) {
  // 辅助：拆分路径
  fs::path p(filepath);
  std::string dirPath = p.parent_path().string();
  std::string fname = p.filename().string();

  // Check if file exists
  auto existing = repo_.findByPath(filepath);

  if (existing.has_value()) {
    // File exists, check if fingerprint matches
    if (existing->fingerprint == fingerprint) {
      // Fingerprint matches. Check status.
      if (existing->status != "pending") {
        LOG_DEBUG << "[addOrUpdateFile] File " << filepath << " is "
                  << existing->status << " with same fingerprint. Ignoring.";
        return -1;
      }

      // Same fingerprint and pending, increment stable_count
      if (repo_.incrementStableCount(dirPath, fname)) {
        int result = existing->stable_count + 1;
        LOG_DEBUG << "[addOrUpdateFile] Fingerprint same, incremented "
                     "stable_count to "
                  << result;
        return result;
      } else {
        LOG_ERROR << "[addOrUpdateFile] Update failed";
        return -1;
      }
    } else {
      // Fingerprint changed, reset stable_count
      LOG_DEBUG
          << "[addOrUpdateFile] Fingerprint changed, resetting stable_count";
      if (repo_.resetFingerprint(dirPath, fname, fingerprint)) {
        return 1;
      } else {
        LOG_ERROR << "[addOrUpdateFile] Reset failed";
        return -1;
      }
    }
  } else {
    // New file, insert
    LOG_DEBUG << "[addOrUpdateFile] New file, inserting: " << filepath;
    if (repo_.insert(dirPath, fname, fingerprint)) {
      LOG_DEBUG << "[addOrUpdateFile] Inserted successfully";
      return 1;
    } else {
      LOG_ERROR << "[addOrUpdateFile] Insert failed";
      return -1;
    }
  }
}

std::vector<PendingFile> PendingFileService::getStableFiles(int minCount) {
  return repo_.findStableWithMinCount(minCount);
}

bool PendingFileService::markAsStable(const std::string &filepath) {
  fs::path p(filepath);
  std::string fname = p.filename().string();

  // 1. Parse start_time from filename
  std::string startTimeStr;
  std::string endTimeStr;

  auto parsedTime = MergerService::parseTime(fname);

  if (parsedTime.has_value()) {
    // 2. Get media duration
    int durationMs = live2mp3::utils::getMediaDuration(filepath);
    if (durationMs == -1) {
      LOG_WARN << "[markAsStable] Cannot get duration for " << filepath
               << ", marking as deprecated";
      markAsDeprecated(filepath);
      return false;
    }

    // Format start_time
    auto startTp = parsedTime.value();
    std::time_t startTt = std::chrono::system_clock::to_time_t(startTp);
    std::tm startTm = *std::localtime(&startTt);
    std::ostringstream startSs;
    startSs << std::put_time(&startTm, "%Y-%m-%d %H:%M:%S");
    startTimeStr = startSs.str();

    // Calculate end_time = start_time + duration
    auto endTp = startTp + std::chrono::milliseconds(durationMs);
    std::time_t endTt = std::chrono::system_clock::to_time_t(endTp);
    std::tm endTm = *std::localtime(&endTt);
    std::ostringstream endSs;
    endSs << std::put_time(&endTm, "%Y-%m-%d %H:%M:%S");
    endTimeStr = endSs.str();
  }

  // 3. Update DB
  bool success = repo_.updateStatusWithStartEnd(filepath, "stable",
                                                startTimeStr, endTimeStr);
  if (success) {
    LOG_DEBUG << "[markAsStable] Marked as stable: " << filepath
              << " (start_time=" << startTimeStr << ", end_time=" << endTimeStr
              << ")";
    // 检测并处理同名不同扩展名的文件
    resolveDuplicateExtensions(filepath);
  }
  return success;
}

std::vector<PendingFile> PendingFileService::getAllStableFiles() {
  return repo_.findByStatus("stable");
}

std::vector<PendingFile> PendingFileService::getAndClaimStableFiles() {
  return repo_.claimStableFiles();
}

bool PendingFileService::markAsProcessing(const std::string &filepath) {
  bool success = repo_.updateStatus(filepath, "processing");
  if (success) {
    LOG_DEBUG << "[markAsProcessing] Marked as processing: " << filepath;
  }
  return success;
}

bool PendingFileService::markAsProcessingBatch(
    const std::vector<std::string> &filepaths) {
  return repo_.markProcessingBatch(filepaths);
}

bool PendingFileService::rollbackToStable(
    const std::vector<std::string> &filepaths) {
  return repo_.rollbackToStable(filepaths);
}

bool PendingFileService::markAsConverting(const std::string &filepath) {
  return repo_.updateStatus(filepath, "converting");
}

bool PendingFileService::markAsStaged(const std::string &filepath,
                                      const std::string &tempMp4Path) {
  return repo_.updateStatusWithTempPath(filepath, "staged", tempMp4Path);
}

bool PendingFileService::markAsCompleted(const std::string &filepath) {
  return repo_.updateStatus(filepath, "completed");
}

std::vector<PendingFile>
PendingFileService::getStagedFilesOlderThan(int seconds) {
  return repo_.findStagedOlderThan(seconds);
}

std::vector<PendingFile> PendingFileService::getAllStagedFiles() {
  return repo_.findByStatus("staged");
}

bool PendingFileService::removeFile(const std::string &filepath) {
  return repo_.deleteByPath(filepath);
}

bool PendingFileService::removeFileById(int id) { return repo_.deleteById(id); }

std::optional<PendingFile>
PendingFileService::getFile(const std::string &filepath) {
  return repo_.findByPath(filepath);
}

bool PendingFileService::isProcessed(const std::string &md5) {
  return repo_.existsByFingerprint(md5);
}

std::vector<PendingFile> PendingFileService::getCompletedFiles() {
  return repo_.findByStatus("completed");
}

std::vector<PendingFile> PendingFileService::getAll() {
  return repo_.findAll();
}

bool PendingFileService::markAsDeprecated(const std::string &filepath) {
  bool success = repo_.updateStatus(filepath, "deprecated");
  if (success) {
    LOG_INFO << "[markAsDeprecated] 文件标记为废弃: " << filepath;
  }
  return success;
}

void PendingFileService::resolveDuplicateExtensions(
    const std::string &filepath) {
  // 1. 提取文件 stem（不含扩展名）和目录
  fs::path path(filepath);
  std::string stem = path.stem().string();
  std::string dir = path.parent_path().string();

  // 2. 查询同目录下同 stem 但不同扩展名的 stable 文件
  std::string pattern = stem + ".%";
  auto candidates = repo_.findByDirAndStemLike(dir, pattern, "stable");

  // 收集文件大小信息
  struct FileInfo {
    std::string filepath;
    uintmax_t size;
  };
  std::vector<FileInfo> validCandidates;

  for (const auto &pf : candidates) {
    std::string candidatePath = pf.getFilepath();

    // 验证：确保 stem 完全匹配
    fs::path candidateFsPath(candidatePath);
    if (candidateFsPath.stem().string() != stem) {
      continue;
    }

    // 获取文件大小
    std::error_code ec;
    uintmax_t fileSize = fs::file_size(candidatePath, ec);
    if (ec) {
      LOG_WARN << "[resolveDuplicateExtensions] 无法获取文件大小: "
               << candidatePath << ", 错误: " << ec.message();
      continue;
    }

    validCandidates.push_back({candidatePath, fileSize});
  }

  // 3. 如果只有一个文件或没有文件，无需处理
  if (validCandidates.size() <= 1) {
    return;
  }

  LOG_INFO << "[resolveDuplicateExtensions] 发现 " << validCandidates.size()
           << " 个同名不同扩展名的文件 (stem=" << stem << ")";

  // 4. 找出最大的文件
  auto maxIt = std::max_element(
      validCandidates.begin(), validCandidates.end(),
      [](const FileInfo &a, const FileInfo &b) { return a.size < b.size; });

  // 5. 将非最大文件标记为 deprecated
  for (const auto &file : validCandidates) {
    if (file.filepath != maxIt->filepath) {
      LOG_INFO << "[resolveDuplicateExtensions] 标记为废弃: " << file.filepath
               << " (size=" << file.size << " < " << maxIt->filepath
               << " size=" << maxIt->size << ")";
      markAsDeprecated(file.filepath);
    }
  }
}

void PendingFileService::cleanupOnStartup() {
  LOG_INFO << "[cleanupOnStartup] 开始启动清理操作...";

  // 1. 恢复 processing 状态的记录
  recoverProcessingRecords();

  // 2. 清理输出目录中的临时文件和 _writing 文件
  auto configService = drogon::app().getPlugin<ConfigService>();
  if (configService) {
    AppConfig cfg = configService->getConfig();
    cleanupTempDirectory(cfg.output.output_root);
    cleanupWritingFiles(cfg.output.output_root);
  } else {
    LOG_WARN << "[cleanupOnStartup] 无法获取 ConfigService，跳过清理操作";
  }

  LOG_INFO << "[cleanupOnStartup] 启动清理操作完成";
}

void PendingFileService::recoverProcessingRecords() {
  auto records = repo_.findProcessingRecords();

  if (records.empty()) {
    LOG_INFO << "[recoverProcessingRecords] 没有需要恢复的 processing 记录";
    return;
  }

  LOG_INFO << "[recoverProcessingRecords] 发现 " << records.size()
           << " 条 processing 记录，开始恢复...";

  int recoveredCount = 0;
  int deletedCount = 0;

  for (const auto &rec : records) {
    std::string fullPath = rec.getFilepath();
    std::error_code ec;
    bool fileExists = fs::exists(fullPath, ec);

    if (ec) {
      LOG_WARN << "[recoverProcessingRecords] 检查文件存在性失败: " << fullPath
               << ", 错误: " << ec.message();
      continue;
    }

    if (fileExists) {
      // 检查该 ID 是否已经在 task_batch_files 中
      if (!batchRepo_.isInBatch(rec.id)) {
        // 文件存在且不在任何批次中，属于僵尸记录，恢复为 stable 状态
        if (repo_.updateStatusById(rec.id, "stable")) {
          LOG_INFO << "[recoverProcessingRecords] 发现僵尸记录，恢复为 stable: "
                   << fullPath;
          recoveredCount++;
        } else {
          LOG_ERROR << "[recoverProcessingRecords] 更新失败: " << fullPath;
        }
      } else {
        LOG_DEBUG << "[recoverProcessingRecords] 文件已在批次中，保持 "
                     "processing 状态: "
                  << fullPath;
      }
    } else {
      // 文件不存在，删除记录
      if (repo_.deleteById(rec.id)) {
        LOG_WARN << "[recoverProcessingRecords] 文件不存在，删除记录: "
                 << fullPath;
        deletedCount++;
      } else {
        LOG_ERROR << "[recoverProcessingRecords] 删除失败: " << fullPath;
      }
    }
  }

  LOG_INFO << "[recoverProcessingRecords] 恢复完成: 恢复 " << recoveredCount
           << " 条，删除 " << deletedCount << " 条";
}

void PendingFileService::cleanupTempDirectory(const std::string &outputRoot) {
  const std::string tmpDir = outputRoot + "/tmp";
  std::error_code ec;

  if (!fs::exists(tmpDir, ec) || !fs::is_directory(tmpDir, ec)) {
    LOG_WARN << "[cleanupTempDirectory] 临时目录不存在或不是目录: " << tmpDir;
    return;
  }

  LOG_INFO << "[cleanupTempDirectory] 开始清理临时目录: " << tmpDir;

  int deletedCount = 0;
  for (const auto &entry : fs::directory_iterator(tmpDir, ec)) {
    if (ec) {
      LOG_WARN << "[cleanupTempDirectory] 遍历目录失败: " << ec.message();
      break;
    }

    if (entry.is_regular_file(ec)) {
      std::string filename = entry.path().filename().string();
      if (filename.find("_writing") != std::string::npos) {
        std::error_code removeEc;
        fs::remove(entry.path(), removeEc);
        if (!removeEc) {
          LOG_DEBUG << "[cleanupTempDirectory] 删除: " << entry.path().string();
          deletedCount++;
        } else {
          LOG_WARN << "[cleanupTempDirectory] 删除失败: "
                   << entry.path().string() << ", 错误: " << removeEc.message();
        }
      }
    }
  }

  LOG_INFO << "[cleanupTempDirectory] 清理完成，删除 " << deletedCount
           << " 个文件";
}

void PendingFileService::cleanupWritingFiles(const std::string &outputRoot) {
  std::error_code ec;

  if (!fs::exists(outputRoot, ec) || !fs::is_directory(outputRoot, ec)) {
    LOG_WARN << "[cleanupWritingFiles] 输出目录不存在或不是目录: "
             << outputRoot;
    return;
  }

  LOG_INFO << "[cleanupWritingFiles] 开始清理输出目录中的 _writing 文件: "
           << outputRoot;

  int deletedCount = 0;
  for (const auto &entry : fs::recursive_directory_iterator(outputRoot, ec)) {
    if (ec) {
      LOG_WARN << "[cleanupWritingFiles] 遍历目录失败: " << ec.message();
      break;
    }

    if (entry.is_regular_file(ec)) {
      std::string filename = entry.path().filename().string();
      if (filename.find("_writing.mp4") != std::string::npos ||
          filename.find("_writing.mp3") != std::string::npos) {
        std::error_code removeEc;
        fs::remove(entry.path(), removeEc);
        if (!removeEc) {
          LOG_INFO << "[cleanupWritingFiles] 删除 _writing 文件: "
                   << entry.path().string();
          deletedCount++;
        } else {
          LOG_WARN << "[cleanupWritingFiles] 删除失败: "
                   << entry.path().string() << ", 错误: " << removeEc.message();
        }
      }
    }
  }

  LOG_INFO << "[cleanupWritingFiles] 清理完成，删除 " << deletedCount
           << " 个 _writing 文件";
}
