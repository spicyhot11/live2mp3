#pragma once
#include <drogon/HttpController.h>

using namespace drogon;

class FileController : public drogon::HttpController<FileController> {
public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(FileController::listDirectories, "/api/files/list", Post);
  METHOD_LIST_END

  void listDirectories(const HttpRequestPtr &req,
                       std::function<void(const HttpResponsePtr &)> &&callback);
};
