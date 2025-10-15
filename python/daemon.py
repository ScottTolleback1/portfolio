import sqlite3
import yfinance as yf
import numpy as np
import pandas as pd
import time
import os
import datetime
from init_db import init_db, init_nasdaq_db

"""
daemon.py
----------
Main background process for the Portfolio Analytics system.

This script periodically fetches and updates stock market data,
financial fundamentals, and valuation metrics from Yahoo Finance
using `yfinance`, and stores them in a local SQLite database.

Responsibilities:
- Initialize and maintain `portfolio.db` (prices, fundamentals, etc.)
- Periodically refresh all tickers or handle on-demand update requests
- Compute and store financial metrics such as balance sheet, income,
  and cash flow data for use in the C++ analytics GUI.

Modules:
- init_db: creates database schema if missing
- init_nasdaq_db: builds the searchable ticker symbol database

Run:
    $ python3 daemon.py

Notes:
    - This process is designed to run continuously in the background.
    - All documentation were assisted by AI.
"""
TICKER_DB_PATH = os.path.abspath(os.path.join(os.path.dirname(__file__), "../data/tickers.db"))
CSV_PATH = os.path.abspath(os.path.join(os.path.dirname(__file__), "../data/nasdaq-listed-symbols.csv"))
DB_PATH = os.path.abspath(os.path.join(os.path.dirname(__file__), "../data/portfolio.db"))

BATCH_SIZE = 50
SLEEP_IDLE = 5.0
SLEEP_ACTIVE = 0.2
REFRESH_INTERVAL = 300

def fetch_and_store_fundamentals(t, ticker, conn):
    """Fetch and insert balance sheet, income statement, and cashflow data."""
    try:
        bs = t.balance_sheet
        if bs is not None and not bs.empty:
            bs = bs.T
            for date, row in bs.iterrows():
                row.index = row.index.str.strip()
                conn.execute("""
                    INSERT OR REPLACE INTO balance_sheet
                    (ticker, date, total_assets, total_liabilities, total_debt, total_cash)
                    VALUES (?, ?, ?, ?, ?, ?)
                """, (
                    ticker, str(date.date()),
                    float(row.get("Total Assets", 0.0)),
                    float(row.get("Total Liabilities Net Minority Interest",
                                 row.get("Total Non Current Liabilities Net Minority Interest", 0.0))),
                    float(row.get("Total Debt", row.get("Long Term Debt", 0.0))),
                    float(row.get("Cash And Cash Equivalents",
                                 row.get("Cash Cash Equivalents And Short Term Investments", 0.0)))
                ))

        inc = t.financials
        if inc is not None and not inc.empty:
            inc = inc.T
            for date, row in inc.iterrows():
                row.index = row.index.str.strip()
                conn.execute("""
                    INSERT OR REPLACE INTO income_statement
                    (ticker, date, ebit, ebitda, net_income, total_revenue)
                    VALUES (?, ?, ?, ?, ?, ?)
                """, (
                    ticker, str(date.date()),
                    float(row.get("EBIT", row.get("Operating Income", 0.0))),
                    float(row.get("EBITDA", row.get("Operating Income", 0.0))),
                    float(row.get("Net Income", row.get("Net Income Continuous Operations", 0.0))),
                    float(row.get("Total Revenue", row.get("Operating Revenue", 0.0)))
                ))

        cf = t.cashflow
        if cf is not None and not cf.empty:
            cf = cf.T
            for date, row in cf.iterrows():
                row.index = row.index.str.strip()
                conn.execute("""
                    INSERT OR REPLACE INTO cashflow_statement
                    (ticker, date, operating_cash_flow, capital_expenditures)
                    VALUES (?, ?, ?, ?)
                """, (
                    ticker, str(date.date()),
                    float(row.get("Operating Cash Flow",
                                 row.get("Cash Flow From Continuing Operating Activities", 0.0))),
                    abs(float(row.get("Capital Expenditure",
                                 row.get("Net PPE Purchase And Sale", 0.0))))
                ))

        conn.commit()

    except Exception as e:
        print(f"[FUNDAMENTALS ERROR] {ticker}: {e}")

def fetch_and_store_batch(tickers):
    """Fetch prices and all data for a batch of tickers."""
    if not tickers:
        return

    tickers = [t.upper() for t in tickers]
    conn = sqlite3.connect(DB_PATH, timeout=5.0)
    conn.execute("PRAGMA journal_mode=WAL;")

    for ticker in tickers:
        try:
            t = yf.Ticker(ticker)
            price = None

            try:
                price = t.fast_info.last_price
            except Exception:
                pass
            if price is None or price == 0:
                df = t.history(period="1d")
                if not df.empty:
                    price = float(df["Close"].iloc[-1])
            if price is None:
                print(f"[SKIP] {ticker}: No price data found")
                continue

            date = pd.Timestamp.now().strftime("%Y-%m-%d %H:%M:%S")
            info = t.info or {}

            shares = info.get("sharesOutstanding", 0.0)
            market_cap = info.get("marketCap", price * shares)
            beta = info.get("beta", None)
            name = info.get("shortName", ticker)
            currency = info.get("currency", "USD")
            sector = info.get("sector", None)

            growth_rate = 0.03
            rf, rm = 0.04, 0.08
            discount_rate = rf + (beta or 1.0) * (rm - rf)
            tax_rate = 0.21

            conn.execute("""
                INSERT OR REPLACE INTO stocks
                    (ticker, name, currency, sector, shares_outstanding,
                     price, market_cap, beta, growth_rate, discount_rate,
                     tax_rate, last_updated)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP)
            """, (
                ticker, name, currency, sector, shares,
                price, market_cap, beta, growth_rate, discount_rate, tax_rate
            ))

            conn.execute("""
                INSERT OR REPLACE INTO prices (ticker, date, close, last_updated)
                VALUES (?, ?, ?, CURRENT_TIMESTAMP)
            """, (ticker, date, price))

            conn.execute("""
                INSERT OR IGNORE INTO price_history (ticker, date, close, last_updated)
                VALUES (?, DATE('now'), ?, CURRENT_TIMESTAMP)
            """, (ticker, price))

            print(f"[SYNC] {ticker}: refreshing fundamentals...")
            fetch_and_store_fundamentals(t, ticker, conn)

            print(f"[UPDATE] {ticker}: {price:.2f} {currency}")

        except Exception as e:
            print(f"[ERROR] {ticker}: {e}")

    conn.commit()
    conn.execute("PRAGMA wal_checkpoint(FULL);")
    conn.close()

def refresh_all_tickers():
    """Refresh all tickers already stored in the prices table."""
    conn = sqlite3.connect(DB_PATH, timeout=5.0)
    cur = conn.cursor()
    cur.execute("SELECT DISTINCT ticker FROM prices")
    tickers = [r[0] for r in cur.fetchall()]
    conn.close()

    if not tickers:
        print("[REFRESH] No tickers in database yet.")
        return

    print(f"[REFRESH] Refreshing {len(tickers)} tickers...")
    for i in range(0, len(tickers), BATCH_SIZE):
        subset = tickers[i:i + BATCH_SIZE]
        fetch_and_store_batch(subset)

def run_daemon():
    print("[INIT] Starting daemon...")
    last_refresh_time = 0
    sleep_time = SLEEP_IDLE

    refresh_all_tickers()
    last_refresh_time = time.time()

    while True:
        conn = sqlite3.connect(DB_PATH, timeout=5.0)
        cur = conn.cursor()
        cur.execute("SELECT id, ticker FROM update_requests WHERE processed = 0")
        requests = cur.fetchall()

        if requests:
            sleep_time = SLEEP_ACTIVE
            tickers = [ticker for (_, ticker) in requests]
            request_ids = [req_id for (req_id, _) in requests]

            cur.executemany("DELETE FROM update_requests WHERE id = ?", [(rid,) for rid in request_ids])
            conn.commit()
            conn.close()

            print(f"[WAKE] Processing {len(tickers)} request(s): {', '.join(tickers)}")
            fetch_and_store_batch(tickers)

        elif time.time() - last_refresh_time >= REFRESH_INTERVAL:
            refresh_all_tickers()
            last_refresh_time = time.time()
            conn.close()

        else:
            conn.close()
            sleep_time = min(sleep_time * 1.5, SLEEP_IDLE)

        time.sleep(sleep_time)

if __name__ == "__main__":
    init_db(overwrite=False)
    init_nasdaq_db(overwrite=False)
    run_daemon()
