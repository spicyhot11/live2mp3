<script setup>
import { ref, onMounted } from 'vue'
import axios from 'axios'

const history = ref([])
const loading = ref(true)

const fetchHistory = async () => {
  loading.value = true
  try {
    const res = await axios.get('/api/history')
    history.value = res.data.data
  } catch (e) {
    console.error(e)
  } finally {
    loading.value = false
  }
}

const deleteRecord = async (id) => {
  if (!confirm('确定要删除这条记录吗？')) return
  try {
    await axios.delete(`/api/history/${id}`)
    await fetchHistory()
  } catch (e) {
    alert('删除失败')
  }
}

onMounted(fetchHistory)
</script>

<template>
  <div class="history-page">
    <div class="header">
      <h2>转换历史</h2>
      <button class="refresh-btn" @click="fetchHistory">刷新</button>
    </div>

    <div class="table-container">
      <table v-if="history.length > 0">
        <thead>
          <tr>
            <th>ID</th>
            <th>文件名</th>
            <th>MD5</th>
            <th>处理时间</th>
            <th>操作</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="item in history" :key="item.id">
            <td>{{ item.id }}</td>
            <td :title="item.filepath">{{ item.filename }}</td>
            <td class="mono">{{ item.md5 }}</td>
            <td>{{ item.processed_at }}</td>
            <td>
              <button class="delete-btn" @click="deleteRecord(item.id)">删除</button>
            </td>
          </tr>
        </tbody>
      </table>
      <div v-else-if="!loading" class="empty">暂无记录</div>
      <div v-if="loading" class="loading">加载中...</div>
    </div>
  </div>
</template>

<style scoped>
.history-page {
  padding: 1rem;
}
.header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 1rem;
}
h2 {
  margin: 0;
  color: var(--text-primary);
}
.refresh-btn {
  padding: 0.5rem 1rem;
  background: var(--bg-surface);
  border: 1px solid var(--border-color);
  border-radius: 6px;
  cursor: pointer;
  color: var(--text-secondary);
}
.refresh-btn:hover {
  background: var(--bg-hover);
  color: var(--text-primary);
}
.table-container {
  background: var(--bg-surface);
  border-radius: 12px;
  box-shadow: var(--shadow-md);
  overflow-x: auto;
}
table {
  width: 100%;
  border-collapse: collapse;
}
th, td {
  padding: 1rem;
  text-align: left;
  border-bottom: 1px solid var(--border-color);
}
th {
  background: var(--bg-surface-secondary);
  font-weight: 600;
  color: var(--text-secondary);
}
.mono {
  font-family: monospace;
  color: var(--text-muted);
  font-size: 0.9em;
}
.delete-btn {
  color: var(--danger-color);
  background: none;
  border: none;
  cursor: pointer;
}
.delete-btn:hover {
  text-decoration: underline;
}
.empty, .loading {
  padding: 2rem;
  text-align: center;
  color: var(--text-muted);
}
</style>
