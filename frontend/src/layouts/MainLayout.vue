<script setup>
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
        <router-link to="/history" class="nav-item">
            历史记录
        </router-link>
        <router-link to="/settings" class="nav-item">
            系统配置
        </router-link>
      </nav>
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
  background: #f3f4f6; /* Light gray bg */
}
.sidebar {
  width: 250px;
  background: white;
  border-right: 1px solid #e5e7eb;
  display: flex;
  flex-direction: column;
}
.brand {
  padding: 1.5rem;
  border-bottom: 1px solid #f3f4f6;
}
.brand h1 {
  margin: 0;
  font-size: 1.5rem;
  background: linear-gradient(135deg, #6366f1 0%, #a855f7 100%);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
}
nav {
  padding: 1rem;
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}
.nav-item {
  padding: 0.75rem 1rem;
  text-decoration: none;
  color: #4b5563;
  border-radius: 8px;
  font-weight: 500;
  transition: all 0.2s;
}
.nav-item:hover {
  background: #f9fafb;
  color: #1f2937;
}
.nav-item.router-link-active {
  background: #eef2ff;
  color: #4f46e5;
}
.content {
  flex: 1;
  overflow-y: auto;
}
</style>
