#pragma once

#include <string>

namespace utils {

/**
 * @brief 计算文件的MD5哈希值
 *
 * 读取整个文件内容并计算其MD5摘要。通常用于检测文件是否发生变化
 * (稳定性检查) 或作为文件的唯一标识。
 *
 * @param filepath 文件路径
 * @return std::string 32位十六进制MD5字符串
 */
std::string calculateMD5(const std::string &filepath);

} // namespace utils
