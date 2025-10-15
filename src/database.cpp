#include "database.hh"
#include "stock.hh"

#include <chrono>
#include <iostream>
#include <sqlite3.h>
#include <thread>

Database::Database(const std::string& path) : db_path(path)
{
  if (sqlite3_open(path.c_str(), &db)) {
    throw std::runtime_error("Can't open DB: " + std::string(sqlite3_errmsg(db)));
  }
  sqlite3_busy_timeout(db, 1000);
  sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
}

Database::~Database()
{
  sqlite3_close(db);
}

std::string
Database::getPath() const
{
  return db_path;
}

void
Database::reopen()
{
  sqlite3_close(db);
  if (sqlite3_open(db_path.c_str(), &db)) {
    throw std::runtime_error("Can't reopen DB: " + std::string(sqlite3_errmsg(db)));
  }
  sqlite3_busy_timeout(db, 1000);
}

void
Database::insertRequest(const std::string& ticker)
{
  const char* check_sql = "SELECT 1 FROM update_requests WHERE ticker = ? AND "
                          "processed = 0 LIMIT 1;";
  sqlite3_stmt* check_stmt;
  sqlite3_prepare_v2(db, check_sql, -1, &check_stmt, nullptr);
  sqlite3_bind_text(check_stmt, 1, ticker.c_str(), -1, SQLITE_TRANSIENT);
  bool exists = (sqlite3_step(check_stmt) == SQLITE_ROW);
  sqlite3_finalize(check_stmt);
  if (exists)
    return;

  const char* sql = "INSERT INTO update_requests (ticker, processed, requested_at) "
                    "VALUES (?, 0, CURRENT_TIMESTAMP);";
  sqlite3_stmt* stmt;
  sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, ticker.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) != SQLITE_DONE)
    std::cerr << "Insert request failed: " << sqlite3_errmsg(db) << "\n";
  sqlite3_finalize(stmt);
  std::cout << "[DB] Queued update request for: " << ticker << "\n";
}

bool
Database::getLatestPrice(const std::string& ticker, std::string& date, double& close)
{
  const std::string sql = "SELECT date, close FROM prices WHERE ticker = ?;";

  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    std::cerr << "Prepare failed: " << sqlite3_errmsg(db) << "\n";
    return false;
  }

  sqlite3_bind_text(stmt, 1, ticker.c_str(), -1, SQLITE_TRANSIENT);
  bool found = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    close = sqlite3_column_double(stmt, 1);
    found = true;
  }
  sqlite3_finalize(stmt);

  if (found)
    return true;

  insertRequest(ticker);
  std::cout << "[WAIT] Waiting for update of " << ticker << " ...\n";

  for (int i = 0; i < 5; ++i) {
    std::this_thread::sleep_for(std::chrono::seconds(3));

    reopen();

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
      continue;

    sqlite3_bind_text(stmt, 1, ticker.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
      close = sqlite3_column_double(stmt, 1);
      sqlite3_finalize(stmt);
      std::cout << "[DB] Fetched new data for " << ticker << ".\n";
      return true;
    }
    sqlite3_finalize(stmt);
    std::cout << "[WAIT] Still no update (" << (i + 1) * 3 << "s)...\n";
  }

  std::cout << "[TIMEOUT] No data received for " << ticker << " after 15 seconds.\n";
  return false;
}

bool
Database::loadStockData(const std::string& ticker, stock::Stock& stock)
{
  auto has_fundamentals = [&](sqlite3* db, const std::string& t) -> bool {
    sqlite3_stmt* stmt = nullptr;
    std::string sql = "SELECT COUNT(*) FROM stocks WHERE ticker = ?;";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
      return false;
    sqlite3_bind_text(stmt, 1, t.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
      ok = sqlite3_column_int(stmt, 0) > 0;
    sqlite3_finalize(stmt);
    return ok;
  };

  if (!has_fundamentals(db, ticker)) {
    insertRequest(ticker);
    std::cout << "[WAIT] Fundamentals missing for " << ticker << ", requesting update...\n";

    for (int i = 0; i < 5; ++i) {
      std::this_thread::sleep_for(std::chrono::seconds(3));
      reopen();
      if (has_fundamentals(db, ticker)) {
        std::cout << "[DB] Fundamentals now available for " << ticker << ".\n";
        break;
      }
      std::cout << "[WAIT] Still no update (" << (i + 1) * 3 << "s)...\n";
    }

    if (!has_fundamentals(db, ticker)) {
      std::cout << "[TIMEOUT] No fundamentals received for " << ticker << " after 15s.\n";
      return false;
    }
  }

  sqlite3_stmt* stmt = nullptr;

  std::string sql = R"(
      SELECT currency, sector, shares_outstanding, price,
             beta, growth_rate, discount_rate, tax_rate
      FROM stocks WHERE ticker = ? LIMIT 1;
  )";
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    return false;
  sqlite3_bind_text(stmt, 1, ticker.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    std::string currency = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    std::string sector = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    double shares = sqlite3_column_double(stmt, 2);
    double price = sqlite3_column_double(stmt, 3);
    double beta = sqlite3_column_double(stmt, 4);
    double growth = sqlite3_column_double(stmt, 5);
    double discount = sqlite3_column_double(stmt, 6);
    double tax = sqlite3_column_double(stmt, 7);

    stock = stock::Stock(ticker, price, shares);
    stock.setParameters(beta, growth, discount, tax);
  }
  sqlite3_finalize(stmt);

  sql = R"(
      SELECT total_assets, total_liabilities, total_debt, total_cash
      FROM balance_sheet WHERE ticker = ?
      ORDER BY date DESC LIMIT 1;
  )";
  sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, ticker.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    stock.setBalanceSheet(sqlite3_column_double(stmt, 0), sqlite3_column_double(stmt, 1),
                          sqlite3_column_double(stmt, 2), sqlite3_column_double(stmt, 3));
  }
  sqlite3_finalize(stmt);

  sql = R"(
      SELECT ebit, ebitda, net_income, total_revenue
      FROM income_statement WHERE ticker = ?
      ORDER BY date DESC LIMIT 1;
  )";
  sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, ticker.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    stock.setIncomeStatement(sqlite3_column_double(stmt, 0), sqlite3_column_double(stmt, 1),
                             sqlite3_column_double(stmt, 2), sqlite3_column_double(stmt, 3));
  }
  sqlite3_finalize(stmt);

  sql = R"(
      SELECT operating_cash_flow, capital_expenditures
      FROM cashflow_statement WHERE ticker = ?
      ORDER BY date DESC LIMIT 1;
  )";
  sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, ticker.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    stock.setCashFlow(sqlite3_column_double(stmt, 0), sqlite3_column_double(stmt, 1));
  }
  sqlite3_finalize(stmt);

  return true;
}
