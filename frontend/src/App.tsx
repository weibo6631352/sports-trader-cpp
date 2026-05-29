/**
 * App.tsx — 根组件 (v7)
 * owner: 小苏  last_review: 2026-05-29
 *
 * v7 架构:
 *   Material AppBar (常驻, 跨页不变) — 系统状态 + WSS + PnL + Gate
 *   DEMO/STUB 横幅 (Alert)
 *   Tab 导航栏 (AppBar 风格 underline tabs)
 *   页面内容区 (按 activeTab 切换, store 保持不重置)
 *
 * SUID 组件: AppBar / Toolbar / Alert / Chip / Typography / Button
 */

import { createSignal, onMount, Show, For } from 'solid-js';
import AppBar from '@suid/material/AppBar';
import Toolbar from '@suid/material/Toolbar';
import Typography from '@suid/material/Typography';
import Alert from '@suid/material/Alert';
import Chip from '@suid/material/Chip';
import { USE_STUB, initPolling } from './store';
import { StatusBar } from './components/StatusBar';
import { TradingPage } from './components/TradingPage';
import { OpsPage } from './components/OpsPage';
import { AnalyticsPage } from './components/AnalyticsPage';
import { MarketDetailPage } from './components/MarketDetailPage';

const TABS = [
  { key: 'trading',   label: '盯盘 Trading' },
  { key: 'ops',       label: 'Ops 观测' },
  { key: 'analytics', label: 'PnL 分析' },
  { key: 'market',    label: '市场详情' },
] as const;

type TabKey = typeof TABS[number]['key'];

function readHashTab(): TabKey {
  const hash = location.hash.replace('#', '') as TabKey;
  return TABS.some((t) => t.key === hash) ? hash : 'trading';
}

export function App() {
  const [activeTab, setActiveTab] = createSignal<TabKey>(readHashTab());

  function switchTab(key: TabKey) {
    setActiveTab(key);
    location.hash = key;
  }

  onMount(() => {
    initPolling();
    window.addEventListener('hashchange', () => {
      setActiveTab(readHashTab());
    });
  });

  return (
    <div id="app">
      {/* Stub 横幅 */}
      <Show when={USE_STUB}>
        <div id="stub-banner">
          STUB 模式 — 本地 mock 数据 (URL 含 ?stub=1). 移除参数后连接真实 API.
        </div>
      </Show>

      {/* Material AppBar 常驻状态条 */}
      <StatusBar />

      {/* Tab 导航栏 (Material 下划线风格) */}
      <div class="quant-tabs-bar">
        <For each={TABS}>
          {(tab) => (
            <button
              class={`quant-tab-btn${activeTab() === tab.key ? ' tab-active' : ''}`}
              onClick={() => switchTab(tab.key)}
              type="button"
            >
              {tab.label}
            </button>
          )}
        </For>
      </div>

      {/* 页面内容区 */}
      <Show when={activeTab() === 'trading'}>
        <TradingPage />
      </Show>
      <Show when={activeTab() === 'ops'}>
        <OpsPage />
      </Show>
      <Show when={activeTab() === 'analytics'}>
        <AnalyticsPage />
      </Show>
      <Show when={activeTab() === 'market'}>
        <MarketDetailPage />
      </Show>
    </div>
  );
}
