

#include <drogon/drogon.h>

int main() {
  // 加载 Drogon 框架配置 (不包含 listener)
  drogon::app().loadConfigFile("config.json");
  drogon::app().run();
  return 0;
}
