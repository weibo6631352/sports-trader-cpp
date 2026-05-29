/**
 * index.tsx — Solid 挂载入口 (v7)
 * owner: 小苏
 * last_review: 2026-05-29
 *
 * v7: 包裹 SUID ThemeProvider (Material dark + quant 量化语义色)
 */

import { render } from 'solid-js/web';
import { ThemeProvider, createTheme } from '@suid/material/styles';
import CssBaseline from '@suid/material/CssBaseline';
import { App } from './App';
import './style.css';

const theme = createTheme({
  palette: {
    mode: 'dark',
    primary:   { main: '#2196f3' },
    secondary: { main: '#4caf50' },
    success:   { main: '#4caf50' },
    error:     { main: '#f44336' },
    warning:   { main: '#ff9800' },
    background: {
      default: '#121212',
      paper:   '#1e1e1e',
    },
    divider: '#373737',
    text: {
      primary:   'rgba(224,224,224,0.87)',
      secondary: 'rgba(158,158,158,1)',
      disabled:  'rgba(117,117,117,1)',
    },
  },
  typography: {
    fontFamily: '"Roboto","Helvetica","Arial",sans-serif',
    fontSize: 13,
  },
  shape: { borderRadius: 6 },
});

const root = document.getElementById('app');
if (!root) throw new Error('根节点 #app 不存在');

render(() => (
  <ThemeProvider theme={theme}>
    <CssBaseline />
    <App />
  </ThemeProvider>
), root);
