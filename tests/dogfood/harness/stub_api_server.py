#!/usr/bin/env python3
"""
tests/dogfood/harness/stub_api_server.py

TOOL-LEVEL PYTHON ONLY — NOT PRODUCTION HOT PATH

Local stub HTTP server for paper runtime dogfood testing.
Mimics the stcpp_trader debug API endpoints defined in:
  docs/RESEARCH/xiaozheng-observability-endpoints-v1.md

Responds with static/minimal JSON so dogfood probe scripts
can run without a live paper runtime binary.

Usage:
    python3 tests/dogfood/harness/stub_api_server.py [--port PORT]

Owner: 小宫 (dogfood-evaluator)
Date: 2026-05-29
"""

import argparse
import json
import time
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from typing import Any

# Stub state
_START_TIME = time.time()
_REJECT_COUNT = 0
_reject_lock = threading.Lock()


def _now_ms() -> int:
    return int(time.time() * 1000)


def _now_ns() -> int:
    return int(time.time() * 1_000_000_000)


# Stub response payloads (mirror xiaozheng-observability-endpoints-v1.md)

def _resp_healthz() -> dict:
    return {
        "ok": True,
        "as_of_ts": _now_ns(),
        "uptime_seconds": int(time.time() - _START_TIME),
    }


def _resp_version() -> dict:
    return {
        "version": "0.0.0-stub",
        "build_mode": "paper",
        "git_sha": "stubstub",
        "build_ts": "2026-05-29T00:00:00Z",
    }


def _resp_status() -> dict:
    return {
        "state": "RUNNING",
        "mode": "paper",
        "rm_state": "RUNNING",
        "book_count": 42,
        "uptime_seconds": int(time.time() - _START_TIME),
        "as_of_ts": _now_ns(),
    }


def _resp_metrics() -> str:
    uptime = int(time.time() - _START_TIME)
    lines = [
        "# HELP stcpp_uptime_seconds Process uptime",
        "# TYPE stcpp_uptime_seconds gauge",
        f'stcpp_uptime_seconds {uptime}',
        "# HELP stcpp_wss_connected WSS connection state (0/1)",
        "# TYPE stcpp_wss_connected gauge",
        'stcpp_wss_connected{source="polymarket"} 1',
        'stcpp_wss_connected{source="goalserve"} 1',
        "# HELP stcpp_wss_reconnect_total WSS reconnect counter",
        "# TYPE stcpp_wss_reconnect_total counter",
        'stcpp_wss_reconnect_total{source="polymarket"} 0',
        'stcpp_wss_reconnect_total{source="goalserve"} 0',
        "# HELP stcpp_data_staleness_seconds Data staleness",
        "# TYPE stcpp_data_staleness_seconds gauge",
        'stcpp_data_staleness_seconds{source="polymarket",market_type="pregame"} 0.5',
        'stcpp_data_staleness_seconds{source="goalserve",market_type="pregame"} 0.8',
        "# HELP stcpp_data_gap_total Silent data drop counter",
        "# TYPE stcpp_data_gap_total counter",
        'stcpp_data_gap_total{source="polymarket"} 0',
        'stcpp_data_gap_total{source="goalserve"} 0',
        "# HELP stcpp_rm_decision_total RM decision counter",
        "# TYPE stcpp_rm_decision_total counter",
        'stcpp_rm_decision_total{decision="approve",reason_code=""} 87',
        'stcpp_rm_decision_total{decision="reject",reason_code="STALE_DATA"} 5',
        'stcpp_rm_decision_total{decision="reject",reason_code="EXCEED_PER_ORDER_CAP"} 3',
        'stcpp_rm_decision_total{decision="reject",reason_code="DUPLICATE_INTENT"} 2',
        "# HELP stcpp_fill_total Fill counter",
        "# TYPE stcpp_fill_total counter",
        'stcpp_fill_total{market_type="pregame_moneyline"} 71',
        "# HELP stcpp_order_total Order counter",
        "# TYPE stcpp_order_total counter",
        'stcpp_order_total{market_type="pregame_moneyline"} 87',
        "# HELP stcpp_pnl_usd Paper PnL in USD",
        "# TYPE stcpp_pnl_usd gauge",
        'stcpp_pnl_usd{mode="paper"} 123.45',
        "# HELP stcpp_exposure_usd Current exposure",
        "# TYPE stcpp_exposure_usd gauge",
        'stcpp_exposure_usd 2500.00',
        "# HELP stcpp_bankroll_usd Bankroll",
        "# TYPE stcpp_bankroll_usd gauge",
        'stcpp_bankroll_usd 20000.00',
        "# HELP stcpp_net_edge_bps Net edge distribution",
        "# TYPE stcpp_net_edge_bps histogram",
        'stcpp_net_edge_bps_bucket{sport="NBA",le="0"} 10',
        'stcpp_net_edge_bps_bucket{sport="NBA",le="50"} 45',
        'stcpp_net_edge_bps_bucket{sport="NBA",le="+Inf"} 71',
        'stcpp_net_edge_bps_sum{sport="NBA"} 2840.5',
        'stcpp_net_edge_bps_count{sport="NBA"} 71',
        "# HELP stcpp_event_loop_latency_seconds Event loop latency",
        "# TYPE stcpp_event_loop_latency_seconds histogram",
        'stcpp_event_loop_latency_seconds_bucket{loop="pm_wss",le="0.0001"} 990',
        'stcpp_event_loop_latency_seconds_bucket{loop="pm_wss",le="+Inf"} 1000',
        'stcpp_event_loop_latency_seconds_sum{loop="pm_wss"} 0.085',
        'stcpp_event_loop_latency_seconds_count{loop="pm_wss"} 1000',
        "# HELP stcpp_audit_wal_ring_fill_ratio Audit WAL ring buffer fill ratio",
        "# TYPE stcpp_audit_wal_ring_fill_ratio gauge",
        'stcpp_audit_wal_ring_fill_ratio 0.12',
        "# HELP stcpp_ts_lag_seconds Timestamp lag by stage",
        "# TYPE stcpp_ts_lag_seconds histogram",
        'stcpp_ts_lag_seconds_bucket{stage="event_to_source",source="polymarket",le="0.005"} 950',
        'stcpp_ts_lag_seconds_bucket{stage="event_to_source",source="polymarket",le="+Inf"} 1000',
        "# HELP stcpp_gate_sharpe_30d Gate 30d Sharpe",
        "# TYPE stcpp_gate_sharpe_30d gauge",
        'stcpp_gate_sharpe_30d 0.73',
    ]
    return "\n".join(lines) + "\n"


def _resp_risk_rejects(n: int) -> dict:
    global _REJECT_COUNT
    results = []
    with _reject_lock:
        for i in range(min(n, 10)):
            results.append({
                "intent_id": f"01HZ{i:014X}",
                "reason_code": "STALE_DATA" if i % 3 == 0 else
                               "EXCEED_PER_ORDER_CAP" if i % 3 == 1 else
                               "DUPLICATE_INTENT",
                "bankroll_usd": 19800.0 - i * 10,
                "ts_ns": _now_ns() - i * 1_000_000_000,
                "market_type": "pregame_moneyline",
                "condition_id": f"0xCOND{i:016X}",
            })
    return {"rejects": results, "total": _REJECT_COUNT, "as_of_ts": _now_ns()}


def _resp_trace_recent(n: int) -> dict:
    traces = []
    for i in range(min(n, 10)):
        traces.append({
            "intent_id": f"01HZ{i:014X}",
            "status": "FILLED" if i % 4 != 0 else "REJECTED",
            "signal_emit_ts_ns": _now_ns() - (10 - i) * 1_000_000_000,
            "e2e_latency_ns": 12_000_000 + i * 500_000,
            "market_type": "pregame_moneyline",
        })
    return {"traces": traces, "as_of_ts": _now_ns()}


def _resp_trace_detail(intent_id: str) -> dict:
    return {
        "intent_id": intent_id,
        "trace_id": intent_id,
        "spans": [
            {
                "name": "signal.emit",
                "start_ns": _now_ns() - 50_000_000,
                "end_ns": _now_ns() - 49_000_000,
                "attributes": {"signal_id": "sig_p001", "edge_bps": 42},
            },
            {
                "name": "rm.evaluate",
                "start_ns": _now_ns() - 49_000_000,
                "end_ns": _now_ns() - 48_500_000,
                "attributes": {"decision": "APPROVE", "reason_code": ""},
            },
            {
                "name": "signer.sign",
                "start_ns": _now_ns() - 48_500_000,
                "end_ns": _now_ns() - 38_000_000,
                "attributes": {"nonce": intent_id[:8], "pUSD_micro": 10_000_000},
            },
            {
                "name": "exec.match",
                "start_ns": _now_ns() - 38_000_000,
                "end_ns": _now_ns() - 2_000_000,
                "attributes": {
                    "fill_status": "FILLED",
                    "fill_price": 0.557,
                    "slippage_bps": 7,
                },
            },
        ],
        "as_of_ts": _now_ns(),
    }


def _resp_gate_paper() -> dict:
    # Stub: prelim_pass=True (14d), confirm_pass=False (< 30d)
    elapsed_days = (time.time() - _START_TIME) / 86400.0
    return {
        "window_days": 30,
        "elapsed_days": round(elapsed_days, 2),
        "n_trades": 87,
        "positive_day_ratio": 0.57,
        "sharpe_30d": 0.73,
        "sharpe_se": 0.18,
        "p_value": 0.041,
        "hit_rate": 0.57,
        "mdd": -0.028,
        "net_pnl_usd": 123.45,
        "prelim_pass": elapsed_days >= 14,
        "confirm_pass": False,
        "rm_reject_rate": 0.115,
        "rm_bypass_count": 0,
        "r11_cross_write_count": 0,
        "as_of_ts": _now_ns(),
    }


def _resp_data_latency(market_id: str) -> dict:
    return {
        "market_id": market_id,
        "event_to_source_ms": 1.2,
        "source_to_ingest_ms": 3.4,
        "ingest_to_asof_ms": 0.8,
        "total_lag_ms": 5.4,
        "ts_chain_valid": True,
        "as_of_ts": _now_ns(),
    }


def _resp_logs(level: str, limit: int) -> dict:
    entries = []
    for i in range(min(limit, 5)):
        entries.append({
            "ts_ns": _now_ns() - i * 10_000_000_000,
            "level": level.upper(),
            "module": "paper_runtime",
            "message": f"stub log entry {i}",
            "trace_id": f"01HZ{i:014X}",
        })
    return {"logs": entries, "as_of_ts": _now_ns()}


class StubHandler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass  # suppress default access log

    def _send_json(self, data: Any, status: int = 200):
        body = json.dumps(data).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_text(self, body: str, status: int = 200,
                   content_type: str = "text/plain"):
        encoded = body.encode()
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def _parse_query(self) -> dict:
        from urllib.parse import urlparse, parse_qs
        parsed = urlparse(self.path)
        qs = parse_qs(parsed.query)
        return {k: v[0] for k, v in qs.items()}

    def _path_without_query(self) -> str:
        from urllib.parse import urlparse
        return urlparse(self.path).path

    def do_GET(self):
        path = self._path_without_query()
        query = self._parse_query()

        if path == "/healthz":
            self._send_json(_resp_healthz())

        elif path == "/version":
            self._send_json(_resp_version())

        elif path == "/status":
            self._send_json(_resp_status())

        elif path == "/metrics":
            self._send_text(_resp_metrics(), content_type="text/plain; charset=utf-8")

        elif path == "/risk/rejects":
            n = int(query.get("n", "50"))
            self._send_json(_resp_risk_rejects(n))

        elif path == "/trace/recent":
            n = int(query.get("n", "50"))
            self._send_json(_resp_trace_recent(n))

        elif path.startswith("/trace/"):
            intent_id = path[len("/trace/"):]
            if intent_id:
                self._send_json(_resp_trace_detail(intent_id))
            else:
                self._send_json({"error": "missing intent_id"}, 400)

        elif path == "/gate/paper":
            self._send_json(_resp_gate_paper())

        elif path.startswith("/data/latency/"):
            market_id = path[len("/data/latency/"):]
            self._send_json(_resp_data_latency(market_id or "unknown"))

        elif path == "/logs":
            level = query.get("level", "info")
            limit = int(query.get("limit", "20"))
            self._send_json(_resp_logs(level, limit))

        else:
            self._send_json({"error": f"unknown path: {path}"}, 404)


def main():
    parser = argparse.ArgumentParser(
        description="Paper runtime stub API server for dogfood testing"
    )
    parser.add_argument("--port", type=int, default=19091)
    parser.add_argument("--host", default="127.0.0.1")
    args = parser.parse_args()

    server = HTTPServer((args.host, args.port), StubHandler)
    print(f"[stub_api_server] listening on http://{args.host}:{args.port}")
    print("[stub_api_server] endpoints: /healthz /version /status /metrics")
    print("                             /risk/rejects /trace/recent /trace/{id}")
    print("                             /gate/paper /data/latency/{id} /logs")
    print("[stub_api_server] Ctrl-C to stop")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[stub_api_server] stopped")


if __name__ == "__main__":
    main()
