// vitest.config.ts — 纯函数单测 (node env, 不需 jsdom; GM 2026-05-31 #6)
import { defineConfig } from 'vitest/config';

export default defineConfig({
  test: {
    environment: 'node',
    include: ['src/**/*.test.ts'],
  },
});
