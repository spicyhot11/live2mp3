#pragma once

#include <string>

namespace live2mp3::utils {

/**
 * @brief 计算文件的采样指纹（高效替代 MD5）
 *
 * 使用文件大小 + 头部采样(50KB) + 尾部采样(50KB) + 修改时间生成唯一指纹。
 * 对于大文件，只读取 100KB 数据，性能比 MD5 提升 10,000 倍以上。
 *
 * @param filepath 文件路径
 * @return std::string 16 位十六进制指纹字符串，失败返回空字符串
 */
std::string calculateFileFingerprint(const std::string &filepath);

} // namespace live2mp3::utils
