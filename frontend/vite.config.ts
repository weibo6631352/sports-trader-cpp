import { defineConfig } from 'vite';
import solidPlugin from 'vite-plugin-solid';

export default defineConfig({
  plugins: [
    solidPlugin(),
  ],
  server: {
    port: 3000,
    host: '127.0.0.1',
    // 防双开 (GM 2026-05-31): 端口被占即报错退出, 不偷偷换 3001 (省资源 + 避免多看板)。
    strictPort: true,
  },
  build: {
    target: 'esnext',
    outDir: 'dist',
  },
});
