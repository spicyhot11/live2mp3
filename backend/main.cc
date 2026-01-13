#include <drogon/drogon.h>

int main() {
    // 加载配置文件
    drogon::app().loadConfigFile("config.json");
    
    // 启动 HTTP 服务
    LOG_INFO << "Server running on 0.0.0.0:8080";
    
    // 设置静态文件根目录 (指向 Vue 编译后的 dist 目录)
    // 在 Docker 中，我们将把 dist 挂载或复制到 /app/dist
    drogon::app().setDocumentRoot("./dist");
    
    // 设置主页
    drogon::app().setHomePage("index.html");

    drogon::app().run();
    return 0;
}
