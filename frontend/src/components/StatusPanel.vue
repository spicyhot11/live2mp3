<script setup>
import { ref, onMounted } from 'vue'
import api from '../api'

const status = ref(null)
const loading = ref(false)
const scanningDisk = ref(false)
const message = ref('')
const diskStats = ref(null)

const fetchStatus = async () => {
  try {
    const res = await api.getStatus()
    status.value = res.data
  } catch (e) {
    message.value = '获取状态错误'
  }
}

const fetchDashboardStats = async () => {
  try {
    const res = await api.getDashboardStats()
    if (res.data.disk) {
        diskStats.value = res.data.disk
        scanningDisk.value = res.data.disk.is_scanning || false
    }
  } catch (e) {
    console.error('获取仪表盘错误', e)
  }
}

const triggerTask = async () => {
  loading.value = true
  try {
    await api.triggerTask()
    message.value = '任务触发成功'
    setTimeout(() => message.value = '', 3000)
  } catch (e) {
    message.value = '触发任务错误: ' + e.message
  } finally {
    loading.value = false
  }
}

const triggerDiskScan = async () => {
    scanningDisk.value = true
    try {
        const res = await api.triggerDiskScan()
        if(res.data.status === 'started') {
            message.value = '磁盘扫描已开始...'
            setTimeout(() => message.value = '', 2000)
        }
    } catch(e) {
        message.value = '触发扫描失败: ' + e.message
        scanningDisk.value = false
    }
}

onMounted(() => {
  fetchStatus()
  fetchDashboardStats()
  triggerDiskScan() // Scan on mounted
  setInterval(fetchStatus, 5000) // Poll status
  setInterval(fetchDashboardStats, 3000) // Poll disk stats more frequently
})
</script>

<template>
  <div class="status-panel">
    <h2>系统仪表盘</h2>
    
    <div v-if="status" class="status-card">
      <p><strong>状态:</strong> {{ status.status }}</p>
      <p><strong>后端:</strong> {{ status.backend }} {{ status.version }}</p>
    </div>

    <!-- Disk Stats Section -->
    <div class="status-card disk-card">
        <div class="card-header">
            <h3>磁盘使用情况</h3>
            <button @click="triggerDiskScan" :disabled="scanningDisk" class="refresh-btn" title="刷新">↻</button>
        </div>
        
        <div v-if="scanningDisk" class="overlay">
            <div class="spinner"></div>
            <span>扫描中...</span>
        </div>

        <div v-if="diskStats && diskStats.locations" class="disk-list">
             <div v-for="(item, idx) in diskStats.locations" :key="idx" class="disk-item">
                 <div class="disk-info">
                     <span class="disk-label">{{ item.label }}:</span>
                     <span class="disk-path">{{ item.path }}</span>
                 </div>
                 <div class="disk-usage">
                      <span v-if="item.error" class="error">{{ item.error }}</span>
                      <span v-else>
                          已用: {{ (item.used_size / 1024 / 1024 / 1024).toFixed(2) }} GB /
                          剩余: {{ (item.free_space / 1024 / 1024 / 1024).toFixed(2) }} GB
                      </span>
                 </div>
             </div>
        </div>
        <div v-else-if="diskStats && diskStats.error">
            <p class="error">错误: {{ diskStats.error }}</p>
        </div>
    </div>

    <div v-if="message" class="message">{{ message }}</div>

    <button class="trigger-btn" @click="triggerTask" :disabled="loading">
      {{ loading ? '运行中...' : '立即触发扫描' }}
    </button>
  </div>
</template>

<style scoped>
.status-panel {
  padding: 20px;
  background: #222;
  color: #eee;
  border-radius: 8px;
  margin-bottom: 20px;
}
.status-card {
  background: #333;
  padding: 15px;
  border-radius: 4px;
  margin-bottom: 15px;
}
.disk-card {
    position: relative;
    min-height: 100px;
}
.card-header {
    display: flex; justify-content: space-between; align-items: center;
    margin-bottom: 10px;
}
.card-header h3 { margin: 0; font-size: 1.1em; }
.refresh-btn {
    background: transparent; border: 1px solid #555; color: #aaa;
    border-radius: 50%; width: 30px; height: 30px; cursor: pointer;
    display: flex; align-items: center; justify-content: center;
}
.refresh-btn:hover { background: #444; color: white; }

.disk-list { display: flex; flex-direction: column; gap: 8px; }
.disk-item {
    background: #2a2a2a; padding: 8px; border-radius: 4px;
    font-size: 0.9em;
}
.disk-info { display: flex; gap: 10px; margin-bottom: 4px; color: #888; }
.disk-path { color: #ccc; word-break: break-all; }
.disk-usage { font-weight: bold; color: #ddd; }
.error { color: #e76f51; }

.overlay {
    position: absolute; top: 0; left: 0; right: 0; bottom: 0;
    background: rgba(0,0,0,0.7);
    display: flex; flex-direction: column; align-items: center; justify-content: center;
    border-radius: 4px;
    z-index: 10;
}
.spinner {
    border: 4px solid #f3f3f3; border-top: 4px solid #3498db;
    border-radius: 50%; width: 30px; height: 30px;
    animation: spin 1s linear infinite; margin-bottom: 5px;
}
@keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
.trigger-btn {
  width: 100%;
  padding: 15px;
  background: #e76f51;
  color: white;
  border: none;
  border-radius: 4px;
  font-size: 1.2em;
  cursor: pointer;
}
.trigger-btn:hover {
  background: #d65d3e;
}
.trigger-btn:disabled {
  background: #777;
  cursor: not-allowed;
}
.message {
  padding: 10px;
  background: #264653;
  margin-bottom: 10px;
  border-radius: 4px;
}
</style>
