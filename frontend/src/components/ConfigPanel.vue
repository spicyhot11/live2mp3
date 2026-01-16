<script setup>
import { ref, onMounted } from 'vue'
import api from '../api'

const config = ref({
  scanner: { video_roots: [], extensions: [], allow_list: [], deny_list: [] },
  output: { output_root: '', keep_original: false },
  scheduler: { scan_interval_seconds: 60, merge_window_hours: 2 }
})

const loading = ref(false)
const message = ref('')

const fetchConfig = async () => {
  loading.value = true
  try {
    const res = await api.getConfig()
    config.value = res.data
  } catch (e) {
    message.value = '获取配置错误: ' + e.message
  } finally {
    loading.value = false
  }
}

const saveConfig = async () => {
  loading.value = true
  try {
    await api.updateConfig(config.value)
    message.value = '配置保存成功！'
    setTimeout(() => message.value = '', 3000)
  } catch (e) {
    message.value = '保存配置错误: ' + e.message
  } finally {
    loading.value = false
  }
}

const addArrayItem = (arr) => {
  arr.push('')
}

const removeArrayItem = (arr, index) => {
  arr.splice(index, 1)
}

onMounted(() => {
  fetchConfig()
})
</script>

<template>
  <div class="config-panel">
    <h2>配置</h2>
    
    <div v-if="loading" class="loading">加载中...</div>
    <div v-if="message" class="message">{{ message }}</div>

    <div class="section">
      <h3>扫描器设置</h3>
      
      <div class="form-group">
        <label>视频根目录 (扫描路径)</label>
        <div v-for="(item, index) in config.scanner.video_roots" :key="index" class="array-item">
          <input v-model="config.scanner.video_roots[index]" placeholder="/path/to/videos" />
          <button @click="removeArrayItem(config.scanner.video_roots, index)">X</button>
        </div>
        <button @click="addArrayItem(config.scanner.video_roots)">+ 添加路径</button>
      </div>

      <div class="form-group">
        <label>扩展名</label>
        <div v-for="(item, index) in config.scanner.extensions" :key="index" class="array-item">
          <input v-model="config.scanner.extensions[index]" placeholder=".mp4" />
          <button @click="removeArrayItem(config.scanner.extensions, index)">X</button>
        </div>
        <button @click="addArrayItem(config.scanner.extensions)">+ 添加扩展名</button>
      </div>
      
      <div class="form-group">
        <label>白名单 (正则匹配)</label>
        <div v-for="(item, index) in config.scanner.allow_list" :key="index" class="array-item">
          <input v-model="config.scanner.allow_list[index]" placeholder="Regex Pattern" />
          <button @click="removeArrayItem(config.scanner.allow_list, index)">X</button>
        </div>
        <button @click="addArrayItem(config.scanner.allow_list)">+ 添加规则</button>
      </div>
    </div>

    <div class="section">
      <h3>输出设置</h3>
      <div class="form-group">
        <label>输出根目录</label>
        <input v-model="config.output.output_root" placeholder="/path/to/output" />
      </div>
      <div class="form-group checkbox">
         <label>
           <input type="checkbox" v-model="config.output.keep_original" />
           保留原始文件
         </label>
      </div>
    </div>

    <div class="section">
      <h3>调度器</h3>
      <div class="form-group">
        <label>扫描间隔 (秒)</label>
        <input type="number" v-model.number="config.scheduler.scan_interval_seconds" />
      </div>
      <div class="form-group">
        <label>合并窗口 (小时)</label>
        <input type="number" v-model.number="config.scheduler.merge_window_hours" />
      </div>
    </div>

    <button class="save-btn" @click="saveConfig">保存配置</button>
  </div>
</template>

<style scoped>
.config-panel {
  padding: 20px;
  background: #222;
  color: #eee;
  border-radius: 8px;
}
.section {
  margin-bottom: 20px;
  border: 1px solid #444;
  padding: 15px;
  border-radius: 4px;
}
.form-group {
  margin-bottom: 15px;
}
.form-group label {
  display: block;
  margin-bottom: 5px;
  font-weight: bold;
}
input {
  width: 100%;
  padding: 8px;
  background: #333;
  border: 1px solid #555;
  color: #fff;
  border-radius: 4px;
}
.array-item {
  display: flex;
  gap: 10px;
  margin-bottom: 5px;
}
button {
  padding: 5px 10px;
  cursor: pointer;
  background: #444;
  color: #fff;
  border: none;
  border-radius: 4px;
}
button:hover {
  background: #555;
}
.save-btn {
  background: #2a9d8f;
  width: 100%;
  padding: 12px;
  font-size: 1.1em;
}
.save-btn:hover {
  background: #261447;
}
.message {
  padding: 10px;
  background: #264653;
  margin-bottom: 10px;
  border-radius: 4px;
}
</style>
