<script setup>
import { ref, onMounted } from 'vue'
import axios from 'axios'

const config = ref(null)
const loading = ref(true)
const saving = ref(false)

// Temp vars for text areas
const simpleAllowText = ref('')
const simpleDenyText = ref('')

const fetchConfig = async () => {
  loading.value = true
  try {
    const res = await axios.get('/api/config')
    config.value = res.data
    
    // Convert arrays to semicolon separated strings
    if (config.value.scanner.simple_allow_list) {
        simpleAllowText.value = config.value.scanner.simple_allow_list.join(';')
    }
    if (config.value.scanner.simple_deny_list) {
        simpleDenyText.value = config.value.scanner.simple_deny_list.join(';')
    }
  } catch (e) {
    console.error(e)
  } finally {
    loading.value = false
  }
}

const saveConfig = async () => {
  saving.value = true
  try {
    // Parse strings back to arrays
    config.value.scanner.simple_allow_list = simpleAllowText.value.split(';').map(s => s.trim()).filter(s => s)
    config.value.scanner.simple_deny_list = simpleDenyText.value.split(';').map(s => s.trim()).filter(s => s)

    await axios.post('/api/config', config.value)
    alert('保存成功')
  } catch (e) {
    alert('保存失败')
  } finally {
    saving.value = false
  }
}

onMounted(fetchConfig)
</script>

<template>
  <div class="settings-page">
    <div class="header">
        <h2>系统设置</h2>
        <button class="save-btn" @click="saveConfig" :disabled="saving">
            {{ saving ? '保存中...' : '保存配置' }}
        </button>
    </div>

    <div v-if="loading" class="loading">加载中...</div>
    <div v-else class="form-container">
        <!-- Filtering Section -->
        <div class="section">
            <h3>文件过滤</h3>
            <div class="form-group">
                <label>白名单 (包含关键词)</label>
                <div class="hint">使用分号(;)分隔多个关键词。如果设置，仅处理匹配文件。</div>
                <textarea v-model="simpleAllowText" rows="3" placeholder="例如: .live;important"></textarea>
            </div>
            <div class="form-group">
                <label>黑名单 (包含关键词)</label>
                <div class="hint">使用分号(;)分隔多个关键词。匹配的文件将被跳过。</div>
                <textarea v-model="simpleDenyText" rows="3" placeholder="例如: temp;backup"></textarea>
            </div>
            <!-- Regex lists could go here too, but prioritized simple lists as requested -->
        </div>

        <!-- Output Section -->
        <div class="section">
            <h3>输出设置</h3>
            <div class="form-group">
                <label>输出目录</label>
                <input v-model="config.output.output_root" type="text">
            </div>
            <div class="form-group checkbox">
                <input type="checkbox" id="keep" v-model="config.output.keep_original">
                <label for="keep">保留原始文件</label>
            </div>
        </div>
        
         <!-- Scheduler Section -->
        <div class="section">
            <h3>调度设置</h3>
            <div class="form-group">
                <label>扫描间隔 (秒)</label>
                <input v-model.number="config.scheduler.scan_interval_seconds" type="number">
            </div>
        </div>
    </div>
  </div>
</template>

<style scoped>
.settings-page {
  padding: 1rem;
}
.header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 2rem;
}
h2 { margin: 0; color: #1f2937; }
.save-btn {
    background: #4f46e5;
    color: white;
    padding: 0.5rem 1.5rem;
    border: none;
    border-radius: 6px;
    font-weight: 600;
    cursor: pointer;
}
.save-btn:disabled { opacity: 0.7; }

.form-container {
    background: white;
    border-radius: 12px;
    padding: 2rem;
    box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.1);
    max-width: 800px;
}
.section {
    margin-bottom: 2rem;
    border-bottom: 1px solid #f3f4f6;
    padding-bottom: 1rem;
}
.section:last-child { border-bottom: none; }
h3 { margin-bottom: 1rem; color: #374151; }
.form-group { margin-bottom: 1rem; }
label { display: block; margin-bottom: 0.5rem; color: #4b5563; font-weight: 500; }
.hint { font-size: 0.85rem; color: #9ca3af; margin-bottom: 0.5rem; }
input[type="text"], input[type="number"], textarea {
    width: 100%;
    padding: 0.5rem;
    border: 1px solid #d1d5db;
    border-radius: 6px;
    font-size: 1rem;
}
.checkbox { display: flex; align-items: center; gap: 0.5rem; }
.checkbox label { margin-bottom: 0; }
.checkbox input { width: auto; }
</style>
