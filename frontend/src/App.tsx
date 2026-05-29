/**
 * App.tsx — 根组件 (v6)
 * owner: 小苏  last_review: 2026-05-29
 *
 * v6 架构:
 *   StatusBar (常驻, 跨页不变)
 *   TabNav    (4 tabs: 盯盘 / Ops / PnL分析 / 市场详情)
 *   页面内容区 (按 activeTab 切换, store 保持不重置)
 *
 * 路由: URL hash (#trading / #ops / #analytics / #market)
 *   tab 切换同步 hash, 刷新后还原
 */

import { createSignal, onMount, Show } from 'solid-js';
import { USE_STUB, initPolling } from './store';
import { StatusBar } from './components/StatusBar';
import { TabNav } from './components/ui/TabNav';
import { TradingPage } from './components/TradingPage';
import { OpsPage } from './components/OpsPage';
import { AnalyticsPage } from './components/AnalyticsPage';
import type { TabDef } from './components/ui/TabNav';

const TABS: TabDef[] = [
  { key: 'trading',   label: '盯盘 Trading' },
  { key: 'ops',       label: 'Ops 观测' },
  { key: 'analytics', label: 'PnL 分析' },
  { key: 'market',    label: '市场详情' },
];

function readHashTab(): string {
  const hash = location.hash.replace('#', '');
  return TABS.some((t) => t.key === hash) ? hash : 'trading';
}

export function App() {
  const [activeTab, setActiveTab] = createSignal<string>(readHashTab());

  function switchTab(key: string) {
    setActiveTab(key);
    location.hash = key;
  }

  onMount(() => {
    initPolling();
    // 监听浏览器前进/后退
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

      {/* 跨页常驻状态条 */}
      <StatusBar />

      {/* Tab 导航 */}
      <TabNav tabs={TABS} activeTab={activeTab()} onChange={switchTab} />

      {/* 页面内容区 (Show 切换视图层, store 不重置) */}
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
        <div class="page-content">
          <div class="no-data" style={{ padding: '60px', 'font-size': '14px' }}>
            市场详情页 (P2 后续实现)
            <div style={{ 'font-size': '11px', 'margin-top': '8px' }}>
              依赖 ADR-038 §4.2/4.3 完整 trace 端点。入口: 盯盘页盘口标题点击 → 跳转此页。
            </div>
          </div>
        </div>
      </Show>
    </div>
  );
}
