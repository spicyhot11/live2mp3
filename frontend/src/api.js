import axios from 'axios';

const apiClient = axios.create({
    baseURL: '/api', // Relative path, proxy will handle it or same domain
    headers: {
        'Content-Type': 'application/json',
    },
});

export default {
    getStatus() {
        return apiClient.get('/status');
    },
    getDashboardStats() {
        return apiClient.get('/dashboard/stats');
    },
    getConfig() {
        return apiClient.get('/config');
    },
    updateConfig(config) {
        return apiClient.post('/config', config);
    },
    triggerTask() {
        return apiClient.post('/trigger');
    },
    getDetailedStatus() {
        return apiClient.get('/status/detailed');
    },
    triggerDiskScan() {
        return apiClient.post('/dashboard/disk_scan');
    },
    listDirectories(path) {
        return apiClient.post('/files/list', { path });
    },
};
