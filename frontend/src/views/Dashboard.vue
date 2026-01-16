<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import axios from 'axios'

const stats = ref({
  status: { running: false, current_file: '' },
  disk: { capacity: 0, free: 0, available: 0, error: '' }
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
        <h3>磁盘空间</h3>
        <div v-if="stats.disk.error" class="error">{{ stats.disk.error }}</div>
        <div v-else class="disk-content">
          <div class="disk-bar">
            <div class="disk-fill" :style="{ width: (1 - stats.disk.available / stats.disk.capacity) * 100 + '%' }"></div>
          </div>
          <div class="disk-details">
            <div class="detail-item">
              <span class="label">可用</span>
              <span class="value">{{ formatBytes(stats.disk.available) }}</span>
            </div>
            <div class="detail-item">
              <span class="label">总量</span>
              <span class="value">{{ formatBytes(stats.disk.capacity) }}</span>
            </div>
          </div>
        </div>
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
  background: white;
  border-radius: 12px;
  padding: 1.5rem;
  box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.1);
}
h3 {
  margin-top: 0;
  color: #1f2937;
  font-size: 1.1rem;
  margin-bottom: 1rem;
}
.status-indicator {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  font-weight: 600;
  color: #6b7280;
}
.status-indicator.running {
  color: #10b981;
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
  color: #4b5563;
  word-break: break-all;
}
.disk-bar {
  height: 8px;
  background: #e5e7eb;
  border-radius: 4px;
  overflow: hidden;
  margin-bottom: 0.5rem;
}
.disk-fill {
  height: 100%;
  background: #3b82f6;
  border-radius: 4px;
  transition: width 0.3s ease;
}
.disk-details {
  display: flex;
  justify-content: space-between;
  font-size: 0.9rem;
  color: #6b7280;
}
.detail-item {
  display: flex;
  flex-direction: column;
}
.value {
  font-weight: 600;
  color: #1f2937;
}
</style>
