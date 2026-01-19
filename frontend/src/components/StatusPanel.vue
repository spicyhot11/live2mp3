<script setup>
import { ref, onMounted, computed } from 'vue'
import api from '../api'

const status = ref(null)
const detailedStatus = ref(null)
const loading = ref(false)
const scanningDisk = ref(false)
const message = ref('')
const diskStats = ref(null)
const prevCpuStats = ref(null)

const fetchStatus = async () => {
  try {
    const res = await api.getStatus()
    status.value = res.data
  } catch (e) {
    message.value = '获取状态错误'
  }
}

const fetchDetailedStatus = async () => {
  try {
    const res = await api.getDetailedStatus()
    detailedStatus.value = res.data
    
    // Calculate CPU percentage from delta
    if (res.data.system && res.data.system.cpu_total) {
      const current = { total: res.data.system.cpu_total, active: res.data.system.cpu_active }
      if (prevCpuStats.value) {
        const deltaTotal = current.total - prevCpuStats.value.total
        const deltaActive = current.active - prevCpuStats.value.active
        if (deltaTotal > 0) {
          detailedStatus.value.system.cpu_percent = ((deltaActive / deltaTotal) * 100).toFixed(1)
        }
      }
      prevCpuStats.value = current
    }
  } catch (e) {
    console.error('获取详细状态错误', e)
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
    message.value = '任务触发成功 (立即处理所有暂存文件)'
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

const phaseLabel = computed(() => {
  if (!detailedStatus.value?.task?.current_phase) return ''
  const phases = {
    'stability_scan': '稳定性扫描',
    'conversion': '转换中',
    'merge_output': '合并输出'
  }
  return phases[detailedStatus.value.task.current_phase] || detailedStatus.value.task.current_phase
})

const memoryPercent = computed(() => {
  if (!detailedStatus.value?.system?.mem_total_kb) return 0
  const total = detailedStatus.value.system.mem_total_kb
  const used = detailedStatus.value.system.mem_used_kb
  return ((used / total) * 100).toFixed(1)
})

const formatMemory = (kb) => {
  if (!kb) return '0 MB'
  if (kb >= 1024 * 1024) {
    return (kb / 1024 / 1024).toFixed(2) + ' GB'
  }
  return (kb / 1024).toFixed(0) + ' MB'
}

onMounted(() => {
  fetchStatus()
  fetchDetailedStatus()
  fetchDashboardStats()
  triggerDiskScan() // Scan on mounted
  setInterval(fetchStatus, 5000) // Poll status
  setInterval(fetchDetailedStatus, 2000) // Poll detailed status more frequently
  setInterval(fetchDashboardStats, 3000) // Poll disk stats
})
</script>

<template>
  <div class="status-panel">
    <h2>系统仪表盘</h2>
    
    <div v-if="status" class="status-card">
      <p><strong>状态:</strong> {{ status.status }}</p>
      <p><strong>后端:</strong> {{ status.backend }} {{ status.version }}</p>
    </div>

    <!-- Conversion Status Card -->
    <div class="status-card conversion-card">
      <div class="card-header">
        <h3>转换状态</h3>
        <span v-if="detailedStatus?.task?.running" class="running-indicator">运行中</span>
        <span v-else class="idle-indicator">空闲</span>
      </div>
      
      <div v-if="detailedStatus?.task?.running" class="conversion-info">
        <div class="info-row">
          <span class="label">当前阶段:</span>
          <span class="value phase-badge">{{ phaseLabel }}</span>
        </div>
        <div v-if="detailedStatus?.task?.current_file" class="info-row">
          <span class="label">当前文件:</span>
          <span class="value file-path">{{ detailedStatus.task.current_file }}</span>
        </div>
      </div>
      <div v-else class="conversion-info idle-message">
        <span>等待任务...</span>
      </div>
      
      <!-- System Resources -->
      <div v-if="detailedStatus?.system" class="system-resources">
        <div class="resource-item">
          <span class="resource-label">CPU:</span>
          <div class="resource-bar">
            <div class="resource-fill" 
                 :style="{ width: (detailedStatus.system.cpu_percent || 0) + '%' }"
                 :class="{ 'warning': parseFloat(detailedStatus.system.cpu_percent) > 80 }">
            </div>
          </div>
          <span class="resource-value">{{ detailedStatus.system.cpu_percent || 0 }}%</span>
        </div>
        <div class="resource-item">
          <span class="resource-label">内存:</span>
          <div class="resource-bar">
            <div class="resource-fill" 
                 :style="{ width: memoryPercent + '%' }"
                 :class="{ 'warning': parseFloat(memoryPercent) > 80 }">
            </div>
          </div>
          <span class="resource-value">{{ formatMemory(detailedStatus.system.mem_used_kb) }} / {{ formatMemory(detailedStatus.system.mem_total_kb) }}</span>
        </div>
      </div>
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
             <div v-for="(item, idx) in diskStats.locations" :key="idx" class="disk-item" :class="{ 'temp-item': item.label === 'Temp' }">
                 <div class="disk-info">
                     <span class="disk-label">{{ item.label === 'Temp' ? '临时' : item.label === 'Output' ? '输出' : '源' }}:</span>
                     <span class="disk-path">{{ item.path }}</span>
                 </div>
                 <div class="disk-usage">
                      <span v-if="item.error" class="error">{{ item.error }}</span>
                      <template v-else>
                          <span>
                              已用: {{ (item.used_size / 1024 / 1024 / 1024).toFixed(2) }} GB
                              <template v-if="item.label === 'Temp' && item.size_limit_mb > 0">
                                  / 限制: {{ (item.size_limit_mb / 1024).toFixed(2) }} GB
                              </template>
                              <template v-else>
                                  / 剩余: {{ (item.free_space / 1024 / 1024 / 1024).toFixed(2) }} GB
                              </template>
                          </span>
                          <div v-if="item.label === 'Temp' && item.size_limit_mb > 0" class="progress-bar">
                              <div class="progress-fill" 
                                   :style="{ width: Math.min(100, (item.used_size / (item.size_limit_mb * 1024 * 1024)) * 100).toFixed(1) + '%' }"
                                   :class="{ 'warning': (item.used_size / (item.size_limit_mb * 1024 * 1024)) > 0.8 }">
                              </div>
                          </div>
                      </template>
                 </div>
             </div>
        </div>
        <div v-else-if="diskStats && diskStats.error">
            <p class="error">错误: {{ diskStats.error }}</p>
        </div>
    </div>

    <div v-if="message" class="message">{{ message }}</div>

    <button class="trigger-btn" @click="triggerTask" :disabled="loading">
      {{ loading ? '运行中...' : '立即触发转换' }}
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

/* Conversion status card */
.conversion-card {
    border-left: 3px solid #3498db;
}
.running-indicator {
    background: #27ae60;
    color: white;
    padding: 3px 10px;
    border-radius: 12px;
    font-size: 0.8em;
    animation: pulse 1.5s infinite;
}
.idle-indicator {
    background: #555;
    color: #aaa;
    padding: 3px 10px;
    border-radius: 12px;
    font-size: 0.8em;
}
@keyframes pulse {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.6; }
}
.conversion-info {
    margin: 12px 0;
}
.idle-message {
    color: #888;
    font-style: italic;
}
.info-row {
    display: flex;
    gap: 10px;
    margin-bottom: 8px;
    align-items: center;
}
.info-row .label {
    color: #888;
    min-width: 70px;
}
.phase-badge {
    background: #2a5298;
    padding: 2px 8px;
    border-radius: 4px;
    font-size: 0.9em;
}
.file-path {
    color: #aaa;
    font-size: 0.85em;
    word-break: break-all;
}
.system-resources {
    border-top: 1px solid #444;
    padding-top: 12px;
    margin-top: 12px;
}
.resource-item {
    display: flex;
    align-items: center;
    gap: 10px;
    margin-bottom: 8px;
}
.resource-label {
    color: #888;
    min-width: 40px;
}
.resource-bar {
    flex: 1;
    height: 8px;
    background: #444;
    border-radius: 4px;
    overflow: hidden;
}
.resource-fill {
    height: 100%;
    background: linear-gradient(90deg, #3498db, #2980b9);
    transition: width 0.3s ease;
}
.resource-fill.warning {
    background: linear-gradient(90deg, #e67e22, #d35400);
}
.resource-value {
    color: #aaa;
    font-size: 0.85em;
    min-width: 120px;
    text-align: right;
}

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

/* Temp directory styles */
.temp-item {
    border-left: 3px solid #3498db;
}
.progress-bar {
    margin-top: 8px;
    height: 8px;
    background: #444;
    border-radius: 4px;
    overflow: hidden;
}
.progress-fill {
    height: 100%;
    background: linear-gradient(90deg, #2ecc71, #27ae60);
    transition: width 0.3s ease;
}
.progress-fill.warning {
    background: linear-gradient(90deg, #e67e22, #d35400);
}
</style>
