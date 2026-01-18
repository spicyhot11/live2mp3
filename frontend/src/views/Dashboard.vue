<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import axios from 'axios'

const stats = ref({
  status: { running: false, current_file: '' },
  disk: { locations: [], is_scanning: false, error: '' }
})

const pollInterval = ref(null)

const fetchStats = async () => {
  try {
    const res = await axios.get('/api/dashboard/stats')
    stats.value = res.data
  } catch (e) {
    console.error('Failed to fetch stats', e)
  }
}

const triggerDiskScan = async () => {
  // Immediately show overlay
  stats.value.disk.is_scanning = true
  try {
    await axios.post('/api/dashboard/disk_scan')
  } catch (e) {
    console.error('Failed to trigger disk scan', e)
    stats.value.disk.is_scanning = false
  }
}

const formatBytes = (bytes, decimals = 2) => {
  if (!+bytes) return '0 Bytes'
  const k = 1024
  const dm = decimals < 0 ? 0 : decimals
  const sizes = ['Bytes', 'KB', 'MB', 'GB', 'TB']
  const i = Math.floor(Math.log(bytes) / Math.log(k))
  return `${parseFloat((bytes / Math.pow(k, i)).toFixed(dm))} ${sizes[i]}`
}

onMounted(() => {
  fetchStats()
  triggerDiskScan() // Trigger disk scan on mount
  pollInterval.value = setInterval(fetchStats, 2000)
})

onUnmounted(() => {
  if (pollInterval.value) clearInterval(pollInterval.value)
})
</script>

<template>
  <div class="dashboard">
    <div class="grid">
      <!-- Status Card -->
      <div class="card status-card">
        <h3>运行状态</h3>
        <div class="status-content">
          <div class="status-indicator" :class="{ running: stats.status.running }">
            <span class="dot"></span>
            {{ stats.status.running ? '正在运行' : '等待中' }}
          </div>
          <div v-if="stats.status.running" class="current-file">
             <span class="label">正在处理:</span>
             <span class="filename" :title="stats.status.current_file">{{ stats.status.current_file }}</span>
          </div>
        </div>
      </div>

      <!-- Disk Usage Card -->
      <div class="card disk-card">
        <div class="card-header">
          <h3>磁盘空间</h3>
          <button @click="triggerDiskScan" :disabled="stats.disk.is_scanning" class="refresh-btn" title="刷新">↻</button>
        </div>
        
        <div v-if="stats.disk.is_scanning" class="overlay">
            <div class="spinner"></div>
            <span>扫描中...</span>
        </div>
        
        <div v-if="stats.disk.error" class="error">{{ stats.disk.error }}</div>
        <div v-else-if="stats.disk.locations && stats.disk.locations.length > 0" class="disk-content">
          <div v-for="(loc, idx) in stats.disk.locations" :key="idx" class="disk-item">
            <div class="disk-header">
              <span class="disk-label">{{ loc.label }}</span>
              <span class="disk-path" :title="loc.path">{{ loc.path }}</span>
            </div>
            <div v-if="loc.error" class="error">{{ loc.error }}</div>
            <div v-else>
              <div class="disk-bar">
                <div class="disk-fill" :style="{ width: ((loc.total_space - loc.free_space) / loc.total_space) * 100 + '%' }"></div>
              </div>
              <div class="disk-details">
                <div class="detail-item">
                  <span class="label">已用</span>
                  <span class="value">{{ formatBytes(loc.used_size) }}</span>
                </div>
                <div class="detail-item">
                  <span class="label">剩余</span>
                  <span class="value">{{ formatBytes(loc.free_space) }}</span>
                </div>
                <div class="detail-item">
                  <span class="label">总量</span>
                  <span class="value">{{ formatBytes(loc.total_space) }}</span>
                </div>
              </div>
            </div>
          </div>
        </div>
        <div v-else class="empty-text">暂无数据，点击刷新按钮扫描</div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.dashboard {
  padding: 1rem;
}
.grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
  gap: 1.5rem;
}
.card {
  background: var(--bg-surface);
  border-radius: 12px;
  padding: 1.5rem;
  box-shadow: var(--shadow-md);
  position: relative;
}
.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 1rem;
}
.card-header h3 {
  margin: 0;
}
h3 {
  margin-top: 0;
  color: var(--text-primary);
  font-size: 1.1rem;
  margin-bottom: 1rem;
}
.refresh-btn {
  background: transparent;
  border: 1px solid var(--border-color);
  color: var(--text-secondary);
  border-radius: 50%;
  width: 30px;
  height: 30px;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 1.2rem;
}
.refresh-btn:hover {
  background: var(--bg-hover);
  color: var(--text-primary);
}
.refresh-btn:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}
.overlay {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background: rgba(0,0,0,0.6);
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  border-radius: 12px;
  z-index: 10;
  color: white;
}
.spinner {
  border: 4px solid rgba(255,255,255,0.3);
  border-top: 4px solid white;
  border-radius: 50%;
  width: 30px;
  height: 30px;
  animation: spin 1s linear infinite;
  margin-bottom: 5px;
}
@keyframes spin {
  0% { transform: rotate(0deg); }
  100% { transform: rotate(360deg); }
}
.status-indicator {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  font-weight: 600;
  color: var(--text-secondary);
}
.status-indicator.running {
  color: var(--success-color);
}
.dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: currentColor;
}
.current-file {
  margin-top: 0.5rem;
  font-size: 0.9rem;
  color: var(--text-secondary);
  word-break: break-all;
}
.disk-item {
  background: var(--bg-surface-secondary);
  border-radius: 8px;
  padding: 1rem;
  margin-bottom: 0.75rem;
}
.disk-item:last-child {
  margin-bottom: 0;
}
.disk-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 0.5rem;
  font-size: 0.9rem;
}
.disk-label {
  font-weight: 600;
  color: var(--primary-color);
}
.disk-path {
  color: var(--text-secondary);
  font-size: 0.85rem;
  max-width: 200px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.disk-bar {
  height: 8px;
  background: var(--bg-surface);
  border-radius: 4px;
  overflow: hidden;
  margin-bottom: 0.5rem;
}
.disk-fill {
  height: 100%;
  background: var(--primary-color);
  border-radius: 4px;
  transition: width 0.3s ease;
}
.disk-details {
  display: flex;
  justify-content: space-between;
  font-size: 0.85rem;
  color: var(--text-secondary);
}
.detail-item {
  display: flex;
  flex-direction: column;
  text-align: center;
}
.value {
  font-weight: 600;
  color: var(--text-primary);
}
.error {
  color: var(--danger-color);
  font-size: 0.9rem;
}
.empty-text {
  color: var(--text-secondary);
  text-align: center;
  padding: 1rem;
}
</style>
