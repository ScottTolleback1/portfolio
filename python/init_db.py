import os
import sqlite3
import pandas as pd
import re

"""
init_db.py
-----------
Database initialization and ticker list creation utilities
for the Portfolio Analytics system.

This script provides two main functions:

1. init_db():
   - Initializes the main `portfolio.db` SQLite database
     with tables for prices, stocks, balance sheets,
     income statements, cash flows, and valuation metrics.

2. init_nasdaq_db():
   - Downloads and merges NASDAQ + NYSE + AMEX ticker data
     from official NASDAQ FTP sources.
   - Cleans and normalizes company names (removing suffixes like
     "Inc", "Corp", "Class A", etc.) and stores them in `tickers.db`.

Used by:
- daemon.py (for background updates)
- C++ application (for ticker search and analytics)

Run manually if databases need to be recreated:
    $ python3 init_db.py

Note:
    All documentation were written with AI assistance.
"""
NASDAQ_URL = "ftp://ftp.nasdaqtrader.com/SymbolDirectory/nasdaqlisted.txt"
OTHER_URL  = "ftp://ftp.nasdaqtrader.com/SymbolDirectory/otherlisted.txt"

CORP_STOPWORDS = [
    "INC", "INCORPORATED", "CORP", "CORPORATION",
    "TECH", "TECHNOLOGY", "TECHNOLOGIES",
    "COMPANY", "CO", "GROUP", "HOLDINGS",
    "LIMITED", "LTD", "COMMUNICATIONS", "COMMUNICATION",
    "SYSTEMS", "PLC", "SA", "LLC", "COMMON", "STOCK",
    "SHARES"
]

def clean_company_name(name: str) -> str:
    """Normalize and remove corporate suffixes and multi-word phrases."""
    if not isinstance(name, str):
        return ""

    name = name.upper()
    name = re.sub(r"[^A-Z0-9 ]", " ", name)

    multi_phrases = [
        "CLASS A", "CLASS B", "CLASS C",
        "COMMON STOCK", "PREFERRED STOCK",
        "WARRANT", "WARRANTS"
    ]
    for phrase in multi_phrases:
        name = name.replace(phrase, " ")

    tokens = [t for t in name.split() if t not in CORP_STOPWORDS]

    cleaned = " ".join(tokens)
    cleaned = re.sub(r"\s+", " ", cleaned).strip()
    return cleaned


def init_db(overwrite=True, path=None):
    base_dir = os.path.dirname(os.path.abspath(__file__))
    if path is None:
        path = os.path.join(base_dir, "../data/portfolio.db")
    path = os.path.abspath(path)

    if overwrite and os.path.exists(path):
        os.remove(path)
        print(f"[DB] Existing database removed: {path}")
    elif not overwrite and os.path.exists(path):
        print(f"[DB] Database already exists, skipping creation: {path}")
        return

    os.makedirs(os.path.dirname(path), exist_ok=True)

    conn = sqlite3.connect(path)
    cur = conn.cursor()

    cur.execute("""
    CREATE TABLE IF NOT EXISTS prices (
        ticker TEXT PRIMARY KEY,
        date TEXT,
        close REAL,
        last_updated TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    )
    """)

    cur.execute("""
    CREATE TABLE IF NOT EXISTS price_history (
        ticker TEXT,
        date TEXT,
        close REAL,
        last_updated TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
        PRIMARY KEY (ticker, date)
    )
    """)

    cur.execute("""
    CREATE TABLE IF NOT EXISTS update_requests (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        ticker TEXT NOT NULL,
        requested_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
        processed INTEGER DEFAULT 0
    )
    """)

    cur.execute("""
  CREATE TABLE IF NOT EXISTS stocks (
    ticker TEXT PRIMARY KEY,
    name TEXT,
    currency TEXT DEFAULT 'USD',
    sector TEXT,
    shares_outstanding REAL,
    price REAL,
    market_cap REAL,
    beta REAL,
    growth_rate REAL,
    discount_rate REAL,
    tax_rate REAL,
    last_updated TIMESTAMP DEFAULT CURRENT_TIMESTAMP
  )
  """)

    cur.execute("""
    CREATE TABLE IF NOT EXISTS balance_sheet (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      ticker TEXT NOT NULL,
      date TEXT NOT NULL,
      total_assets REAL,
      total_liabilities REAL,
      total_debt REAL,
      total_cash REAL,
      FOREIGN KEY (ticker) REFERENCES stocks(ticker)
    )
    """)

    cur.execute("""
    CREATE TABLE IF NOT EXISTS income_statement (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      ticker TEXT NOT NULL,
      date TEXT NOT NULL,
      ebit REAL,
      ebitda REAL,
      net_income REAL,
      total_revenue REAL,
      FOREIGN KEY (ticker) REFERENCES stocks(ticker)
    )
    """)

    cur.execute("""
    CREATE TABLE IF NOT EXISTS cashflow_statement (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      ticker TEXT NOT NULL,
      date TEXT NOT NULL,
      operating_cash_flow REAL,
      capital_expenditures REAL,
      FOREIGN KEY (ticker) REFERENCES stocks(ticker)
    )
    """)

    cur.execute("""
    CREATE TABLE IF NOT EXISTS valuations (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      ticker TEXT NOT NULL,
      intrinsic_value REAL,
      undervaluation_percent REAL,
      pe_ratio REAL,
      pb_ratio REAL,
      ev_ebitda REAL,
      computed_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
      FOREIGN KEY (ticker) REFERENCES stocks(ticker)
    )
    """)

    conn.commit()

    cur.execute("SELECT name FROM sqlite_master WHERE type='table';")
    conn.close()


def init_nasdaq_db(overwrite=True, path=None):
    db_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "../data/tickers.db"))

    base_dir = os.path.dirname(os.path.abspath(__file__))
    if path is None:
        path = os.path.join(base_dir, "../data/tickers.db")
    path = os.path.abspath(path)

    if overwrite and os.path.exists(path):
        os.remove(path)
        print(f"[DB] Existing database removed: {path}")
    elif not overwrite and os.path.exists(path):
        print(f"[DB] Database already exists, skipping creation: {path}")
        return

    os.makedirs(os.path.dirname(path), exist_ok=True)

    df1 = pd.read_csv(NASDAQ_URL, sep="|")
    df2 = pd.read_csv(OTHER_URL, sep="|")

    df1 = df1.rename(columns={"Symbol": "ticker", "Security Name": "name"})
    df2 = df2.rename(columns={"ACT Symbol": "ticker", "Security Name": "name"})

    tickers = pd.concat([df1[["ticker", "name"]], df2[["ticker", "name"]]], ignore_index=True)
    tickers = tickers.dropna().drop_duplicates(subset=["ticker"])

    tickers["ticker"] = tickers["ticker"].astype(str).str.strip().str.upper()
    tickers["name"] = tickers["name"].astype(str).apply(clean_company_name)

    tickers = tickers[tickers["ticker"].str.isalpha()]
    tickers = tickers[tickers["name"].str.len() > 0]

    print(f"[CLEAN] {len(tickers)} valid tickers after cleaning.")

    conn = sqlite3.connect(db_path)
    cur = conn.cursor()
    cur.execute("""
        CREATE TABLE IF NOT EXISTS tickers (
            ticker TEXT PRIMARY KEY,
            company TEXT
        )
    """)

    tickers.rename(columns={"name": "company"}, inplace=True)
    tickers.to_sql("tickers", conn, if_exists="append", index=False)
    conn.commit()

    cur.execute("SELECT COUNT(*) FROM tickers;")
    count = cur.fetchone()[0]
    conn.close()

    print(f"[DB] Inserted {count} rows into {db_path}")
    print("[DONE] Combined ticker database (NASDAQ + NYSE + AMEX) created successfully.")
