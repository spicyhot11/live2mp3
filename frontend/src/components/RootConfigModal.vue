<script setup>
import { ref, watch, reactive } from 'vue'
import FileBrowserModal from './FileBrowserModal.vue'

const props = defineProps({
  modelValue: Boolean,
  config: Object // VideoRootConfig object
})

const emit = defineEmits(['update:modelValue', 'save'])

const localConfig = reactive({
    path: '',
    filter_mode: 'blacklist',
    rules: [],
    enable_delete: false,
    delete_mode: 'blacklist',
    delete_rules: []
})

const showDirPickerForRule = ref(false)
const currentRuleIndex = ref(-1)

watch(() => props.modelValue, (val) => {
    if (val && props.config) {
        localConfig.path = props.config.path
        localConfig.filter_mode = props.config.filter_mode
        localConfig.rules = JSON.parse(JSON.stringify(props.config.rules || []))
        
        localConfig.enable_delete = props.config.enable_delete || false
        localConfig.delete_mode = props.config.delete_mode || 'blacklist'
        localConfig.delete_rules = JSON.parse(JSON.stringify(props.config.delete_rules || []))
    }
})

// Rule management type: 'filter' or 'delete'
// We need to know which list we are adding/removing/picking for.
const currentTab = ref('filter') // 'filter' | 'delete'

const addRule = () => {
    const list = currentTab.value === 'filter' ? localConfig.rules : localConfig.delete_rules;
    list.push({
        pattern: '',
        type: 'exact'
    })
}

const removeRule = (index) => {
    const list = currentTab.value === 'filter' ? localConfig.rules : localConfig.delete_rules;
    list.splice(index, 1)
}

const openDirPickerForRule = (index) => {
    currentRuleIndex.value = index
    showDirPickerForRule.value = true
}

const onDirSelected = (path) => {
    // Try to calculate relative path from root
    let pattern = '';
    const rootPath = localConfig.path.endsWith('/') ? localConfig.path : localConfig.path + '/';
    
    if (path.startsWith(rootPath)) {
         let rel = path.substring(rootPath.length);
         if (rel.endsWith('/')) rel = rel.slice(0, -1);
         // Use first component
         const parts = rel.split('/');
         if (parts.length > 0) pattern = parts[0];
         else pattern = rel;
    } else {
         // Fallback to directory name
         const parts = path.split('/');
         pattern = parts[parts.length-1];
         if(!pattern && parts.length > 1) pattern = parts[parts.length-2]; // handle trailing slash
    }
    
    if (pattern) {
        const list = currentTab.value === 'filter' ? localConfig.rules : localConfig.delete_rules;
        if(list[currentRuleIndex.value]) list[currentRuleIndex.value].pattern = pattern;
    }
    showDirPickerForRule.value = false; 
}

const save = () => {
    emit('save', JSON.parse(JSON.stringify(localConfig)))
    emit('update:modelValue', false)
}
</script>

<template>
<div v-if="modelValue" class="modal-overlay">
    <div class="modal-content">
        <h3>配置子目录规则</h3>
        <div class="info">根目录: <code>{{ localConfig.path }}</code></div>
        
        <div class="tabs">
            <button :class="{active: currentTab==='filter'}" @click="currentTab='filter'">转换过滤</button>
            <button :class="{active: currentTab==='delete'}" @click="currentTab='delete'">源文件清理</button>
        </div>
        
        <div v-if="currentTab === 'filter'" class="tab-content">
             <div class="form-group">
                 <label>模式:</label>
                 <div class="radios">
                     <label><input type="radio" v-model="localConfig.filter_mode" value="whitelist"> 白名单 (仅处理允许的)</label>
                     <label><input type="radio" v-model="localConfig.filter_mode" value="blacklist"> 黑名单 (跳过指定的)</label>
                 </div>
             </div>
    
             <div class="rules-section">
                <h4>规则列表 ({{localConfig.filter_mode === 'whitelist' ? '仅处理' : '跳过'}})</h4>
                <div class="rule-list">
                    <div v-for="(rule, index) in localConfig.rules" :key="index" class="rule-item">
                        <select v-model="rule.type">
                            <option value="exact">精确 (目录名)</option>
                            <option value="glob">模糊 (Glob)</option>
                            <option value="regex">正则 (Regex)</option>
                        </select>
                        <input v-model="rule.pattern" placeholder="规则内容" />
                        <button v-if="rule.type !== 'regex'" @click="openDirPickerForRule(index)" title="选择目录">📂</button>
                        <button @click="removeRule(index)" class="del-btn">X</button>
                    </div>
                </div>
                <button @click="addRule" class="add-btn">+ 添加规则</button>
            </div>
        </div>
        
        <div v-if="currentTab === 'delete'" class="tab-content">
             <div class="form-group">
                 <label class="toggle-label">
                     <input type="checkbox" v-model="localConfig.enable_delete"> 启用源文件删除
                 </label>
                 <p class="hint">开启后，符合条件的源文件将在合并完成后被删除。请谨慎操作！</p>
             </div>
             
             <div v-if="localConfig.enable_delete">
                 <div class="form-group">
                     <label>模式:</label>
                     <div class="radios">
                         <label><input type="radio" v-model="localConfig.delete_mode" value="whitelist"> 白名单 (仅删除允许的)</label>
                         <label><input type="radio" v-model="localConfig.delete_mode" value="blacklist"> 黑名单 (保留指定的，删除其他)</label>
                     </div>
                 </div>
        
                 <div class="rules-section">
                    <h4>清理规则列表</h4>
                    <div class="rule-list">
                        <div v-for="(rule, index) in localConfig.delete_rules" :key="index" class="rule-item">
                            <select v-model="rule.type">
                                <option value="exact">精确 (目录名)</option>
                                <option value="glob">模糊 (Glob)</option>
                                <option value="regex">正则 (Regex)</option>
                            </select>
                            <input v-model="rule.pattern" placeholder="规则内容" />
                            <button v-if="rule.type !== 'regex'" @click="openDirPickerForRule(index)" title="选择目录">📂</button>
                            <button @click="removeRule(index)" class="del-btn">X</button>
                        </div>
                    </div>
                    <button @click="addRule" class="add-btn">+ 添加规则</button>
                </div>
             </div>
        </div>

        <div class="actions">
            <button @click="$emit('update:modelValue', false)" class="cancel-btn">取消</button>
            <button @click="save" class="confirm-btn">保存</button>
        </div>
    </div>

    <!-- File Browser for Rule -->
    <FileBrowserModal 
        v-model="showDirPickerForRule" 
        :initial-path="localConfig.path"
        @select="onDirSelected"
    />
</div>
</template>

<style scoped>
.modal-overlay {
    position: fixed; top: 0; left: 0; right: 0; bottom: 0;
    background: rgba(0,0,0,0.5);
    display: flex; align-items: center; justify-content: center;
    z-index: 900;
}
.modal-content {
    background: var(--bg-surface);
    color: var(--text-primary);
    width: 650px;
    max-height: 80vh;
    display: flex;
    flex-direction: column;
    border-radius: 8px;
    padding: 1.5rem;
    box-shadow: 0 4px 20px rgba(0,0,0,0.5);
}
h3 { margin-top: 0; border-bottom: 1px solid var(--border-color); padding-bottom: 10px; }
.info { margin-bottom: 1rem; color: var(--text-secondary); }
.tabs { display: flex; gap: 10px; margin-bottom: 15px; border-bottom: 1px solid var(--border-color); padding-bottom: 5px; }
.tabs button {
    background: transparent; border: none; color: var(--text-secondary); padding: 8px 15px; cursor: pointer;
    border-bottom: 2px solid transparent; border-radius: 0;
}
.tabs button.active { color: var(--primary-color); border-bottom-color: var(--primary-color); }
.tab-content { display: flex; flex-direction: column; flex: 1; overflow: hidden; }

.form-group { margin-bottom: 1.5rem; }
.radios { display: flex; gap: 20px; margin-top: 5px; }
.toggle-label { display: flex; align-items: center; gap: 10px; font-weight: bold; cursor: pointer; }
.hint { font-size: 0.9em; color: var(--warning-color); margin-top: 5px; }
.rules-section {
    flex: 1;
    overflow-y: auto;
    border: 1px solid var(--border-color);
    padding: 10px;
    border-radius: 4px;
    background: var(--bg-surface-secondary);
}
.rule-item {
    display: flex; gap: 10px; margin-bottom: 8px;
}
.rule-item select, .rule-item input {
    background: var(--bg-input); 
    color: var(--text-primary); 
    border: 1px solid var(--border-color);
    padding: 5px;
    border-radius: 4px;
}
.rule-item input { flex: 1; }
.actions {
    margin-top: 1.5rem;
    display: flex; justify-content: flex-end; gap: 10px;
}
button { 
    cursor: pointer; 
    padding: 6px 12px; 
    border-radius: 4px; 
    border: 1px solid var(--border-color); 
    color: var(--text-primary); 
    background: var(--bg-surface-secondary); 
}
button:hover {
    background: var(--bg-hover);
}
.add-btn { width: 100%; margin-top: 5px; background: transparent; border: 1px dashed var(--border-color); color: var(--text-secondary); }
.add-btn:hover { background: var(--bg-hover); color: var(--text-primary); }
.del-btn { background: var(--danger-color); color: white; border: none; }
.del-btn:hover { background: #dc2626; }
.confirm-btn { background: var(--primary-color); color: white; border: none; }
.confirm-btn:hover { background: var(--primary-hover); }
.cancel-btn { background: transparent; color: var(--text-secondary); border: 1px solid var(--border-color); }
.cancel-btn:hover { background: var(--bg-hover); color: var(--text-primary); }
</style>
