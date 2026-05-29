/**
 * TabNav — 顶部 Tab 导航组件
 * owner: 小苏  last_review: 2026-05-29
 */

import { For } from 'solid-js';

export interface TabDef {
  key: string;
  label: string;
  badge?: string | number;
}

interface TabNavProps {
  tabs: TabDef[];
  activeTab: string;
  onChange: (key: string) => void;
}

export function TabNav(props: TabNavProps) {
  return (
    <nav class="tab-nav">
      <For each={props.tabs}>
        {(tab) => (
          <button
            class={`tab-nav-btn${props.activeTab === tab.key ? ' tab-active' : ''}`}
            onClick={() => props.onChange(tab.key)}
            type="button"
          >
            {tab.label}
            {tab.badge != null && (
              <span class="tab-nav-badge">{tab.badge}</span>
            )}
          </button>
        )}
      </For>
    </nav>
  );
}
