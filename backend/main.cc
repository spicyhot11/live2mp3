
#include <drogon/drogon.h>
#include <fstream>
#include <toml++/toml.hpp>

int main() {
  // 加载 Drogon 框架配置 (不包含 listener)
  drogon::app().loadConfigFile("config.json");

  // 从用户配置文件读取端口
  uint16_t port = 8080; // 默认端口
  std::string configPath = "./user_config.toml";

  try {
    toml::table tbl = toml::parse_file(configPath);
    if (auto server = tbl["server"].as_table()) {
      if (auto p = (*server)["port"].value<int64_t>()) {
        port = static_cast<uint16_t>(*p);
      }
    }
    LOG_INFO << "Loaded server port from user config: " << port;
  } catch (const toml::parse_error &e) {
    LOG_WARN << "Could not parse " << configPath << ": " << e.description()
             << ". Using default port " << port;
  } catch (const std::exception &e) {
    LOG_WARN << "Could not load " << configPath << ": " << e.what()
             << ". Using default port " << port;
  }

  // 添加监听器
  drogon::app().addListener("0.0.0.0", port);
  LOG_INFO << "Server will listen on 0.0.0.0:" << port;

  drogon::app().run();
  return 0;
}
