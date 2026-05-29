import { defineConfig } from 'vite';
import solidPlugin from 'vite-plugin-solid';

export default defineConfig({
  plugins: [solidPlugin()],
  // base 默认 '/' — 标准 Vite 单一路径 (dev 3000 / preview 4173), C++ 不托管前端
  server: {
    port: 3000,
    host: '127.0.0.1',
  },
  build: {
    target: 'esnext',
    outDir: 'dist',
  },
});
