#pragma once

#include <string>
#include <vector>

namespace search
{

/*
 * search::Search
 * ----------------
 * Provides fuzzy search functionality over ticker and company names
 * stored in the local SQLite ticker database.
 *
 * Responsibilities:
 *  - Load all tickers and company names into memory
 *  - Represent company names as n-gram vector embeddings
 *  - Compute hybrid similarity between user queries and database entries
 *    using both cosine similarity (semantic) and Levenshtein distance (string edit)
 *  - Return the best matching ticker and its confidence score
 *
 * Used by:
 *  - PortfolioWidget (GUI) for matching user input to a valid stock symbol
 */
class Search
{
public:
  Search(const std::string& db_path, int ngram_size = 3, double weight_cosine = 0.6,
         double weight_lev = 0.4);

  std::pair<std::string, double> findBestMatch(const std::string& query) const;

private:
  struct Entry {
    std::string ticker;
    std::string name;
    std::vector<double> vec;
    uint64_t mask;
  };
  std::vector<Entry> entries_;

  void loadFromDatabase(const std::string& db_path);
  std::vector<double> vectorize(const std::string& text) const;

  double dot_product(const std::vector<double>& a, const std::vector<double>& b) const;
  double norm(const std::vector<double>& a) const;
  double cosine_similarity(const std::vector<double>& a, const std::vector<double>& b) const;
  int levenshtein_distance(const std::string& s1, const std::string& s2) const;
  double hybrid_similarity(const std::string& s1, const std::string& s2,
                           const std::vector<double>& v2) const;

  int n_;
  double weight_cosine_;
  double weight_lev_;
};

} // namespace search
