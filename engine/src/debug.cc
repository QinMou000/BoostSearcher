#include "searcher.hpp"

const std::string raw = "./data/raw_html/raw.txt";

int main() {
    ns_searcher::Searcher search;
    search.InitSearcher(raw);
    std::string json_string, query;
    while (std::cout << "please enter query: " && std::getline(std::cin, query)) {
        search.Search(query, &json_string);
        std::cout << json_string << std::endl;
        json_string.clear();
    }
    return 0;
}
