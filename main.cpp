#include "SearchEngine.h"

#include <exception>
#include <iostream>
#include <utility>
#include <vector>

int main() {
    try {
        ConverterJSON converter;
        std::cout << "Starting " << converter.GetEngineName() << '\n';

        InvertedIndex index;
        index.UpdateDocumentBase(converter.GetTextDocuments());

        const auto requests = converter.GetRequests();
        SearchServer server(index, converter.GetResponsesLimit());
        const auto results = server.search(requests);

        std::vector<std::vector<std::pair<int, float>>> answers;
        answers.reserve(results.size());
        for (std::size_t i = 0; i < results.size(); ++i) {
            auto& row = answers.emplace_back();
            for (const RelativeIndex& item : results[i]) {
                row.emplace_back(static_cast<int>(item.doc_id), item.rank);
            }
            std::cout << "Request " << i + 1 << ": found "
                      << server.GetTotalResults(i) << ", shown " << row.size() << '\n';
        }
        converter.putAnswers(answers);
        std::cout << "Results saved to answers.json\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
