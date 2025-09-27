#include "searcher.hpp"

const std::string raw = "./data/raw_html/raw.txt"; // 解析完的内容

int main()
{
    ns_searcher::Searcher search;
    search.InitSearcher(raw);
    while (1)
    {
        std::string json_string, query;
        std::cout << "please enter query:";
        std::getline(std::cin, query);
        query[query.size()] = 0;
        search.ns_searcher::Searcher::Search(query, &json_string);
        std::cout << json_string << std::endl;
        std::cout << query << std::endl;
    }
    return 0;
}