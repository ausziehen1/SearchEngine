#include "SearchEngine.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("search_engine_test_" + std::to_string(stamp));
        std::filesystem::create_directories(path);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }

    std::filesystem::path path;
};

void WriteFile(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream(path) << contents;
}

std::string DocumentWord(std::size_t id) {
    std::string word = "document";
    for (int i = 0; i < 3; ++i) {
        word += static_cast<char>('a' + id % 26);
        id /= 26;
    }
    return word;
}
}

TEST(InvertedIndexTest, CountsWordsByDocument) {
    InvertedIndex index;
    index.UpdateDocumentBase({
        "london is the capital of great britain",
        "big ben is the nickname for the great bell of the striking clock"
    });

    EXPECT_EQ(index.GetWordCount("london"), (std::vector<Entry>{{0, 1}}));
    EXPECT_EQ(index.GetWordCount("the"), (std::vector<Entry>{{0, 1}, {1, 3}}));
    EXPECT_TRUE(index.GetWordCount("missing").empty());
}

TEST(InvertedIndexTest, RebuildClearsOldEntries) {
    InvertedIndex index;
    index.UpdateDocumentBase({"milk milk", "water"});
    EXPECT_EQ(index.GetWordCount("milk"), (std::vector<Entry>{{0, 2}}));

    index.UpdateDocumentBase({"tea tea tea", "tea"});
    EXPECT_TRUE(index.GetWordCount("milk").empty());
    EXPECT_EQ(index.GetWordCount("tea"), (std::vector<Entry>{{0, 3}, {1, 1}}));
}

TEST(InvertedIndexTest, ManyDocumentsKeepStableIds) {
    InvertedIndex index;
    std::vector<std::string> docs(200, "water water");
    docs[80] = "milk";
    docs[199] = "water milk";
    index.UpdateDocumentBase(docs);

    EXPECT_EQ(index.GetWordCount("milk"), (std::vector<Entry>{{80, 1}, {199, 1}}));
    EXPECT_EQ(index.GetWordCount("water").size(), 199u);
}

TEST(SearchServerTest, RelevanceAndMissingTerm) {
    InvertedIndex index;
    index.UpdateDocumentBase({
        "milk milk milk milk water water water",
        "milk water water",
        "milk milk milk milk milk water water water water water",
        "americano cappuccino"
    });

    SearchServer server(index);
    const auto result = server.search({"milk water", "sugar"});
    const std::vector<RelativeIndex> expected = {{2, 1.0f}, {0, 0.7f}, {1, 0.3f}};
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], expected);
    EXPECT_TRUE(result[1].empty());
    EXPECT_EQ(server.GetTotalResults(0), 3u);
    EXPECT_EQ(server.GetTotalResults(1), 0u);
}

TEST(SearchServerTest, TopFiveUsesPartialMatches) {
    InvertedIndex index;
    index.UpdateDocumentBase({
        "london is the capital of great britain",
        "paris is the capital of france",
        "berlin is the capital of germany",
        "rome is the capital of italy",
        "madrid is the capital of spain",
        "lisboa is the capital of portugal",
        "bern is the capital of switzerland",
        "moscow is the capital of russia",
        "kiev is the capital of ukraine",
        "minsk is the capital of belarus",
        "astana is the capital of kazakhstan",
        "beijing is the capital of china",
        "tokyo is the capital of japan",
        "bangkok is the capital of thailand",
        "welcome to moscow the capital of russia the third rome",
        "amsterdam is the capital of netherlands",
        "helsinki is the capital of finland",
        "oslo is the capital of norway",
        "stockholm is the capital of sweden",
        "riga is the capital of latvia",
        "tallinn is the capital of estonia",
        "warsaw is the capital of poland"
    });
    SearchServer server(index, 5);
    const auto results = server.search({"moscow is the capital of russia"});

    ASSERT_EQ(results.size(), 1u);
    ASSERT_EQ(results[0].size(), 5u);
    EXPECT_EQ(results[0][0], (RelativeIndex{7, 1.0f}));
    EXPECT_EQ(results[0][1], (RelativeIndex{14, 1.0f}));
    for (std::size_t i = 2; i < 5; ++i) {
        EXPECT_EQ(results[0][i].doc_id, i - 2);
        EXPECT_FLOAT_EQ(results[0][i].rank, 4.0f / 6.0f);
    }
    EXPECT_EQ(server.GetTotalResults(0), 22u);
}

TEST(SearchServerTest, RepeatedQueryWordsAreCountedOnce) {
    InvertedIndex index;
    index.UpdateDocumentBase({"milk water", "milk milk", "water"});
    SearchServer server(index, 2);
    const auto results = server.search({"milk milk water"});
    EXPECT_EQ(results[0], (std::vector<RelativeIndex>{{0, 1.0f}, {1, 1.0f}}));
    EXPECT_EQ(server.GetTotalResults(0), 3u);
}

TEST(SearchServerTest, EmptyQueryAndNoDocuments) {
    InvertedIndex index;
    index.UpdateDocumentBase({});
    SearchServer server(index);
    const auto results = server.search({"milk", "   "});
    EXPECT_TRUE(results[0].empty());
    EXPECT_TRUE(results[1].empty());
}

TEST(ConverterJSONTest, ReadsAndWritesSpecifiedFormat) {
    TemporaryDirectory temp;
    WriteFile(temp.path / "config.json", R"({
        "config": {"name": "MySearch", "version": "0.1", "max_responses": 2},
        "files": ["one.txt", "missing.txt", "two.txt"]
    })");
    WriteFile(temp.path / "one.txt", "milk water");
    WriteFile(temp.path / "two.txt", "water water");
    WriteFile(temp.path / "requests.json", R"({"requests":["water", "missing"]})");

    ConverterJSON converter(temp.path);
    EXPECT_EQ(converter.GetEngineName(), "MySearch");
    EXPECT_EQ(converter.GetResponsesLimit(), 2);
    EXPECT_EQ(converter.GetTextDocuments(),
              (std::vector<std::string>{"milk water", "", "water water"}));
    EXPECT_EQ(converter.GetRequests(),
              (std::vector<std::string>{"water", "missing"}));

    converter.putAnswers({{{2, 1.0f}, {0, 0.5f}}, {}});
    std::ifstream answer_file(temp.path / "answers.json");
    const auto answer = nlohmann::json::parse(answer_file);
    EXPECT_EQ(answer["answers"]["request001"]["result"], "true");
    EXPECT_EQ(answer["answers"]["request001"]["relevance"][0]["docid"], 2);
    EXPECT_EQ(answer["answers"]["request001"]["relevance"][0]["rank"], 1.0f);
    EXPECT_EQ(answer["answers"]["request002"]["result"], "false");
    EXPECT_FALSE(answer["answers"]["request002"].contains("relevance"));
}

TEST(ConverterJSONTest, DetectsMissingAndWrongConfiguration) {
    TemporaryDirectory temp;
    EXPECT_THROW((void)ConverterJSON(temp.path), std::runtime_error);

    WriteFile(temp.path / "config.json", "{}");
    EXPECT_THROW((void)ConverterJSON(temp.path), std::runtime_error);

    WriteFile(temp.path / "config.json", R"({
        "config": {"name": "MySearch", "version": "9.9"}, "files": []
    })");
    EXPECT_THROW((void)ConverterJSON(temp.path), std::runtime_error);

    WriteFile(temp.path / "config.json", R"({
        "config": {"name": "MySearch", "version": "0.1"}, "files": []
    })");
    ConverterJSON converter(temp.path);
    EXPECT_EQ(converter.GetResponsesLimit(), 5);
}

TEST(SearchServerTest, ThousandDocumentsAndThousandQueries) {
    std::string common_words;
    for (int i = 0; i < 999; ++i) {
        common_words += "common ";
    }

    std::vector<std::string> documents;
    std::vector<std::string> queries;
    documents.reserve(1000);
    queries.reserve(1000);
    for (std::size_t id = 0; id < 1000; ++id) {
        const std::string word = DocumentWord(id);
        documents.push_back(common_words + word);
        queries.push_back(word);
    }

    const auto start = std::chrono::steady_clock::now();
    InvertedIndex index;
    index.UpdateDocumentBase(std::move(documents));
    const auto indexed = std::chrono::steady_clock::now();

    const auto common = index.GetWordCount("common");
    ASSERT_EQ(common.size(), 1000u);
    EXPECT_EQ(common.front(), (Entry{0, 999}));
    EXPECT_EQ(common.back(), (Entry{999, 999}));
    EXPECT_EQ(index.GetWordCount(DocumentWord(999)), (std::vector<Entry>{{999, 1}}));

    SearchServer server(index, 5);
    const auto results = server.search(queries);
    const auto searched = std::chrono::steady_clock::now();
    ASSERT_EQ(results.size(), 1000u);
    for (std::size_t id = 0; id < results.size(); ++id) {
        SCOPED_TRACE(id);
        ASSERT_EQ(results[id].size(), 1u);
        EXPECT_EQ(results[id][0], (RelativeIndex{id, 1.0f}));
        EXPECT_EQ(server.GetTotalResults(id), 1u);
    }

    const auto popular = server.search({"common"});
    ASSERT_EQ(popular.size(), 1u);
    ASSERT_EQ(popular[0].size(), 5u);
    EXPECT_EQ(server.GetTotalResults(0), 1000u);
    for (std::size_t id = 0; id < 5; ++id) {
        EXPECT_EQ(popular[0][id], (RelativeIndex{id, 1.0f}));
    }

    const auto index_ms = std::chrono::duration_cast<std::chrono::milliseconds>(indexed - start);
    const auto search_ms = std::chrono::duration_cast<std::chrono::milliseconds>(searched - indexed);
    std::cout << "Stress: 1000 documents x 1000 words indexed in " << index_ms.count()
              << " ms, 1000 requests processed in " << search_ms.count() << " ms\n";
}

TEST(ConverterJSONTest, RejectsInvalidJsonAndLimits) {
    TemporaryDirectory temp;
    const auto config = temp.path / "config.json";
    const auto requests = temp.path / "requests.json";

    WriteFile(config, "{invalid json");
    EXPECT_THROW((void)ConverterJSON(temp.path), nlohmann::json::parse_error);

    WriteFile(config, R"({"config":{"name":"Test","version":"0.1","max_responses":0},"files":[]})");
    EXPECT_THROW((void)ConverterJSON(temp.path), std::runtime_error);

    WriteFile(config, R"({"config":{"name":"Test","version":"0.1","max_responses":-1},"files":[]})");
    EXPECT_THROW((void)ConverterJSON(temp.path), std::runtime_error);

    WriteFile(config, R"({"config":{"name":"Test","version":"0.1"},"files":[]})");
    ConverterJSON converter(temp.path);
    EXPECT_THROW((void)converter.GetRequests(), std::runtime_error);

    WriteFile(requests, R"({"requests": [not json]})");
    EXPECT_THROW((void)converter.GetRequests(), nlohmann::json::parse_error);

    WriteFile(requests, R"({"requests": "not an array"})");
    EXPECT_THROW((void)converter.GetRequests(), std::runtime_error);
}

TEST(ConverterJSONTest, ThousandAnswersAndOverwrite) {
    TemporaryDirectory temp;
    WriteFile(temp.path / "config.json",
              R"({"config":{"name":"Test","version":"0.1"},"files":[]})");
    ConverterJSON converter(temp.path);

    std::vector<std::vector<std::pair<int, float>>> answers(1000);
    answers[0] = {{2, 1.0f}};
    answers[999] = {{999, 0.5f}};
    converter.putAnswers(answers);

    {
        std::ifstream file(temp.path / "answers.json");
        const auto output = nlohmann::json::parse(file);
        ASSERT_EQ(output["answers"].size(), 1000u);
        EXPECT_EQ(output["answers"]["request001"]["relevance"][0]["docid"], 2);
        EXPECT_EQ(output["answers"]["request1000"]["relevance"][0]["docid"], 999);
        EXPECT_EQ(output["answers"]["request050"]["result"], "false");
        EXPECT_FALSE(output["answers"]["request050"].contains("relevance"));
    }

    converter.putAnswers({{}});
    std::ifstream file(temp.path / "answers.json");
    const auto output = nlohmann::json::parse(file);
    ASSERT_EQ(output["answers"].size(), 1u);
    EXPECT_EQ(output["answers"]["request001"]["result"], "false");
    EXPECT_FALSE(output["answers"].contains("request1000"));
}
