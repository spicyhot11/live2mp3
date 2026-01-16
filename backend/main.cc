
#include <drogon/drogon.h>

int main() {
  // 加载配置文件 (Drogon internal)
  drogon::app().loadConfigFile("config.json");

  drogon::app().run();
  return 0;
}
