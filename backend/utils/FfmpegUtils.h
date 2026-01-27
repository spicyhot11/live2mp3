#pragma once
#include <string>


namespace live2mp3::utils{

  struct FfmpegPipeInfo {
    int time;                   ///< 媒体时长
    int frame;                  ///< 处理帧数
    int fps;                    ///< 处理帧率
    int bitrate;                ///< 处理码率
    int size;                   ///< 处理文件大小
  };

  FfmpegPipeInfo getFfmpegPipeInfo(const std::string &pipe);

}

