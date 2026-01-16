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
    getConfig() {
        return apiClient.get('/config');
    },
    updateConfig(config) {
        return apiClient.post('/config', config);
    },
    triggerTask() {
        return apiClient.post('/trigger');
    },
};
