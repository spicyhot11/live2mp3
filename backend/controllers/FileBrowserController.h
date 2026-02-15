#pragma once
#include "../services/ConfigService.h"
#include "../services/PendingFileService.h"
#include "../services/ScannerService.h"
#include "../services/SchedulerService.h"
#include <drogon/HttpController.h>

using namespace drogon;

/**
 * @brief 文件浏览控制器类
 *
 * 提供文件系统浏览接口，允许用户浏览目录并手动触发目录下文件的处理。
 */
class FileBrowserController
    : public drogon::HttpController<FileBrowserController> {
public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(FileBrowserController::browseFiles, "/api/files/browse", Get);
  ADD_METHOD_TO(FileBrowserController::processDirectory, "/api/files/process",
                Post);
  METHOD_LIST_END

  FileBrowserController();

  /**
   * @brief 浏览指定目录下的文件
   *
   * 接收 `path` 查询参数，返回该目录下的文件和子目录列表。
   *
   * @param req HTTP请求对象
   * @param callback 回调函数
   */
  void browseFiles(const HttpRequestPtr &req,
                   std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief 触发处理目录下的文件
   *
   * 通常用于手动添加某个目录下的文件到待处理队列中。
   *
   * @param req HTTP请求对象
   * @param callback 回调函数
   */
  void
  processDirectory(const HttpRequestPtr &req,
                   std::function<void(const HttpResponsePtr &)> &&callback);

private:
  std::shared_ptr<ConfigService> lpConfigService_;
  std::shared_ptr<PendingFileService> lpPendingFileService_;
  std::shared_ptr<ScannerService> lpScannerService_;
  std::shared_ptr<SchedulerService> lpSchedulerService_;
};
