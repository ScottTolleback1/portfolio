#include "search.hh"

#include <iostream>
#include <sqlite3.h>

namespace search
{

static inline uint64_t
char_mask36(const std::string& s)
{
  uint64_t m = 0;
  for (unsigned char c : s) {
    if (c >= 'A' && c <= 'Z')
      m |= (1ull << (c - 'A'));
    else if (c >= '0' && c <= '9')
      m |= (1ull << (26 + (c - '0')));
  }
  return m;
}

Search::Search(const std::string& db_path, int ngram_size, double weight_cosine, double weight_lev)
    : n_(ngram_size), weight_cosine_(weight_cosine), weight_lev_(weight_lev)
{
  loadFromDatabase(db_path);
}

void
Search::loadFromDatabase(const std::string& db_path)
{
  sqlite3* db = nullptr;
  std::cout << "[INIT] Loading tickers from " << db_path << std::endl;

  if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
    std::string msg = "Cannot open DB: " + std::string(sqlite3_errmsg(db));
    sqlite3_close(db);
    throw std::runtime_error(msg);
  }

  const char* sql = "SELECT ticker, company FROM tickers;";
  sqlite3_stmt* stmt = nullptr;

  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    std::string msg = "SQL prepare failed: " + std::string(sqlite3_errmsg(db));
    sqlite3_close(db);
    throw std::runtime_error(msg);
  }

  int count = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const unsigned char* t = sqlite3_column_text(stmt, 0);
    const unsigned char* n = sqlite3_column_text(stmt, 1);
    std::string ticker = t ? reinterpret_cast<const char*>(t) : "";
    std::string name = n ? reinterpret_cast<const char*>(n) : "";
    if (ticker.empty() || name.empty())
      continue;

    std::transform(name.begin(), name.end(), name.begin(), ::toupper);

    entries_.push_back({ticker, name, vectorize(name), char_mask36(name)});
    ++count;
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  std::cout << "[DB] Loaded " << count << " valid entries.\n";
}

std::vector<double>
Search::vectorize(const std::string& s) const
{
  const int vec_size = 256;
  std::vector<double> vec(vec_size, 0.0);

  if (s.size() < static_cast<size_t>(n_))
    return vec;

  for (size_t i = 0; i + n_ <= s.size(); ++i) {
    size_t idx = std::hash<std::string>{}(s.substr(i, n_)) % vec_size;
    vec[idx] += 1.0;
  }

  double length = norm(vec);
  if (length > 0.0)
    for (auto& v : vec)
      v /= length;

  return vec;
}

double
Search::dot_product(const std::vector<double>& a, const std::vector<double>& b) const
{
  double sum = 0.0;
  size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; ++i)
    sum += a[i] * b[i];
  return sum;
}

double
Search::norm(const std::vector<double>& a) const
{
  double sum = 0.0;
  for (double v : a)
    sum += v * v;
  return std::sqrt(sum);
}

double
Search::cosine_similarity(const std::vector<double>& a, const std::vector<double>& b) const
{
  double denom = norm(a) * norm(b);
  return denom == 0.0 ? 0.0 : dot_product(a, b) / denom;
}

int
Search::levenshtein_distance(const std::string& s1, const std::string& s2) const
{
  const size_t m = s1.size();
  const size_t n = s2.size();

  if (m == 0)
    return static_cast<int>(n);
  if (n == 0)
    return static_cast<int>(m);

  std::vector<int> prev(n + 1), curr(n + 1);

  for (size_t j = 0; j <= n; ++j)
    prev[j] = static_cast<int>(j);

  for (size_t i = 1; i <= m; ++i) {
    curr[0] = static_cast<int>(i);

    for (size_t j = 1; j <= n; ++j) {
      int cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
      curr[j] = std::min({prev[j] + 1, curr[j - 1] + 1, prev[j - 1] + cost});
    }
    prev.swap(curr);
  }
  return prev[n];
}

double
Search::hybrid_similarity(const std::string& s1, const std::string& s2,
                          const std::vector<double>& v2) const
{
  auto v1 = vectorize(s1);
  double cos_sim = cosine_similarity(v1, v2);

  int lev_dist = levenshtein_distance(s1, s2);
  double lev_sim = 1.0 - static_cast<double>(lev_dist) / std::max(s1.size(), s2.size());
  if (lev_sim < 0.0)
    lev_sim = 0.0;

  return weight_cosine_ * cos_sim + weight_lev_ * lev_sim;
}

std::pair<std::string, double>
Search::findBestMatch(const std::string& query) const
{
  if (query.empty())
    return {"", 0.0};

  std::string q = query;
  std::transform(q.begin(), q.end(), q.begin(), ::toupper);
  uint64_t qmask = char_mask36(q);

  for (const auto& e : entries_) {
    if (e.ticker == q)
      return {e.ticker, 1.0};
  }

  double wc = weight_cosine_;
  double wl = weight_lev_;
  if (q.size() <= 4) {
    wc *= 0.5;
    wl *= 1.5;
  }

  auto qvec = vectorize(q);
  std::string best_ticker;
  double best_score = -1.0;

  for (const auto& e : entries_) {
    if ((qmask & e.mask) == 0)
      continue;

    double bonus = 0.0;
    if (q.size() <= 6) {
      if (e.name.rfind(q, 0) == 0)
        bonus += 0.2;
      else if (e.name.find(q) != std::string::npos)
        bonus += 0.1;
    }

    double cos_sim = cosine_similarity(qvec, e.vec);
    int lev = levenshtein_distance(q, e.name);
    double lev_sim = 1.0 - double(lev) / std::max(q.size(), e.name.size());
    if (lev_sim < 0.0)
      lev_sim = 0.0;

    double score = wc * cos_sim + wl * lev_sim + bonus;

    if (score > best_score) {
      best_score = score;
      best_ticker = e.ticker;
    }
  }

  if (best_score < 0.333)
    return {"", 0.0};

  if (best_score > 1.0)
    best_score = 1.0;

  return {best_ticker, best_score};
}

} // namespace search
