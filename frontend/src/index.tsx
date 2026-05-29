/**
 * index.tsx — Solid 挂载入口
 * owner: 小苏
 * last_review: 2026-05-29
 */

import { render } from 'solid-js/web';
import { App } from './App';
import './style.css';

const root = document.getElementById('app');
if (!root) throw new Error('根节点 #app 不存在');

render(() => <App />, root);
