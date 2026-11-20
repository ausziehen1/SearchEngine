#include "SearchEngine.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

using json = nlohmann::json;

namespace {
json ReadJson(const std::filesystem::path& path, const char* missing_message) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error(missing_message);
    }
    return json::parse(file);
}
}

ConverterJSON::ConverterJSON(std::filesystem::path directory)
    : directory_(std::move(directory)) {
    const json root = ReadJson(directory_ / "config.json", "config file is missing");
    if (!root.contains("config") || !root["config"].is_object() || root["config"].empty()) {
        throw std::runtime_error("config file is empty");
    }

    const json& config = root["config"];
    name_ = config.value("name", std::string("SearchEngine"));
    if (config.value("version", std::string()) != "0.1") {
        throw std::runtime_error("config.json has incorrect file version");
    }
    max_responses_ = config.value("max_responses", 5);
    if (max_responses_ <= 0) {
        throw std::runtime_error("max_responses must be positive");
    }
    if (!root.contains("files") || !root["files"].is_array()) {
        throw std::runtime_error("config.json must contain a files array");
    }
    files_ = root["files"].get<std::vector<std::string>>();
}

const std::string& ConverterJSON::GetEngineName() const {
    return name_;
}

std::vector<std::string> ConverterJSON::GetTextDocuments() const {
    std::vector<std::string> documents;
    documents.reserve(files_.size());

    for (const std::string& path : files_) {
        std::ifstream file(directory_ / path);
        if (!file) {
            std::cerr << "Cannot open document: " << path << '\n';
            documents.emplace_back();
            continue;
        }
        std::ostringstream content;
        content << file.rdbuf();
        documents.push_back(content.str());
    }
    return documents;
}

int ConverterJSON::GetResponsesLimit() const {
    return max_responses_;
}

std::vector<std::string> ConverterJSON::GetRequests() const {
    const json root = ReadJson(directory_ / "requests.json", "requests file is missing");
    if (!root.contains("requests") || !root["requests"].is_array()) {
        throw std::runtime_error("requests.json must contain a requests array");
    }
    return root["requests"].get<std::vector<std::string>>();
}

void ConverterJSON::putAnswers(
    const std::vector<std::vector<std::pair<int, float>>>& answers) const {
    json root;
    root["answers"] = json::object();

    for (std::size_t i = 0; i < answers.size(); ++i) {
        std::ostringstream request_id;
        request_id << "request" << std::setfill('0') << std::setw(3) << i + 1;

        json answer;
        answer["result"] = answers[i].empty() ? "false" : "true";
        if (!answers[i].empty()) {
            answer["relevance"] = json::array();
            for (const auto& [doc_id, rank] : answers[i]) {
                answer["relevance"].push_back({{"docid", doc_id}, {"rank", rank}});
            }
        }
        root["answers"][request_id.str()] = std::move(answer);
    }

    std::ofstream output(directory_ / "answers.json", std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot write answers.json");
    }
    output << root.dump(4) << '\n';
    if (!output) {
        throw std::runtime_error("failed to write answers.json");
    }
}

void InvertedIndex::UpdateDocumentBase(std::vector<std::string> input_docs) {
    docs_ = std::move(input_docs);
    freq_dictionary_.clear();
    if (docs_.empty()) {
        return;
    }

    std::vector<std::map<std::string, std::size_t>> frequencies(docs_.size());
    std::atomic<std::size_t> next_document{0};
    const unsigned hardware_threads = std::max(1u, std::thread::hardware_concurrency());
    const std::size_t worker_count = std::min<std::size_t>(docs_.size(), hardware_threads);
    std::vector<std::thread> workers;
    workers.reserve(worker_count);

    for (std::size_t worker = 0; worker < worker_count; ++worker) {
        workers.emplace_back([&] {
            while (true) {
                const std::size_t doc_id = next_document.fetch_add(1);
                if (doc_id >= docs_.size()) {
                    break;
                }
                std::istringstream text(docs_[doc_id]);
                std::string word;
                while (text >> word) {
                    ++frequencies[doc_id][word];
                }
            }
        });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }

    for (std::size_t doc_id = 0; doc_id < frequencies.size(); ++doc_id) {
        for (const auto& [word, count] : frequencies[doc_id]) {
            freq_dictionary_[word].push_back({doc_id, count});
        }
    }
}

std::vector<Entry> InvertedIndex::GetWordCount(const std::string& word) const {
    const auto it = freq_dictionary_.find(word);
    return it == freq_dictionary_.end() ? std::vector<Entry>{} : it->second;
}

SearchServer::SearchServer(InvertedIndex& index, std::size_t max_responses)
    : index_(index), max_responses_(max_responses) {}

std::vector<std::vector<RelativeIndex>> SearchServer::search(
    const std::vector<std::string>& queries_input) {
    total_results_.clear();
    std::vector<std::vector<RelativeIndex>> responses;
    responses.reserve(queries_input.size());

    for (const std::string& query : queries_input) {
        std::istringstream stream(query);
        std::set<std::string> unique_words;
        std::string word;
        while (stream >> word) {
            unique_words.insert(word);
        }

        std::vector<std::pair<std::size_t, std::vector<Entry>>> words;
        for (const std::string& term : unique_words) {
            auto entries = index_.GetWordCount(term);
            std::size_t frequency = 0;
            for (const Entry& entry : entries) {
                frequency += entry.count;
            }
            words.emplace_back(frequency, std::move(entries));
        }
        std::stable_sort(words.begin(), words.end(), [](const auto& left, const auto& right) {
            return left.first < right.first;
        });

        std::map<std::size_t, std::size_t> scores;
        for (const auto& item : words) {
            for (const Entry& entry : item.second) {
                scores[entry.doc_id] += entry.count;
            }
        }
        total_results_.push_back(scores.size());

        std::vector<std::pair<std::size_t, std::size_t>> sorted(scores.begin(), scores.end());
        std::sort(sorted.begin(), sorted.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.second != rhs.second ? lhs.second > rhs.second
                                            : lhs.first < rhs.first;
        });

        std::vector<RelativeIndex> result;
        if (!sorted.empty()) {
            const float maximum = static_cast<float>(sorted.front().second);
            const std::size_t count = std::min(max_responses_, sorted.size());
            result.reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
                result.push_back({sorted[i].first, static_cast<float>(sorted[i].second) / maximum});
            }
        }
        responses.push_back(std::move(result));
    }
    return responses;
}

std::size_t SearchServer::GetTotalResults(std::size_t request_index) const {
    return total_results_.at(request_index);
}
