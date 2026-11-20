#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

struct Entry {
    std::size_t doc_id;
    std::size_t count;

    bool operator==(const Entry& other) const {
        return doc_id == other.doc_id && count == other.count;
    }
};

struct RelativeIndex {
    std::size_t doc_id;
    float rank;

    bool operator==(const RelativeIndex& other) const {
        return doc_id == other.doc_id && rank == other.rank;
    }
};

class ConverterJSON {
public:
    explicit ConverterJSON(std::filesystem::path directory = ".");

    const std::string& GetEngineName() const;
    std::vector<std::string> GetTextDocuments() const;
    int GetResponsesLimit() const;
    std::vector<std::string> GetRequests() const;
    void putAnswers(const std::vector<std::vector<std::pair<int, float>>>& answers) const;

private:
    std::filesystem::path directory_;
    std::string name_;
    int max_responses_ = 5;
    std::vector<std::string> files_;
};

class InvertedIndex {
public:
    void UpdateDocumentBase(std::vector<std::string> input_docs);
    std::vector<Entry> GetWordCount(const std::string& word) const;

private:
    std::vector<std::string> docs_;
    std::map<std::string, std::vector<Entry>> freq_dictionary_;
};

class SearchServer {
public:
    explicit SearchServer(InvertedIndex& index, std::size_t max_responses = 5);

    std::vector<std::vector<RelativeIndex>> search(
        const std::vector<std::string>& queries_input);

    std::size_t GetTotalResults(std::size_t request_index) const;

private:
    InvertedIndex& index_;
    std::size_t max_responses_;
    std::vector<std::size_t> total_results_;
};
