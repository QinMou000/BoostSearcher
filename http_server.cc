#include "searcher.hpp"
#include "httplib.h"

const std::string raw = "./data/raw_html/raw.txt"; // 解析完的内容
const std::string root_path = "./wwwroot";

int main()
{
    ns_searcher::Searcher searcher;
    searcher.InitSearcher(raw);

    httplib::Server svr;
    svr.set_base_dir(root_path.c_str());

    svr.Get("/s", [&](const httplib::Request &req, httplib::Response &res)
            { 
                if(!req.has_param("word"))
                {
                    res.set_content("请输入搜索关键字 ", "text/plain; charset=utf-8");
                    return ;
                } 
                std::string word = req.get_param_value("word");
                std::cout << "用户搜索:" << word << std::endl;
                std::string json_string;
                searcher.Search(word,&json_string); 
                res.set_content(json_string,"application/json"); });

    svr.listen("0.0.0.0", 8080);
    return 0;
}
