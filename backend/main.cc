
#include <drogon/drogon.h>
#include "services/CommonThreadService.h"

int main() {
  // 加载 Drogon 框架配置 (不包含 listener)
  drogon::app().loadConfigFile("config.json");
  drogon::app().run();

  auto* threadService = drogon::app().getPlugin<CommonThreadService>();
      if (threadService) {
    // 这里的 shutdown() 会调用 queue.stop() -> join()
    // 因为这时候 FfmpegTask 还没销毁，Worker 线程可以顺利跑完任务并退出。
    threadService->shutdown();
    LOG_INFO << "CommonThreadService stopped manually.";
  }
  return 0;
}
