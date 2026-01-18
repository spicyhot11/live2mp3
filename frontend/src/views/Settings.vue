<script setup>
import ConfigPanel from '../components/ConfigPanel.vue'
</script>

<template>
  <div class="settings-page">
    <ConfigPanel />
  </div>
</template>

<style scoped>
.settings-page {
    /* padding handles by ConfigPanel usually, or here */
    padding: 20px;
}
</style>
