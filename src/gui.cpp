#include "gui.hh"
#include "search.hh"
#include "stock.hh"

#include <QString>

PortfolioWidget::PortfolioWidget(Database* db, QWidget* parent) : QWidget(parent), db_(db)
{
  input_ = new QLineEdit(this);
  button_ = new QPushButton("Fetch", this);
  output_ = new QLabel("Waiting for input...", this);
  search_ = new search::Search("../data/tickers.db");
  stock_ = nullptr;

  auto* layout = new QVBoxLayout(this);
  layout->addWidget(input_);
  layout->addWidget(button_);
  layout->addWidget(output_);
  setLayout(layout);

  connect(button_, &QPushButton::clicked, this, &PortfolioWidget::onFetchClicked);
}

void
PortfolioWidget::onFetchClicked()
{
  QString query = input_->text().trimmed();
  if (query.isEmpty()) {
    output_->setText("Enter a company name or ticker symbol.");
    return;
  }

  auto [ticker, score] = search_->findBestMatch(query.toStdString());
  if (ticker.empty()) {
    output_->setText("No match found.");
    return;
  }

  stock::Stock stock;

  if (!db_->loadStockData(ticker, stock)) {
    output_->setText(
        QString("Ticker: %1\nNo cached data. Request queued.").arg(QString::fromStdString(ticker)));
    return;
  }

  std::string summary = stock.summaryString();
  output_->setText(QString::fromStdString(summary));
}

#include "gui.moc"
