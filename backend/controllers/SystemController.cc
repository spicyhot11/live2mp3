#include "SystemController.h"

void SystemController::getStatus(const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> &&callback)
{
    Json::Value ret;
    ret["status"] = "online";
    ret["version"] = "1.0.0";
    ret["backend"] = "drogon-cpp";
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    callback(resp);
}
