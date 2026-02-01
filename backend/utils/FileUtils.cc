#include "FileUtils.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <xxhash.h>

namespace fs = std::filesystem;

namespace live2mp3::utils {

// 采样大小：50KB
constexpr size_t SAMPLE_SIZE = 51200;

std::string calculateFileFingerprint(const std::string &filepath) {
  std::error_code ec;

  // 1. 获取文件元数据
  auto size = fs::file_size(filepath, ec);
  if (ec)
    return "";

  auto mtime = fs::last_write_time(filepath, ec);
  if (ec)
    return "";

  // 2. 打开文件
  std::ifstream file(filepath, std::ios::binary);
  if (!file)
    return "";

  // 3. 初始化 xxHash 状态
  XXH64_state_t *state = XXH64_createState();
  if (!state)
    return "";
  XXH64_reset(state, 0);

  // 加入文件大小
  XXH64_update(state, &size, sizeof(size));

  // 加入修改时间
  auto mtimeVal = mtime.time_since_epoch().count();
  XXH64_update(state, &mtimeVal, sizeof(mtimeVal));

  // 4. 读取采样数据
  std::vector<char> buffer(SAMPLE_SIZE);

  if (size <= SAMPLE_SIZE * 2) {
    // 小文件（≤100KB）：读取全部内容
    while (file.read(buffer.data(), buffer.size()) || file.gcount() > 0) {
      XXH64_update(state, buffer.data(), static_cast<size_t>(file.gcount()));
      if (static_cast<size_t>(file.gcount()) < buffer.size())
        break;
    }
  } else {
    // 大文件：只读取头部和尾部各 50KB
    // 头部 50KB
    file.read(buffer.data(), SAMPLE_SIZE);
    XXH64_update(state, buffer.data(), static_cast<size_t>(file.gcount()));

    // 尾部 50KB
    file.seekg(-static_cast<std::streamoff>(SAMPLE_SIZE), std::ios::end);
    file.read(buffer.data(), SAMPLE_SIZE);
    XXH64_update(state, buffer.data(), static_cast<size_t>(file.gcount()));
  }

  // 5. 计算最终哈希值
  XXH64_hash_t hash = XXH64_digest(state);
  XXH64_freeState(state);

  // 格式化为 16 位十六进制字符串
  std::stringstream ss;
  ss << std::hex << std::setw(16) << std::setfill('0') << hash;
  return ss.str();
}

} // namespace live2mp3::utils
