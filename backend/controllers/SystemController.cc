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

void SystemController::getDetailedStatus(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  Json::Value ret;
  ret["status"] = "online";
  ret["version"] = "1.0.0";

  // Get scheduler status
  bool running = lpSchedulerService_->isRunning();
  std::string currentFile = lpSchedulerService_->getCurrentFile();
  std::string currentPhase = lpSchedulerService_->getCurrentPhase();

  ret["task"]["running"] = running;
  ret["task"]["current_file"] = currentFile;
  ret["task"]["current_phase"] = currentPhase;

  // Get system resource usage
  // CPU usage (simplified - read from /proc/stat)
  std::ifstream statFile("/proc/stat");
  if (statFile.is_open()) {
    std::string line;
    std::getline(statFile, line);
    // Parse cpu line: cpu user nice system idle iowait irq softirq
    unsigned long long user, nice, sys, idle, iowait, irq, softirq, steal;
    if (sscanf(line.c_str(), "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
               &user, &nice, &sys, &idle, &iowait, &irq, &softirq,
               &steal) >= 4) {
      unsigned long long total =
          user + nice + sys + idle + iowait + irq + softirq + steal;
      unsigned long long active = user + nice + sys + irq + softirq + steal;
      ret["system"]["cpu_total"] = (Json::Value::UInt64)total;
      ret["system"]["cpu_active"] = (Json::Value::UInt64)active;
    }
    statFile.close();
  }

  // Memory usage from /proc/meminfo
  std::ifstream memFile("/proc/meminfo");
  if (memFile.is_open()) {
    std::string line;
    unsigned long long memTotal = 0, memFree = 0, memAvailable = 0, buffers = 0,
                       cached = 0;
    while (std::getline(memFile, line)) {
      if (line.find("MemTotal:") == 0)
        sscanf(line.c_str(), "MemTotal: %llu kB", &memTotal);
      else if (line.find("MemFree:") == 0)
        sscanf(line.c_str(), "MemFree: %llu kB", &memFree);
      else if (line.find("MemAvailable:") == 0)
        sscanf(line.c_str(), "MemAvailable: %llu kB", &memAvailable);
      else if (line.find("Buffers:") == 0)
        sscanf(line.c_str(), "Buffers: %llu kB", &buffers);
      else if (line.find("Cached:") == 0 && line.find("SwapCached:") != 0)
        sscanf(line.c_str(), "Cached: %llu kB", &cached);
    }
    memFile.close();

    unsigned long long memUsed = memTotal - memAvailable;
    ret["system"]["mem_total_kb"] = (Json::Value::UInt64)memTotal;
    ret["system"]["mem_used_kb"] = (Json::Value::UInt64)memUsed;
    ret["system"]["mem_available_kb"] = (Json::Value::UInt64)memAvailable;
  }

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

    TempConfig temp;
    if (j.contains("temp"))
      j.at("temp").get_to(temp);

    AppConfig newConfig;
    newConfig.scanner = scanner;
    newConfig.output = output;
    newConfig.scheduler = scheduler;
    newConfig.temp = temp;

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
