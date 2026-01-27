#include "FfmpegTaskService.h"
#include "ConverterService.h"
#include "MergerService.h"
#include <algorithm>
#include <chrono>
#include <drogon/drogon.h>

using namespace drogon;



FfAsyncChannel::FfAsyncChannel(size_t capacity, std::shared_ptr<CommonThreadService> threadServicePtr) : semaphore_(capacity), threadServicePtr_(threadServicePtr) {
  
}

FfAsyncChannel::~FfAsyncChannel() {
  close();
}

void FfAsyncChannel::close() {
    // todo 补充关闭逻辑
}

drogon::Task<std::string> FfAsyncChannel::send(FfmpegTaskInput item) {

  std::shared_ptr<FfmpegTaskProcDetail> taskProcDetail = FfmpegTaskProcDetail::getInstance(item);
  std::string taskId = taskProcDetail->getId();

  bool getProc = co_await semaphore_.acquire();
  if (!getProc) {
      LOG_WARN << "FfmpegTaskService: get thread resource failed type: " << (int)item.type;
      semaphore_.release();
      co_return taskId;
  }
  
  threadServicePtr_->runTask(std::weak_ptr<ThreadTaskInterface>(taskProcDetail));
  semaphore_.release();
  
  co_return taskId;
}   
