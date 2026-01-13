#pragma once
#include <drogon/HttpController.h>

using namespace drogon;

class SystemController : public drogon::HttpController<SystemController>
{
  public:
    METHOD_LIST_BEGIN
    // Map REST API endpoints
    ADD_METHOD_TO(SystemController::getStatus, "/api/status", Get);
    METHOD_LIST_END

    void getStatus(const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> &&callback);
};
