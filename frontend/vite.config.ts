import { defineConfig } from 'vite';
import solidPlugin from 'vite-plugin-solid';
import tailwindcss from '@tailwindcss/vite';

export default defineConfig({
  plugins: [
    tailwindcss(),
    solidPlugin(),
  ],
  server: {
    port: 3000,
    host: '127.0.0.1',
  },
  build: {
    target: 'esnext',
    outDir: 'dist',
  },
});
