<script setup>
import ThemeToggle from '../components/ThemeToggle.vue'
</script>

<template>
  <div class="layout">
    <aside class="sidebar">
      <div class="brand">
        <h1>Live2MP3</h1>
      </div>
      <nav>
        <router-link to="/" class="nav-item">
            仪表盘
        </router-link>
        <router-link to="/files" class="nav-item">
            文件浏览
        </router-link>
        <router-link to="/history" class="nav-item">
            历史记录
        </router-link>
        <router-link to="/settings" class="nav-item">
            系统配置
        </router-link>
      </nav>
      <div class="sidebar-footer">
        <ThemeToggle />
      </div>
    </aside>
    <main class="content">
      <router-view></router-view>
    </main>
  </div>
</template>

<style scoped>
.layout {
  display: flex;
  height: 100vh;
  background: var(--bg-body);
}
.sidebar {
  width: 250px;
  background: var(--bg-surface);
  border-right: 1px solid var(--border-color);
  display: flex;
  flex-direction: column;
}
.brand {
  padding: 1.5rem;
  border-bottom: 1px solid var(--border-color);
  display: flex;
  justify-content: space-between;
  align-items: center;
}
.brand h1 {
  margin: 0;
  font-size: 1.5rem;
  background: linear-gradient(135deg, var(--primary-color) 0%, #a855f7 100%);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
}
nav {
  padding: 1rem;
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
  flex: 1;
}
.nav-item {
  padding: 0.75rem 1rem;
  text-decoration: none;
  color: var(--text-secondary);
  border-radius: 8px;
  font-weight: 500;
  transition: all 0.2s;
}
.nav-item:hover {
  background: var(--bg-surface-secondary);
  color: var(--text-primary);
}
.nav-item.router-link-active {
  background: var(--bg-surface-secondary); /* or a primary tint */
  color: var(--primary-color);
  background-color: color-mix(in srgb, var(--primary-color) 10%, transparent);
}
.sidebar-footer {
    padding: 1rem;
    border-top: 1px solid var(--border-color);
    display: flex;
    justify-content: center;
}
.content {
  flex: 1;
  overflow-y: auto;
}
</style>
