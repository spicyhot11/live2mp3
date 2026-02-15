<script setup>
import { ref, watch } from 'vue'
import api from '../api'

const props = defineProps({
  modelValue: Boolean, // Visible
  initialPath: String
})

const emit = defineEmits(['update:modelValue', 'select'])

const currentPath = ref('/')
const directories = ref([])
const loading = ref(false)
const error = ref('')

const loadPath = async (path) => {
    loading.value = true
    error.value = ''
    try {
        const res = await api.listDirectories(path)
        currentPath.value = res.data.current_path
        directories.value = res.data.directories
    } catch(e) {
        error.value = e.response?.data?.error || e.message
    } finally {
        loading.value = false
    }
}

const goUp = () => {
    if (currentPath.value === '/' || currentPath.value === '') return
    loadPath(currentPath.value + '/..')
}

const selectDir = (dir) => {
    const newPath = currentPath.value === '/' ? '/' + dir : currentPath.value + '/' + dir;
    loadPath(newPath)
}

const confirmSelection = () => {
    emit('select', currentPath.value)
    close()
}

const close = () => {
    emit('update:modelValue', false)
}

watch(() => props.modelValue, (val) => {
    if (val) {
        loadPath(props.initialPath || '/')
    }
})
</script>

<template>
  <div v-if="props.modelValue" class="modal-overlay">
    <div class="modal-content">
        <h3>选择目录</h3>
        <div class="current-path">
            <button @click="goUp" :disabled="currentPath === '/' || currentPath === ''">⬆️ 上级</button>
            <input class="path-input" v-model="currentPath" @keyup.enter="loadPath(currentPath)" />
            <button @click="loadPath(currentPath)">Go</button>
        </div>
        
        <div class="file-list">
            <div v-if="loading" class="loading">加载中...</div>
            <div v-else-if="error" class="error">{{ error }}</div>
            <template v-else>
                <div 
                    v-for="dir in directories" 
                    :key="dir" 
                    class="file-item" 
                    @dblclick="selectDir(dir)"
                >
                    📁 {{ dir }}
                </div>
                <div v-if="directories.length === 0" class="empty">空目录</div>
            </template>
        </div>

        <div class="actions">
            <button @click="close" class="cancel-btn">取消</button>
            <button @click="confirmSelection" class="confirm-btn">选择当前目录</button>
        </div>
    </div>
  </div>
</template>

<style scoped>
.modal-overlay {
    position: fixed; top: 0; left: 0; right: 0; bottom: 0;
    background: rgba(0,0,0,0.5);
    display: flex; align-items: center; justify-content: center;
    z-index: 1000;
}
.modal-content {
    background: var(--bg-surface);
    color: var(--text-primary);
    width: 600px;
    height: 500px;
    display: flex;
    flex-direction: column;
    border-radius: 8px;
    padding: 1rem;
    box-shadow: 0 4px 20px rgba(0,0,0,0.5);
}
h3 { margin-top: 0; }
.current-path {
    display: flex; gap: 10px; align-items: center;
    padding: 10px;
    background: var(--bg-surface-secondary);
    border-radius: 4px;
    margin-bottom: 10px;
}
.path-input {
    flex: 1;
    background: var(--bg-input); 
    color: var(--text-primary); 
    border: 1px solid var(--border-color); 
    padding: 5px;
}
.file-list {
    flex: 1;
    overflow-y: auto;
    border: 1px solid var(--border-color);
    border-radius: 4px;
    padding: 5px;
    background: var(--bg-surface-secondary);
}
.file-item {
    padding: 8px;
    cursor: pointer;
    border-bottom: 1px solid var(--border-color);
}
.file-item:hover {
    background-color: var(--bg-hover);
}
.actions {
    margin-top: 1rem;
    display: flex; justify-content: flex-end; gap: 10px;
}
.error { color: var(--danger-color); padding: 10px; }
.empty { padding: 20px; text-align: center; color: var(--text-muted); }
button { 
    cursor: pointer; 
    padding: 6px 12px; 
    border-radius: 4px; 
    border: 1px solid var(--border-color); 
    color: var(--text-primary); 
    background: var(--bg-surface-secondary); 
}
button:disabled { opacity: 0.5; cursor: not-allowed; }
button:hover { background: var(--bg-hover); }

.confirm-btn { background: var(--primary-color); color: white; border: none; }
.confirm-btn:hover { background: var(--primary-hover); }
.cancel-btn { background: transparent; color: var(--text-secondary); border: 1px solid var(--border-color); }
.cancel-btn:hover { background: var(--bg-hover); color: var(--text-primary); }
</style>
