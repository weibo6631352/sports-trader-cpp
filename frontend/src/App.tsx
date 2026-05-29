/**
 * App.tsx — 根组件
 * owner: 小苏
 * last_review: 2026-05-29
 */

import { onMount, Show } from 'solid-js';
import { USE_STUB, initPolling } from './store';
import { GlobalBar } from './components/GlobalBar';
import { PnlSparkline } from './components/PnlSparkline';
import { EventGrid } from './components/EventGrid';
import { SecondaryFooter } from './components/SecondaryFooter';

export function App() {
  onMount(() => {
    initPolling();
  });

  return (
    <div id="app">
      <Show when={USE_STUB}>
        <div id="stub-banner">
          STUB 模式 — 本地 mock 数据 (URL 含 ?stub=1). 移除参数后连接真实 API.
        </div>
      </Show>

      <GlobalBar />
      <PnlSparkline />
      <EventGrid />
      <SecondaryFooter />
    </div>
  );
}
