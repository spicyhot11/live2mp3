import { createRouter, createWebHashHistory } from 'vue-router'
import Dashboard from '../views/Dashboard.vue'
import Settings from '../views/Settings.vue'
import History from '../views/History.vue'
import FilesBrowser from '../views/FilesBrowser.vue'

const routes = [
    { path: '/', component: Dashboard },
    { path: '/files', component: FilesBrowser },
    { path: '/history', component: History },
    { path: '/settings', component: Settings },
]

const router = createRouter({
    history: createWebHashHistory(),
    routes,
})

export default router
