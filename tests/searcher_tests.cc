#include "searcher.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

namespace {

const char *kRawPath = "searcher_test_raw.txt";

std::atomic<int> g_failures{0};

void Check(bool condition, const std::string &message) {
    if (!condition) {
        ++g_failures;
        std::cerr << "[FAIL] " << message << std::endl;
        return;
    }
    std::cout << "[PASS] " << message << std::endl;
}

void WriteTestRawFile() {
    const std::string sep = "\3";
    std::ofstream out(kRawPath, std::ios::out | std::ios::binary);

    out << "网络协议基础" << sep
        << "网络协议负责网络层通信，协议字段用于说明数据传输规则。"
        << sep << "data/raw/md/network-protocol.md" << '\n';
    out << "C语言计算器" << sep
        << "按位或 按位异或 函数指针 递归。" << sep
        << "data/raw/md/c-calculator.md" << '\n';
    out << "单片机LED点阵" << sep << "LED点阵显示动画。" << sep
        << "data/raw/md/led.md" << '\n';
    // 这篇文档故意只在正文中重复查询词，用来验证 BM25 的字段权重和词频饱和：
    // 标题命中文档不应该被正文堆词文档轻易压过。
    out << "正文堆词材料" << sep
        << "网络协议 网络协议 网络协议 网络协议 网络协议 网络协议。"
        << sep << "data/raw/md/body-network.md" << '\n';
}

nlohmann::json SearchJson(ns_searcher::Searcher &searcher,
                          const std::string &query) {
    std::string json_text;
    searcher.Search(query, &json_text);
    if (json_text.empty()) {
        return nlohmann::json::array();
    }
    return nlohmann::json::parse(json_text);
}

bool ContainsTitle(const nlohmann::json &results, const std::string &title) {
    for (const auto &item : results) {
        if (item.value("title", "") == title) {
            return true;
        }
    }
    return false;
}

bool AnyUrlStartsWithData(const nlohmann::json &results) {
    for (const auto &item : results) {
        if (item.value("url", "").rfind("data/", 0) == 0) {
            return true;
        }
    }
    return false;
}

void TestExactSearch(ns_searcher::Searcher &searcher) {
    nlohmann::json results = SearchJson(searcher, "网络协议");

    Check(!results.empty(), "精确搜索应返回结果");
    Check(ContainsTitle(results, "网络协议基础"), "精确搜索应命中目标文档");
    Check(AnyUrlStartsWithData(results), "搜索结果 url 应以 data 为根目录");
}

void TestFuzzySearch(ns_searcher::Searcher &searcher) {
    nlohmann::json results = SearchJson(searcher, "网络协义");

    Check(!results.empty(), "错字查询应触发模糊召回");
    Check(ContainsTitle(results, "网络协议基础"),
          "模糊搜索应把“协义”召回到“协议”");
}

void TestSingleCharDoesNotFuzzyMatch(ns_searcher::Searcher &searcher) {
    nlohmann::json results = SearchJson(searcher, "义");

    Check(results.empty(), "单字查询不应进行模糊召回");
}

void TestBm25TitlePriority(ns_searcher::Searcher &searcher) {
    nlohmann::json results = SearchJson(searcher, "网络协议");

    Check(!results.empty(), "BM25 排序测试应返回结果");
    // 若排序退回固定词频累加，正文重复多次的文档可能排到前面；这里锁住标题优先策略。
    Check(results[0].value("title", "") == "网络协议基础",
          "BM25 标题命中应优先于正文堆词文档");
}

void TestConcurrentSearch(ns_searcher::Searcher &searcher) {
    constexpr int kThreadCount = 8;
    constexpr int kIterationsPerThread = 50;

    std::atomic<int> thread_failures{0};
    std::atomic<int> total_queries{0};
    std::atomic<long long> total_latency_ns{0};
    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);

    auto benchmark_start = std::chrono::steady_clock::now();
    for (int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([&searcher, &thread_failures,
                              &total_queries, &total_latency_ns,
                              kIterationsPerThread]() {
            try {
                auto measured_search = [&](const std::string &query) {
                    auto start = std::chrono::steady_clock::now();
                    nlohmann::json results = SearchJson(searcher, query);
                    auto end = std::chrono::steady_clock::now();
                    auto latency =
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            end - start)
                            .count();
                    total_latency_ns.fetch_add(latency,
                                               std::memory_order_relaxed);
                    total_queries.fetch_add(1, std::memory_order_relaxed);
                    return results;
                };

                for (int j = 0; j < kIterationsPerThread; ++j) {
                    nlohmann::json exact = measured_search("网络协议");
                    if (!ContainsTitle(exact, "网络协议基础")) {
                        ++thread_failures;
                    }

                    nlohmann::json fuzzy = measured_search("网络协义");
                    if (!ContainsTitle(fuzzy, "网络协议基础")) {
                        ++thread_failures;
                    }

                    nlohmann::json single_char = measured_search("义");
                    if (!single_char.empty()) {
                        ++thread_failures;
                    }
                }
            } catch (const std::exception &) {
                ++thread_failures;
            } catch (...) {
                ++thread_failures;
            }
        });
    }

    for (std::thread &thread : threads) {
        thread.join();
    }
    auto benchmark_end = std::chrono::steady_clock::now();

    double elapsed_seconds =
        std::chrono::duration<double>(benchmark_end - benchmark_start).count();
    int query_count = total_queries.load(std::memory_order_relaxed);
    long long latency_ns = total_latency_ns.load(std::memory_order_relaxed);
    double qps = elapsed_seconds > 0.0 ? query_count / elapsed_seconds : 0.0;
    double avg_latency_ms =
        query_count > 0 ? latency_ns / 1000000.0 / query_count : 0.0;

    std::cout << std::fixed << std::setprecision(2)
              << "[METRIC] concurrent_search threads=" << kThreadCount
              << " total_queries=" << query_count
              << " elapsed_ms=" << elapsed_seconds * 1000.0
              << " qps=" << qps
              << " avg_latency_ms=" << avg_latency_ms << std::endl;

    Check(thread_failures.load() == 0,
          "并发搜索应保持精确、模糊和无结果查询的行为稳定");
}

} // namespace

int main() {
    WriteTestRawFile();

    ns_searcher::Searcher searcher;
    searcher.InitSearcher(kRawPath);

    TestExactSearch(searcher);
    TestFuzzySearch(searcher);
    TestSingleCharDoesNotFuzzyMatch(searcher);
    TestBm25TitlePriority(searcher);
    TestConcurrentSearch(searcher);

    std::remove(kRawPath);

    if (g_failures.load() != 0) {
        std::cerr << g_failures.load() << " test(s) failed" << std::endl;
        return 1;
    }
    std::cout << "All tests passed" << std::endl;
    return 0;
}
