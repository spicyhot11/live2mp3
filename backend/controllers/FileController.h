#pragma once
#include <drogon/HttpController.h>

using namespace drogon;

/**
 * @brief 简单文件控制器类
 *
 * 仅用于列出目录功能，相比 FileBrowserController 功能更简单。
 * 可能用于特定的文件选择场景。
 */
class FileController : public drogon::HttpController<FileController> {
public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(FileController::listDirectories, "/api/files/list", Post);
  METHOD_LIST_END

  /**
   * @brief 列出目录内容
   *
   * @param req HTTP请求对象
   * @param callback 回调函数
   */
  void listDirectories(const HttpRequestPtr &req,
                       std::function<void(const HttpResponsePtr &)> &&callback);
};
