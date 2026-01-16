#pragma once

#include <drogon/plugins/Plugin.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

struct HistoryRecord {
  int id;
  std::string filepath;
  std::string filename;
  std::string md5;
  std::string processed_at;
};

void to_json(nlohmann::json &j, const HistoryRecord &p);

class HistoryService : public drogon::Plugin<HistoryService> {
public:
  void initAndStart(const Json::Value &config) override;
  void shutdown() override;

  bool hasProcessed(const std::string &md5);
  bool addRecord(const std::string &filepath, const std::string &filename,
                 const std::string &md5);
  bool removeRecord(int id);
  std::vector<HistoryRecord> getAll();

private:
  HistoryService() = default;
};
