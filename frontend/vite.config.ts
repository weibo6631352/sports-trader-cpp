import { defineConfig } from 'vite';
import solidPlugin from 'vite-plugin-solid';

export default defineConfig({
  plugins: [solidPlugin()],
  // base: './' — 产物资源引用使用相对路径, 适配 C++ 在任意路径托管 dist/
  // vite dev server 忽略此项, 不影响开发体验
  base: './',
  server: {
    port: 3000,
    host: '127.0.0.1',
  },
  build: {
    target: 'esnext',
    outDir: 'dist',
  },
});
