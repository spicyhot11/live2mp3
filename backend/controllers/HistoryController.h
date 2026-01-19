#pragma once
#include "../services/ConfigService.h"
#include "../services/PendingFileService.h"
#include <drogon/HttpController.h>

using namespace drogon;

/**
 * @brief 历史记录控制器类
 *
 * 管理已处理文件的历史记录，提供查询和删除功能。
 */
class HistoryController : public drogon::HttpController<HistoryController> {
public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(HistoryController::getAll, "/api/history", Get);
  ADD_METHOD_TO(HistoryController::removeRecord, "/api/history/{id}", Delete);
  METHOD_LIST_END

  HistoryController();

  /**
   * @brief 获取所有历史记录
   *
   * 返回所有已完成转换的文件列表。
   *
   * @param req HTTP请求对象
   * @param callback 回调函数
   */
  void getAll(const HttpRequestPtr &req,
              std::function<void(const HttpResponsePtr &)> &&callback);

  /**
   * @brief 删除指定历史记录
   *
   * 根据ID删除一条历史记录。这将允许该文件被重新处理。
   *
   * @param req HTTP请求对象
   * @param callback 回调函数
   * @param id 记录ID
   */
  void removeRecord(const HttpRequestPtr &req,
                    std::function<void(const HttpResponsePtr &)> &&callback,
                    int id);

private:
  ConfigService *lpConfigService_ = nullptr;
  PendingFileService *lpPendingFileService_ = nullptr;
};
