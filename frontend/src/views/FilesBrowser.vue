<script setup>
import { ref, onMounted, computed, watch } from 'vue'
import axios from 'axios'

const currentPath = ref('')
const parentPath = ref('')
const rootPath = ref('')
const directories = ref([])
const files = ref([])
const loading = ref(false)
const error = ref('')
const filter = ref('all') // 'all', 'processed', 'unprocessed'
const processing = ref(false)
const processMessage = ref('')

const fetchDirectory = async (path = '') => {
  loading.value = true
  error.value = ''
  try {
    const params = path ? { path } : {}
    const res = await axios.get('/api/files/browse', { params })
    
    if (res.data.error) {
      error.value = res.data.error
      return
    }
    
    currentPath.value = res.data.current_path || ''
    parentPath.value = res.data.parent_path || ''
    rootPath.value = res.data.root_path || ''
    directories.value = res.data.directories || []
    files.value = res.data.files || []
  } catch (e) {
    console.error('Failed to fetch directory', e)
    error.value = '获取目录内容失败'
  } finally {
    loading.value = false
  }
}

const navigateTo = (path) => {
  fetchDirectory(path)
}

const goUp = () => {
  if (parentPath.value) {
    fetchDirectory(parentPath.value)
  } else {
    fetchDirectory('')  // Go back to roots
  }
}

const goToRoot = () => {
  fetchDirectory('')
}

const processCurrentDirectory = async () => {
  if (!currentPath.value) {
    alert('请先进入一个目录')
    return
  }
  
  processing.value = true
  processMessage.value = ''
  try {
    const res = await axios.post('/api/files/process', { path: currentPath.value })
    processMessage.value = res.data.message || '处理已触发'
    setTimeout(() => {
      processMessage.value = ''
    }, 3000)
  } catch (e) {
    console.error('Failed to trigger processing', e)
    processMessage.value = '触发处理失败'
  } finally {
    processing.value = false
  }
}

const filteredFiles = computed(() => {
  if (filter.value === 'processed') {
    return files.value.filter(f => f.processed)
  } else if (filter.value === 'unprocessed') {
    return files.value.filter(f => !f.processed)
  }
  return files.value
})

const stats = computed(() => {
  const total = files.value.length
  const processed = files.value.filter(f => f.processed).length
  const unprocessed = total - processed
  return { total, processed, unprocessed }
})

const breadcrumbs = computed(() => {
  if (!currentPath.value) return []
  
  const parts = []
  let path = currentPath.value
  
  // Add current path crumbs
  while (path && path !== rootPath.value) {
    const name = path.split('/').pop()
    parts.unshift({ name, path })
    const parentIdx = path.lastIndexOf('/')
    path = parentIdx > 0 ? path.substring(0, parentIdx) : ''
  }
  
  // Add root
  if (rootPath.value) {
    parts.unshift({ 
      name: rootPath.value.split('/').pop() || rootPath.value, 
      path: rootPath.value 
    })
  }
  
  return parts
})

const isAtRoot = computed(() => !currentPath.value)

const formatBytes = (bytes, decimals = 2) => {
  if (!+bytes) return '0 B'
  const k = 1024
  const dm = decimals < 0 ? 0 : decimals
  const sizes = ['B', 'KB', 'MB', 'GB', 'TB']
  const i = Math.floor(Math.log(bytes) / Math.log(k))
  return `${parseFloat((bytes / Math.pow(k, i)).toFixed(dm))} ${sizes[i]}`
}

onMounted(() => {
  fetchDirectory()
})
</script>

<template>
  <div class="files-browser">
    <div class="header">
      <h2>文件浏览</h2>
      <div class="actions">
        <button 
          v-if="!isAtRoot" 
          @click="processCurrentDirectory" 
          :disabled="processing || loading" 
          class="process-btn"
        >
          {{ processing ? '处理中...' : '⚡ 立即处理' }}
        </button>
        <button @click="fetchDirectory(currentPath)" :disabled="loading" class="refresh-btn">
          {{ loading ? '加载中...' : '刷新' }}
        </button>
      </div>
    </div>

    <!-- Breadcrumb Navigation -->
    <div class="breadcrumb">
      <span class="crumb home" @click="goToRoot">🏠 根目录</span>
      <template v-if="breadcrumbs.length > 0">
        <span class="separator">/</span>
        <template v-for="(crumb, idx) in breadcrumbs" :key="crumb.path">
          <span 
            class="crumb" 
            :class="{ current: idx === breadcrumbs.length - 1 }"
            @click="idx < breadcrumbs.length - 1 ? navigateTo(crumb.path) : null"
          >
            {{ crumb.name }}
          </span>
          <span v-if="idx < breadcrumbs.length - 1" class="separator">/</span>
        </template>
      </template>
    </div>

    <!-- Back Button -->
    <div v-if="!isAtRoot" class="back-bar">
      <button @click="goUp" class="back-btn">
        ← 返回上级
      </button>
    </div>

    <!-- Stats Bar (only show when there are files) -->
    <div v-if="files.length > 0" class="stats-bar">
      <div class="stat-item">
        <span class="stat-label">文件总数</span>
        <span class="stat-value">{{ stats.total }}</span>
      </div>
      <div class="stat-item processed">
        <span class="stat-label">已处理</span>
        <span class="stat-value">{{ stats.processed }}</span>
      </div>
      <div class="stat-item unprocessed">
        <span class="stat-label">未处理</span>
        <span class="stat-value">{{ stats.unprocessed }}</span>
      </div>
    </div>

    <!-- Filter (only show when there are files) -->
    <div v-if="files.length > 0" class="filter-bar">
      <label>筛选:</label>
      <select v-model="filter">
        <option value="all">全部</option>
        <option value="processed">已处理</option>
        <option value="unprocessed">未处理</option>
      </select>
    </div>

    <!-- Process Message -->
    <div v-if="processMessage" class="process-message">{{ processMessage }}</div>

    <!-- Error -->
    <div v-if="error" class="error">{{ error }}</div>

    <!-- Loading -->
    <div v-if="loading" class="loading">
      <div class="spinner"></div>
      <span>正在加载...</span>
    </div>

    <!-- Content -->
    <div v-else class="content">
      <!-- Directories -->
      <div v-if="directories.length > 0" class="section">
        <h3 v-if="!isAtRoot">📁 子目录 ({{ directories.length }})</h3>
        <h3 v-else>📁 根目录</h3>
        <div class="dir-list">
          <div 
            v-for="dir in directories" 
            :key="dir.path" 
            class="dir-item"
            @click="navigateTo(dir.path)"
          >
            <span class="dir-icon">📂</span>
            <span class="dir-name">{{ dir.name }}</span>
            <span class="dir-arrow">→</span>
          </div>
        </div>
      </div>

      <!-- Files -->
      <div v-if="filteredFiles.length > 0" class="section">
        <h3>📄 文件 ({{ filteredFiles.length }})</h3>
        <div class="file-list">
          <div v-for="file in filteredFiles" :key="file.filepath" class="file-item">
            <div class="file-status" :class="{ processed: file.processed }">
              <span class="status-dot"></span>
            </div>
            <div class="file-info">
              <div class="file-name" :title="file.filepath">{{ file.filename }}</div>
              <div class="file-meta">
                <span class="file-size">{{ formatBytes(file.size) }}</span>
                <span class="file-date">{{ file.modified_at }}</span>
              </div>
            </div>
            <div class="file-badge" :class="{ processed: file.processed }">
              {{ file.processed ? '已处理' : '未处理' }}
            </div>
          </div>
        </div>
      </div>

      <!-- Empty State -->
      <div v-if="directories.length === 0 && files.length === 0 && !loading && !error" class="empty">
        <p>此目录为空</p>
      </div>
    </div>
  </div>
</template>

<style scoped>
.files-browser {
  padding: 1rem;
}

.header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 1rem;
}

.header h2 {
  margin: 0;
  color: var(--text-primary);
}

.refresh-btn {
  background: var(--primary-color);
  color: white;
  border: none;
  padding: 0.5rem 1rem;
  border-radius: 6px;
  cursor: pointer;
  font-weight: 500;
  transition: opacity 0.2s;
}

.refresh-btn:hover {
  opacity: 0.9;
}

.refresh-btn:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.process-btn {
  background: linear-gradient(135deg, #f59e0b 0%, #d97706 100%);
  color: white;
  border: none;
  padding: 0.5rem 1rem;
  border-radius: 6px;
  cursor: pointer;
  font-weight: 500;
  transition: all 0.2s;
  margin-right: 0.5rem;
}

.process-btn:hover {
  transform: scale(1.02);
  box-shadow: 0 4px 12px rgba(245, 158, 11, 0.3);
}

.process-btn:disabled {
  opacity: 0.5;
  cursor: not-allowed;
  transform: none;
  box-shadow: none;
}

.process-message {
  background: rgba(34, 197, 94, 0.1);
  color: var(--success-color);
  padding: 0.75rem 1rem;
  border-radius: 8px;
  margin-bottom: 1rem;
  text-align: center;
  animation: fadeIn 0.3s ease;
}

@keyframes fadeIn {
  from { opacity: 0; transform: translateY(-10px); }
  to { opacity: 1; transform: translateY(0); }
}

.breadcrumb {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 0.25rem;
  padding: 0.75rem 1rem;
  background: var(--bg-surface);
  border-radius: 8px;
  margin-bottom: 1rem;
  font-size: 0.9rem;
}

.crumb {
  color: var(--primary-color);
  cursor: pointer;
  padding: 0.25rem 0.5rem;
  border-radius: 4px;
  transition: background 0.2s;
}

.crumb:hover:not(.current) {
  background: var(--bg-surface-secondary);
}

.crumb.current {
  color: var(--text-primary);
  font-weight: 600;
  cursor: default;
}

.crumb.home {
  font-weight: 500;
}

.separator {
  color: var(--text-secondary);
}

.back-bar {
  margin-bottom: 1rem;
}

.back-btn {
  background: var(--bg-surface);
  color: var(--text-secondary);
  border: 1px solid var(--border-color);
  padding: 0.5rem 1rem;
  border-radius: 6px;
  cursor: pointer;
  font-weight: 500;
  transition: all 0.2s;
}

.back-btn:hover {
  background: var(--bg-surface-secondary);
  color: var(--text-primary);
}

.stats-bar {
  display: flex;
  gap: 1rem;
  margin-bottom: 1rem;
}

.stat-item {
  background: var(--bg-surface);
  padding: 0.5rem 1rem;
  border-radius: 8px;
  display: flex;
  flex-direction: column;
  align-items: center;
  min-width: 80px;
  box-shadow: var(--shadow-sm);
}

.stat-label {
  font-size: 0.75rem;
  color: var(--text-secondary);
}

.stat-value {
  font-size: 1.25rem;
  font-weight: 600;
  color: var(--text-primary);
}

.stat-item.processed .stat-value {
  color: var(--success-color);
}

.stat-item.unprocessed .stat-value {
  color: var(--text-secondary);
}

.filter-bar {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  margin-bottom: 1rem;
  color: var(--text-secondary);
}

.filter-bar select {
  padding: 0.5rem 1rem;
  border-radius: 6px;
  border: 1px solid var(--border-color);
  background: var(--bg-surface);
  color: var(--text-primary);
  cursor: pointer;
}

.error {
  background: rgba(239, 68, 68, 0.1);
  color: var(--danger-color);
  padding: 1rem;
  border-radius: 8px;
  margin-bottom: 1rem;
}

.loading {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 3rem;
  color: var(--text-secondary);
}

.spinner {
  border: 4px solid var(--border-color);
  border-top: 4px solid var(--primary-color);
  border-radius: 50%;
  width: 40px;
  height: 40px;
  animation: spin 1s linear infinite;
  margin-bottom: 1rem;
}

@keyframes spin {
  0% { transform: rotate(0deg); }
  100% { transform: rotate(360deg); }
}

.section {
  margin-bottom: 1.5rem;
}

.section h3 {
  margin: 0 0 0.75rem 0;
  color: var(--text-primary);
  font-size: 1rem;
}

.dir-list {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.dir-item {
  display: flex;
  align-items: center;
  padding: 0.75rem 1rem;
  background: var(--bg-surface);
  border-radius: 8px;
  cursor: pointer;
  transition: all 0.2s;
  box-shadow: var(--shadow-sm);
}

.dir-item:hover {
  background: var(--bg-surface-secondary);
  transform: translateX(4px);
}

.dir-icon {
  font-size: 1.25rem;
  margin-right: 0.75rem;
}

.dir-name {
  flex: 1;
  font-weight: 500;
  color: var(--text-primary);
}

.dir-arrow {
  color: var(--text-secondary);
  font-size: 1.25rem;
}

.file-list {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.file-item {
  display: flex;
  align-items: center;
  padding: 0.75rem 1rem;
  background: var(--bg-surface);
  border-radius: 8px;
  box-shadow: var(--shadow-sm);
}

.file-status {
  width: 10px;
  height: 10px;
  margin-right: 1rem;
  flex-shrink: 0;
}

.status-dot {
  display: block;
  width: 10px;
  height: 10px;
  border-radius: 50%;
  background: var(--text-secondary);
}

.file-status.processed .status-dot {
  background: var(--success-color);
}

.file-info {
  flex: 1;
  min-width: 0;
}

.file-name {
  font-weight: 500;
  color: var(--text-primary);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.file-meta {
  display: flex;
  gap: 1rem;
  font-size: 0.85rem;
  color: var(--text-secondary);
  margin-top: 0.25rem;
}

.file-badge {
  padding: 0.25rem 0.75rem;
  border-radius: 20px;
  font-size: 0.75rem;
  font-weight: 500;
  background: var(--bg-surface-secondary);
  color: var(--text-secondary);
  margin-left: 1rem;
  flex-shrink: 0;
}

.file-badge.processed {
  background: rgba(34, 197, 94, 0.15);
  color: var(--success-color);
}

.empty {
  text-align: center;
  padding: 3rem;
  color: var(--text-secondary);
  background: var(--bg-surface);
  border-radius: 8px;
}
</style>
