/**
 * App.tsx — 根组件 (v8)
 * owner: 小苏  last_review: 2026-05-29
 *
 * v8 变更:
 *  - 去掉顶层 DEMO 横幅 (移入 StatusBar, data_source 恒 live)
 *  - Stub 横幅由 StatusBar 负责 (?stub=1 判断)
 *  - 四 Tab 结构保持不变
 *
 * SUID: AppBar / Toolbar / Chip / Typography / Button
 */

import { createSignal, onMount, Show, For } from 'solid-js';
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
      {/* Material AppBar 常驻状态条 (含 Stub/LIVE 横幅) */}
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
