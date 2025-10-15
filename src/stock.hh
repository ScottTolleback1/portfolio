#pragma once

#include <string>

namespace stock
{

/*
 * stock::Stock
 * ----------------
 * Represents a single company’s financial and market fundamentals,
 * providing methods for computing valuation metrics and intrinsic value.
 *
 * Responsibilities:
 *  - Store key balance sheet, income statement, and cash flow data
 *  - Compute derived metrics:
 *       • Book value per share (BVPS)
 *       • Free cash flow (FCF)
 *       • Enterprise value (EV)
 *       • P/E, P/B, EV/EBITDA ratios
 *  - Estimate intrinsic value using a discounted cash flow (DCF) model
 *  - Assess relative undervaluation vs. current market price
 *
 * Used by:
 *  - Database module (to populate Stock objects from SQLite)
 *  - PortfolioWidget (to display formatted valuation summaries)
 */
class Stock
{
private:
  std::string ticker_;
  std::string currency_;
  std::string sector_;

  double price_ = 0.0;
  double sharesOutstanding_ = 0.0;
  double marketCap_ = 0.0;

  double totalAssets_ = 0.0;
  double totalLiabilities_ = 0.0;
  double totalDebt_ = 0.0;
  double totalCash_ = 0.0;

  double ebit_ = 0.0;
  double ebitda_ = 0.0;
  double netIncome_ = 0.0;
  double totalRevenue_ = 0.0;

  double operatingCashFlow_ = 0.0;
  double capitalExpenditures_ = 0.0;

  double beta_ = 0.0;
  double growthRate_ = 0.03;
  double discountRate_ = 0.08;
  double taxRate_ = 0.21;

public:
  Stock() = default;
  Stock(std::string ticker, double price, double shares);

  void setBalanceSheet(double assets, double liabilities, double debt, double cash);
  void setIncomeStatement(double ebit, double ebitda, double netIncome, double revenue);
  void setCashFlow(double ocf, double capex);
  void setParameters(double beta, double growth, double discount, double tax = 0.21);

  const std::string& ticker() const
  {
    return ticker_;
  }
  double price() const
  {
    return price_;
  }
  double sharesOutstanding() const
  {
    return sharesOutstanding_;
  }

  double bookValuePerShare() const;
  double freeCashFlow() const;
  double enterpriseValue() const;
  double peRatio() const;
  double pbRatio() const;
  double evToEbitda() const;

  double intrinsicValueDCF(int years = 5) const;
  double undervaluationPercent() const;

  std::string summaryString() const;
};

} // namespace stock
