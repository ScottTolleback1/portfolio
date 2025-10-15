#include "stock.hh"

#include <iomanip>
#include <sstream>

namespace stock
{

Stock::Stock(std::string ticker, double price, double shares)
    : ticker_(std::move(ticker)), price_(price), sharesOutstanding_(shares)
{
  marketCap_ = price_ * sharesOutstanding_;
}

void
Stock::setBalanceSheet(double assets, double liabilities, double debt, double cash)
{
  totalAssets_ = assets;
  totalLiabilities_ = liabilities;
  totalDebt_ = debt;
  totalCash_ = cash;
}

void
Stock::setIncomeStatement(double ebit, double ebitda, double netIncome, double revenue)
{
  ebit_ = ebit;
  ebitda_ = ebitda;
  netIncome_ = netIncome;
  totalRevenue_ = revenue;
}

void
Stock::setCashFlow(double ocf, double capex)
{
  operatingCashFlow_ = ocf;
  capitalExpenditures_ = capex;
}

void
Stock::setParameters(double beta, double growth, double discount, double tax)
{
  beta_ = beta;
  growthRate_ = growth;
  discountRate_ = discount;
  taxRate_ = tax;
}

double
Stock::bookValuePerShare() const
{
  if (sharesOutstanding_ <= 0.0)
    return 0.0;
  return (totalAssets_ - totalLiabilities_) / sharesOutstanding_;
}

double
Stock::freeCashFlow() const
{
  return operatingCashFlow_ - std::abs(capitalExpenditures_);
}

double
Stock::enterpriseValue() const
{
  return marketCap_ + totalDebt_ - totalCash_;
}

double
Stock::peRatio() const
{
  double eps = sharesOutstanding_ > 0 ? netIncome_ / sharesOutstanding_ : 0.0;
  return eps > 0 ? price_ / eps : 0.0;
}

double
Stock::pbRatio() const
{
  double bvps = bookValuePerShare();
  return bvps > 0 ? price_ / bvps : 0.0;
}

double
Stock::evToEbitda() const
{
  return ebitda_ > 0 ? enterpriseValue() / ebitda_ : 0.0;
}

double
Stock::intrinsicValueDCF(int years) const
{
  double fcf0 = operatingCashFlow_ - capitalExpenditures_;
  double pv_sum = 0.0;

  for (int t = 1; t <= years; ++t)
    pv_sum += fcf0 * std::pow(1 + growthRate_, t) / std::pow(1 + discountRate_, t);

  double fcfN = fcf0 * std::pow(1 + growthRate_, years);
  double tv = fcfN * (1 + growthRate_) / (discountRate_ - growthRate_);
  double pv_tv = tv / std::pow(1 + discountRate_, years);

  double ev = pv_sum + pv_tv;
  double equity = ev - totalDebt_ + totalCash_;
  return sharesOutstanding_ > 0 ? equity / sharesOutstanding_ : 0.0;
}

double
Stock::undervaluationPercent() const
{
  double intrinsic = intrinsicValueDCF(5);
  return price_ > 0.0 ? (intrinsic - price_) / price_ * 100.0 : 0.0;
}

std::string
Stock::summaryString() const
{
  std::ostringstream os;
  os << std::fixed << std::setprecision(2);

  double bvps = bookValuePerShare();
  double fcf = freeCashFlow();
  double ev = enterpriseValue();
  double pe = peRatio();
  double pb = pbRatio();
  double evEbitda = evToEbitda();
  double intrinsic = intrinsicValueDCF();
  double underval = undervaluationPercent();

  os << "------------------------------------------------------------\n";
  os << " Stock Summary: " << ticker_ << "\n";
  os << "------------------------------------------------------------\n";
  os << " Market Price:           " << std::setw(12) << price_ << " " << currency_ << "\n";
  os << " Shares Outstanding:     " << std::setw(12) << sharesOutstanding_ / 1e6 << " M\n";
  os << " Market Cap:             " << std::setw(12) << marketCap_ / 1e9 << " B\n";
  os << "------------------------------------------------------------\n";
  os << " Book Value/Share:       " << std::setw(12) << bvps << "\n";
  os << " Free Cash Flow:         " << std::setw(12) << fcf / 1e9 << " B\n";
  os << " Enterprise Value:       " << std::setw(12) << ev / 1e9 << " B\n";
  os << "------------------------------------------------------------\n";
  os << " P/E Ratio:              " << std::setw(12) << pe << "\n";
  os << " P/B Ratio:              " << std::setw(12) << pb << "\n";
  os << " EV/EBITDA:              " << std::setw(12) << evEbitda << "\n";
  os << "------------------------------------------------------------\n";
  os << " Intrinsic Value (DCF):  " << std::setw(12) << intrinsic << " " << currency_ << "\n";
  os << " Undervaluation:         " << std::setw(11) << (underval >= 0 ? "+" : "") << underval
     << " %\n";
  os << "------------------------------------------------------------\n";

  return os.str();
}

} // namespace stock
