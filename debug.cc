/*
 * @Author: wang-qin928 2830862261@qq.com
 * @Date: 2025-10-09 21:52:01
 * @LastEditors: wang-qin928 2830862261@qq.com
 * @LastEditTime: 2025-12-23 11:25:57
 * @FilePath: \boost-searcher\debug.cc
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#include "searcher.hpp"

const std::string raw = "./data/raw_html/raw.txt"; // 解析完的内容

int main() {
    ns_searcher::Searcher search;
    search.InitSearcher(raw);
    while (1) {
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