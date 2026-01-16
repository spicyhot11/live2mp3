<script setup>
import { ref, onMounted } from 'vue'
import api from '../api'

const status = ref(null)
const loading = ref(false)
const message = ref('')

const fetchStatus = async () => {
  try {
    const res = await api.getStatus()
    status.value = res.data
  } catch (e) {
    message.value = '获取状态错误'
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

onMounted(() => {
  fetchStatus()
  setInterval(fetchStatus, 5000) // Poll status
})
</script>

<template>
  <div class="status-panel">
    <h2>系统仪表盘</h2>
    
    <div v-if="status" class="status-card">
      <p><strong>状态:</strong> {{ status.status }}</p>
      <p><strong>后端:</strong> {{ status.backend }} {{ status.version }}</p>
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
