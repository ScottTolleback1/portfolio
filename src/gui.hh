#pragma once

#include "database.hh"
#include "search.hh"
#include "stock.hh"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

/*
 * PortfolioWidget
 * ----------------
 * A Qt-based GUI widget that connects the search system, SQLite database,
 * and stock analytics engine into a simple interactive interface.
 *
 * Responsibilities:
 *  - Accept user input for ticker or company name
 *  - Use `search::Search` to find the best matching ticker
 *  - Query the `Database` for cached or freshly updated stock data
 *  - Display summary analytics from `stock::Stock`
 *
 * Acts as the main frontend component of the Portfolio Analytics application.
 */
class PortfolioWidget : public QWidget
{
  Q_OBJECT
public:
  explicit PortfolioWidget(Database* db, QWidget* parent = nullptr);

private slots:
  void onFetchClicked();

private:
  Database* db_;
  search::Search* search_;
  stock::Stock* stock_;

  QLineEdit* input_;
  QPushButton* button_;
  QLabel* output_;
};
