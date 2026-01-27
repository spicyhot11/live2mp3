#include "FileUtils.h"
#include <fstream>
#include <iomanip>
#include <openssl/evp.h>
#include <sstream>

namespace live2mp3::utils {

std::string calculateMD5(const std::string &filepath) {
  std::ifstream file(filepath, std::ios::binary);
  if (!file)
    return "";

  EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
  const EVP_MD *md = EVP_md5();

  EVP_DigestInit_ex(mdctx, md, nullptr);

  char buffer[4096];
  while (file.read(buffer, sizeof(buffer))) {
    EVP_DigestUpdate(mdctx, buffer, file.gcount());
  }
  // Handle remaining bytes
  if (file.gcount() > 0) {
    EVP_DigestUpdate(mdctx, buffer, file.gcount());
  }

  unsigned char result[EVP_MAX_MD_SIZE];
  unsigned int resultLen;
  EVP_DigestFinal_ex(mdctx, result, &resultLen);
  EVP_MD_CTX_free(mdctx);

  std::stringstream ss;
  for (unsigned int i = 0; i < resultLen; ++i) {
    ss << std::hex << std::setw(2) << std::setfill('0') << (int)result[i];
  }
  return ss.str();
}

} // namespace live2mp3::utils
