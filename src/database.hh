#pragma once

#include "stock.hh"
#include <sqlite3.h>
#include <string>

/*
 * Database class
 * ----------------
 * Provides an interface for interacting with the SQLite database that stores
 * stock prices, fundamentals, and valuation data.
 *
 * Responsibilities:
 *  - Open and manage the SQLite connection
 *  - Fetch latest price data and insert update requests
 *  - Load full stock data (balance sheet, income, cash flow) into Stock objects
 *
 * Used by:
 *  - GUI layer (PortfolioWidget) to retrieve and display financial data
 */
class Database
{
public:
  explicit Database(const std::string& path);
  ~Database();

  std::string getPath() const;
  bool getLatestPrice(const std::string& ticker, std::string& date, double& close);
  void insertRequest(const std::string& ticker);
  bool loadStockData(const std::string& ticker, stock::Stock& stock);

private:
  sqlite3* db = nullptr;
  std::string db_path;

  void reopen();
};
