<script setup>
import { ref, onMounted } from 'vue'
import api from '../api'
import RootConfigModal from './RootConfigModal.vue'
import FileBrowserModal from './FileBrowserModal.vue'

const config = ref({
  scanner: { video_roots: [], extensions: [] },
  output: { output_root: '', keep_original: false },
  scheduler: { scan_interval_seconds: 60, merge_window_hours: 2 }
})

const loading = ref(false)
const message = ref('')

// Modal States
const showRootConfigModal = ref(false)
const showFileBrowser = ref(false)
const currentRootIndex = ref(-1)
const activeRootConfig = ref(null)
const isSelectingOutput = ref(false)

const fetchConfig = async () => {
  loading.value = true
  try {
    const res = await api.getConfig()
    config.value = res.data
    
    // Legacy support check
    if (config.value.scanner.video_roots.length > 0 && typeof config.value.scanner.video_roots[0] === 'string') {
        config.value.scanner.video_roots = config.value.scanner.video_roots.map(path => ({
            path,
            filter_mode: 'blacklist',
            rules: []
        }))
    }
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

// Logic for Roots
const addRoot = () => {
    isSelectingOutput.value = false
    showFileBrowser.value = true
}

const removeRoot = (index) => {
  config.value.scanner.video_roots.splice(index, 1)
}

const configureRoot = (index) => {
    activeRootConfig.value = config.value.scanner.video_roots[index]
    currentRootIndex.value = index
    showRootConfigModal.value = true
}

const onRootConfigSave = (newConfig) => {
    config.value.scanner.video_roots[currentRootIndex.value] = newConfig
}

// Logic for Output
const selectOutput = () => {
    isSelectingOutput.value = true
    showFileBrowser.value = true
}

const onFileSelected = (path) => {
    if (isSelectingOutput.value) {
        config.value.output.output_root = path
    } else {
        // Check for duplicates?
        const exists = config.value.scanner.video_roots.some(r => r.path === path);
        if(!exists) {
            config.value.scanner.video_roots.push({
                path: path,
                filter_mode: 'blacklist',
                rules: []
            })
        }
    }
    showFileBrowser.value = false
}

// Helpers
const addArrayItem = (arr) => { arr.push('') }
const removeArrayItem = (arr, index) => { arr.splice(index, 1) }

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
        <div class="roots-list">
             <div v-for="(root, index) in config.scanner.video_roots" :key="index" class="root-item">
                 <div class="root-info">
                     <span class="path">{{ root.path }}</span>
                     <div class="badges">
                         <span class="badge" :class="root.filter_mode">
                             {{ root.filter_mode === 'whitelist' ? '白名单' : '黑名单' }}
                         </span>
                         <span class="rule-count">{{ (root.rules || []).length }} 规则</span>
                     </div>
                 </div>
                 <div class="root-actions">
                     <button @click="configureRoot(index)">⚙️ 规则</button>
                     <button @click="removeRoot(index)" class="del-btn">删除</button>
                 </div>
             </div>
             <button @click="addRoot" class="add-btn">+ 添加路径</button>
        </div>
      </div>

      <div class="form-group">
        <label>扩展名</label>
        <div v-for="(item, index) in config.scanner.extensions" :key="index" class="array-item">
          <input v-model="config.scanner.extensions[index]" placeholder=".mp4" />
          <button @click="removeArrayItem(config.scanner.extensions, index)">X</button>
        </div>
        <button @click="addArrayItem(config.scanner.extensions)" class="btn-sm">+ 添加扩展名</button>
      </div>
      
    </div>

    <div class="section">
      <h3>输出设置</h3>
      <div class="form-group">
        <label>输出根目录</label>
        <div class="input-group">
            <input v-model="config.output.output_root" placeholder="/path/to/output" />
            <button @click="selectOutput">📂</button>
        </div>
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

    <!-- Modals -->
    <RootConfigModal 
        v-model="showRootConfigModal" 
        :config="activeRootConfig"
        @save="onRootConfigSave"
    />

    <FileBrowserModal
        v-model="showFileBrowser"
        @select="onFileSelected"
    />
  </div>
</template>

<style scoped>
.config-panel {
  padding: 20px;
  background: var(--bg-surface);
  color: var(--text-primary);
  border-radius: 8px;
  box-shadow: var(--shadow-md);
}
.section {
  margin-bottom: 20px;
  border: 1px solid var(--border-color);
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
  background: var(--bg-input);
  border: 1px solid var(--border-color);
  color: var(--text-primary);
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
  background: var(--bg-surface-secondary);
  color: var(--text-primary);
  border: 1px solid var(--border-color);
  border-radius: 4px;
}
button:hover {
  background: var(--bg-hover);
}
.save-btn {
  background: var(--primary-color);
  color: #fff;
  width: 100%;
  padding: 12px;
  font-size: 1.1em;
  border: none;
}
.save-btn:hover {
  background: var(--primary-hover);
}
.message {
  padding: 10px;
  background: #264653; /* Keep specific color, or use success? */
  background: var(--primary-color);
  color: #fff;
  margin-bottom: 10px;
  border-radius: 4px;
}

/* New Styles */
.roots-list {
    display: flex; flex-direction: column; gap: 8px;
}
.root-item {
    background: var(--bg-surface-secondary);
    padding: 10px;
    border-radius: 4px;
    display: flex;
    justify-content: space-between;
    align-items: center;
}
.root-info {
    display: flex; flex-direction: column; gap: 4px;
}
.path { font-family: monospace; }
.badges { display: flex; gap: 8px; }
.badge {
    font-size: 0.75rem; padding: 2px 6px; border-radius: 4px;
}
.badge.whitelist { background: var(--success-color); color: white; }
.badge.blacklist { background: var(--danger-color); color: white; }
.rule-count { font-size: 0.75rem; color: var(--text-muted); }
.root-actions { display: flex; gap: 5px; }
.del-btn { background: var(--danger-color); color: white; border: none; }
.del-btn:hover { background: #dc2626; }
.add-btn { width: 100%; border: 1px dashed var(--border-color); background: transparent; padding: 8px; color: var(--text-secondary); }
.add-btn:hover { background: var(--bg-hover); color: var(--text-primary); }
.input-group { display: flex; gap: 5px; }
.input-group input { flex: 1; }
.btn-sm { padding: 4px 8px; font-size: 0.9em; margin-top: 5px; }
</style>
