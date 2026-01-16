#include "SystemController.h"
#include "../services/ConfigService.h"

SystemController::SystemController() {
  LOG_INFO << "SystemController initialized";
  lpConfigService_ = drogon::app().getPlugin<ConfigService>();

  if (lpConfigService_ == nullptr) {
    LOG_FATAL << "ConfigService not found";
    return;
  }

  lpSchedulerService_ = drogon::app().getPlugin<SchedulerService>();

  if (lpSchedulerService_ == nullptr) {
    LOG_FATAL << "SchedulerService not found";
    return;
  }
}

void SystemController::getStatus(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  Json::Value ret;
  ret["status"] = "online";
  ret["version"] = "1.0.0";
  ret["backend"] = "drogon-cpp";

  auto resp = HttpResponse::newHttpJsonResponse(ret);
  callback(resp);
}

void SystemController::getConfig(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto json = lpConfigService_->toJson();
  // Start with a simpler approach: Return the raw config object
  // Drogon Json response needs Json::Value (jsoncpp)
  // nlohmann::json to jsoncpp conversion or just stream it
  // Or, ConfigService can return jsoncpp?
  // Wait, ConfigService uses nlohmann::json.
  // I should probably switch ConfigService to use jsoncpp OR convert nlohmann
  // to string then parse with jsoncpp for response? Easiest: nlohmann::json
  // dump -> string -> logic Let's assume standard interaction.

  std::string configStr = json.dump();
  Json::Value ret;
  Json::Reader reader;
  reader.parse(configStr, ret);

  auto resp = HttpResponse::newHttpJsonResponse(ret);
  callback(resp);
}

void SystemController::updateConfig(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto jsonPtr = req->getJsonObject();
  if (!jsonPtr) {
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k400BadRequest);
    resp->setBody("Invalid JSON");
    callback(resp);
    return;
  }

  try {
    // Convert jsoncpp to string then to nlohmann::json
    Json::FastWriter writer;
    std::string str = writer.write(*jsonPtr);
    nlohmann::json j = nlohmann::json::parse(str);

    ScannerConfig scanner;
    if (j.contains("scanner"))
      j.at("scanner").get_to(scanner);

    OutputConfig output;
    if (j.contains("output"))
      j.at("output").get_to(output);

    SchedulerConfig scheduler;
    if (j.contains("scheduler"))
      j.at("scheduler").get_to(scheduler);

    AppConfig newConfig;
    newConfig.scanner = scanner;
    newConfig.output = output;
    newConfig.scheduler = scheduler;

    lpConfigService_->updateConfig(newConfig);
    lpConfigService_->saveConfig();

    Json::Value ret;
    ret["status"] = "updated";
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    callback(resp);
  } catch (const std::exception &e) {
    Json::Value ret;
    ret["error"] = e.what();
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }
}

void SystemController::triggerTask(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  lpSchedulerService_->triggerNow();
  Json::Value ret;
  ret["status"] = "triggered";
  auto resp = HttpResponse::newHttpJsonResponse(ret);
  callback(resp);
}
