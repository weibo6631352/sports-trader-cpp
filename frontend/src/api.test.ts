// src/api.test.ts — 纯格式/解析函数单测 (GM 2026-05-31, 接口核查 #6: 前端原零自动化测试)
import { describe, it, expect } from 'vitest';
import { fmtClock, fmtUsdc, fmtBps, fmtPct, fmtUptime, stalenessMs, modeBadge } from './api';

describe('fmtClock', () => {
  it('mm:ss 格式', () => { expect(fmtClock(125)).toBe('2:05'); });
  it('整分', () => { expect(fmtClock(60)).toBe('1:00'); });
  it('负/null/undefined → —', () => { expect(fmtClock(-1)).toBe('—'); expect(fmtClock(null)).toBe('—'); expect(fmtClock(undefined)).toBe('—'); });
  it('0 → 0:00 (调用方决定是否显示)', () => { expect(fmtClock(0)).toBe('0:00'); });
});

describe('fmtUsdc', () => {
  it('正数带 $', () => { expect(fmtUsdc(1.5)).toContain('1.5'); });
  it('null/undefined → 占位', () => { expect(fmtUsdc(null)).toBe('—'); expect(fmtUsdc(undefined)).toBe('—'); });
  it('0 → +$0.00 (真零非空)', () => { expect(fmtUsdc(0)).toBe('+$0.00'); });
});

describe('fmtBps / fmtPct', () => {
  it('fmtBps 有数', () => { expect(fmtBps(50)).toMatch(/50/); });
  it('fmtPct null → 占位', () => { expect(fmtPct(null)).toBe('—'); });
});

describe('fmtUptime', () => {
  it('秒 → 人类可读', () => { const s = fmtUptime(3661); expect(typeof s).toBe('string'); expect(s.length).toBeGreaterThan(0); });
});

describe('stalenessMs', () => {
  it('null → null', () => { expect(stalenessMs(null)).toBeNull(); });
  it('过去时刻 → 正延迟', () => {
    const pastNs = (Date.now() - 5000) * 1e6;
    const ms = stalenessMs(pastNs);
    expect(ms).not.toBeNull();
    expect(ms!).toBeGreaterThan(4000);
    expect(ms!).toBeLessThan(7000);
  });
});

describe('modeBadge', () => {
  it('live → LIVE', () => { expect(modeBadge('live').text).toBe('LIVE'); });
  it('其他 → PAPER', () => { expect(modeBadge('paper').text).toBe('PAPER'); });
});
