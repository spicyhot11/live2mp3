#include "BatchTaskService.h"
#include "MergerService.h"
#include <cmath>
#include <filesystem>
#include <map>
#include <set>

namespace fs = std::filesystem;

std::string BatchFile::getFilepath() const {
  if (dir_path.empty())
    return filename;
  if (dir_path.back() == '/')
    return dir_path + filename;
  return dir_path + "/" + filename;
}

void to_json(nlohmann::json &j, const BatchInfo &b) {
  j = nlohmann::json{{"id", b.id},
                     {"streamer", b.streamer},
                     {"status", b.status},
                     {"output_dir", b.output_dir},
                     {"tmp_dir", b.tmp_dir},
                     {"final_mp4_path", b.final_mp4_path},
                     {"final_mp3_path", b.final_mp3_path},
                     {"total_files", b.total_files},
                     {"encoded_count", b.encoded_count},
                     {"failed_count", b.failed_count}};
}

void to_json(nlohmann::json &j, const BatchFile &f) {
  j = nlohmann::json{{"id", f.id},
                     {"batch_id", f.batch_id},
                     {"dir_path", f.dir_path},
                     {"filename", f.filename},
                     {"filepath", f.getFilepath()},
                     {"fingerprint", f.fingerprint},
                     {"pending_file_id", f.pending_file_id},
                     {"status", f.status},
                     {"encoded_path", f.encoded_path},
                     {"retry_count", f.retry_count}};
}

void BatchTaskService::initAndStart(const Json::Value &config) {
  recoverInterruptedTasks();
  LOG_INFO << "BatchTaskService initialized";
}

void BatchTaskService::recoverInterruptedTasks() {
  LOG_INFO << "[recoverInterruptedTasks] Checking for interrupted tasks...";

  // 1. 将 task_batch_files 中 encoding 状态的文件回滚到 pending
  int fileChanges = repo_.rollbackEncodingFiles();
  if (fileChanges > 0) {
    LOG_WARN << "[recoverInterruptedTasks] Rolled back " << fileChanges
             << " batch files from 'encoding' to 'pending'";
  } else {
    LOG_INFO << "[recoverInterruptedTasks] No batch files need rollback";
  }

  // 2. 将 task_batches 中 merging / extracting_mp3 状态的批次回滚到 encoding
  int batchChanges = repo_.rollbackBatchStatus();
  if (batchChanges > 0) {
    LOG_WARN << "[recoverInterruptedTasks] Rolled back " << batchChanges
             << " batches from 'merging/extracting_mp3' to 'encoding'";
  } else {
    LOG_INFO << "[recoverInterruptedTasks] No batches need rollback";
  }

  LOG_INFO << "[recoverInterruptedTasks] Recovery check completed";
}

void BatchTaskService::shutdown() {}

int BatchTaskService::createBatch(const std::string &streamer,
                                  const std::string &outputDir,
                                  const std::string &tmpDir,
                                  const std::vector<BatchInputFile> &files) {
  int batchId = repo_.createBatchWithFiles(streamer, outputDir, tmpDir, files);
  if (batchId >= 0) {
    LOG_INFO << "[createBatch] Created batch id=" << batchId
             << " streamer=" << streamer << " files=" << files.size();
  }
  return batchId;
}

bool BatchTaskService::markFileEncoding(int batchId,
                                        const std::string &filepath) {
  return repo_.updateBatchFileStatus(batchId, filepath, "encoding");
}

bool BatchTaskService::markFileEncoded(int batchId, const std::string &filepath,
                                       const std::string &encodedPath,
                                       const std::string &fingerprint) {
  return repo_.markFileEncoded(batchId, filepath, encodedPath, fingerprint);
}

bool BatchTaskService::markFileFailed(int batchId,
                                      const std::string &filepath) {
  // 1. 标记 PendingFile 为弃用
  auto pendingFileService = drogon::app().getSharedPlugin<PendingFileService>();
  if (pendingFileService) {
    pendingFileService->markAsDeprecated(filepath);
  }

  // 2. 删除批次文件记录并递增 failed_count
  return repo_.deleteBatchFileAndIncrFailed(batchId, filepath);
}

std::vector<BatchFile> BatchTaskService::getBatchFiles(int batchId) {
  return repo_.findBatchFiles(batchId);
}

std::vector<std::string> BatchTaskService::getEncodedPaths(int batchId) {
  return repo_.findEncodedPaths(batchId);
}

bool BatchTaskService::isBatchEncodingComplete(int batchId) {
  return repo_.countPendingOrEncoding(batchId) == 0;
}

std::vector<int>
BatchTaskService::getEncodingCompleteBatchIds(int minAgeSeconds) {
  return repo_.findCompleteBatchIds(minAgeSeconds);
}

bool BatchTaskService::updateBatchStatus(int batchId,
                                         const std::string &status) {
  return repo_.updateBatchStatus(batchId, status);
}

bool BatchTaskService::setBatchFinalPaths(int batchId,
                                          const std::string &mp4Path,
                                          const std::string &mp3Path) {
  return repo_.setBatchFinalPaths(batchId, mp4Path, mp3Path);
}

bool BatchTaskService::isInBatch(int pendingFileId) {
  return repo_.isInBatch(pendingFileId);
}

std::optional<BatchInfo> BatchTaskService::getBatch(int batchId) {
  return repo_.findBatch(batchId);
}

std::vector<BatchInfo> BatchTaskService::getIncompleteBatches() {
  return repo_.findIncompleteBatches();
}

std::vector<BatchInfo>
BatchTaskService::getEncodingBatchesByStreamer(const std::string &streamer) {
  return repo_.findEncodingByStreamer(streamer);
}

std::vector<std::chrono::system_clock::time_point>
BatchTaskService::getBatchFileTimes(int batchId) {
  std::vector<std::chrono::system_clock::time_point> times;

  auto filenames = repo_.findBatchFilenames(batchId);
  for (const auto &filename : filenames) {
    auto t = MergerService::parseTime(filename);
    if (t) {
      times.push_back(*t);
    }
  }

  return times;
}

bool BatchTaskService::addFilesToBatch(
    int batchId, const std::vector<BatchInputFile> &files) {
  bool success = repo_.addFilesToBatch(batchId, files);
  if (success) {
    LOG_INFO << "[addFilesToBatch] Added " << files.size()
             << " files to batch id=" << batchId;
  }
  return success;
}

std::vector<BatchAssignment> BatchTaskService::groupAndAssignBatches(
    const std::vector<StableFile> &stableFiles, int mergeWindowSeconds) {
  std::vector<BatchAssignment> result;

  // 1. 按主播名分组
  std::map<std::string, std::vector<StableFile>> groupedByStreamer;
  for (const auto &sf : stableFiles) {
    fs::path filePath(sf.pf.getFilepath());
    std::string filename = filePath.filename().string();
    std::string streamer = MergerService::parseTitle(filename);
    if (streamer.empty()) {
      LOG_WARN << "[groupAndAssignBatches] Could not parse streamer for: "
               << filename;
      continue;
    }
    groupedByStreamer[streamer].push_back(sf);
  }

  // 2. 处理每个主播分组
  for (auto &[streamer, files] : groupedByStreamer) {
    // 按时间降序排列
    std::sort(files.begin(), files.end(),
              [](const StableFile &a, const StableFile &b) {
                return a.time > b.time;
              });

    // 按时间窗口分批
    std::set<size_t> assigned;
    std::vector<std::vector<StableFile>> newBatches;

    for (size_t i = 0; i < files.size(); ++i) {
      if (assigned.count(i))
        continue;

      std::vector<StableFile> batch;
      batch.push_back(files[i]);
      assigned.insert(i);

      for (size_t j = i + 1; j < files.size(); ++j) {
        if (assigned.count(j))
          continue;
        auto gap = std::chrono::duration_cast<std::chrono::seconds>(
                       batch.back().time - files[j].time)
                       .count();
        if (gap <= mergeWindowSeconds) {
          batch.push_back(files[j]);
          assigned.insert(j);
        } else {
          break;
        }
      }

      newBatches.push_back(std::move(batch));
    }

    // 3. 对每个新批次，尝试与已有 encoding 批次合并
    auto existingBatches = getEncodingBatchesByStreamer(streamer);

    for (auto &batch : newBatches) {
      if (existingBatches.empty()) {
        // 没有已有批次，全部作为新批次
        BatchAssignment assign;
        assign.batchId = -1;
        assign.streamer = streamer;
        assign.files = std::move(batch);
        result.push_back(std::move(assign));
        continue;
      }

      // 查找可合并的已有批次
      bool merged = false;
      for (const auto &existBatch : existingBatches) {
        auto existTimes = getBatchFileTimes(existBatch.id);
        if (existTimes.empty())
          continue;

        // 取已有批次中最早的时间
        auto earliestTime =
            *std::min_element(existTimes.begin(), existTimes.end());

        // 拆分新文件：可合并 vs 不可合并
        std::vector<StableFile> mergeable;
        std::vector<StableFile> nonMergeable;

        for (auto &sf : batch) {
          auto diff = std::abs(std::chrono::duration_cast<std::chrono::seconds>(
                                   sf.time - earliestTime)
                                   .count());
          if (diff <= mergeWindowSeconds) {
            mergeable.push_back(sf);
          } else {
            nonMergeable.push_back(sf);
          }
        }

        if (!mergeable.empty()) {
          // 有可合并的文件
          BatchAssignment mergeAssign;
          mergeAssign.batchId = existBatch.id;
          mergeAssign.streamer = streamer;
          mergeAssign.files = std::move(mergeable);
          result.push_back(std::move(mergeAssign));

          LOG_INFO << "[groupAndAssignBatches] Merging "
                   << result.back().files.size()
                   << " files into existing batch id=" << existBatch.id
                   << " for streamer '" << streamer << "'";
        }

        if (!nonMergeable.empty()) {
          // 不可合并的文件作为新批次
          BatchAssignment newAssign;
          newAssign.batchId = -1;
          newAssign.streamer = streamer;
          newAssign.files = std::move(nonMergeable);
          result.push_back(std::move(newAssign));
        }

        merged = true;
        break; // 只尝试合并到第一个找到的已有批次
      }

      if (!merged) {
        // 没有找到可合并的已有批次
        BatchAssignment assign;
        assign.batchId = -1;
        assign.streamer = streamer;
        assign.files = std::move(batch);
        result.push_back(std::move(assign));
      }
    }
  }

  return result;
}
