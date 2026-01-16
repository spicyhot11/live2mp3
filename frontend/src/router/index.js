import { createRouter, createWebHashHistory } from 'vue-router'
import Dashboard from '../views/Dashboard.vue'
import Settings from '../views/Settings.vue'
import History from '../views/History.vue'

const routes = [
    { path: '/', component: Dashboard },
    { path: '/settings', component: Settings },
    { path: '/history', component: History },
]

const router = createRouter({
    history: createWebHashHistory(),
    routes,
})

export default router
