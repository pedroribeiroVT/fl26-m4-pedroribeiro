#include "aiws/processing_core.hpp"

#include "aiws/chunker.hpp"
#include "aiws/context_builder.hpp"
#include "aiws/corpus_index.hpp"
#include "aiws/retrieval_engine.hpp"
#include "aiws/text_processor.hpp"

#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace aiws {

struct ProcessingCore::Impl {
    std::vector<Chunk> chunks;
    CorpusIndex corpus_index;

    std::unique_ptr<ChunkingStrategy> chunking_strategy;
    std::unique_ptr<RetrievalStrategy> retrieval_strategy;
    std::unique_ptr<ContextStrategy> context_strategy;
};

ProcessingCore::ProcessingCore() : impl_(std::make_unique<Impl>()) {
    impl_->chunking_strategy = std::make_unique<Chunker>(ChunkingPolicy{
        kMaxChunkTokens, kChunkOverlap, kParagraphPreferenceWindow});
    impl_->retrieval_strategy = std::make_unique<RetrievalEngine>();
    impl_->context_strategy = std::make_unique<ContextBuilder>();
}

ProcessingCore::ProcessingCore(std::unique_ptr<ChunkingStrategy> chunking_strategy,
                               std::unique_ptr<RetrievalStrategy> retrieval_strategy,
                               std::unique_ptr<ContextStrategy> context_strategy) {
    if (!chunking_strategy) throw std::invalid_argument("chunking strategy must not be null");
    if (!retrieval_strategy) throw std::invalid_argument("retrieval strategy must not be null");
    if (!context_strategy) throw std::invalid_argument("context strategy must not be null");
    impl_ = std::make_unique<Impl>();
    impl_->chunking_strategy = std::move(chunking_strategy);
    impl_->retrieval_strategy = std::move(retrieval_strategy);
    impl_->context_strategy = std::move(context_strategy);
}

ProcessingCore::~ProcessingCore() = default;
ProcessingCore::ProcessingCore(ProcessingCore&&) noexcept = default;
ProcessingCore& ProcessingCore::operator=(ProcessingCore&&) noexcept = default;

std::string ProcessingCore::normalize(const std::string& text) {
    return TextProcessor::normalize(text);
}

void ProcessingCore::rebuild(const Workspace& workspace) {
    std::unordered_set<std::string> seen_document_ids;
    std::vector<Chunk> pending_chunks;
    const std::size_t document_total = workspace.documents().size();
    for (std::size_t document_order = 0; document_order < document_total; ++document_order) {
        const auto& document = workspace.documents()[document_order];
        if (!seen_document_ids.insert(document.id()).second) {
            throw std::invalid_argument("duplicate document id: " + document.id());
        }
        auto document_chunks = impl_->chunking_strategy->chunk(document, document_order);
        pending_chunks.insert(pending_chunks.end(),
                              std::make_move_iterator(document_chunks.begin()),
                              std::make_move_iterator(document_chunks.end()));
    }
    CorpusIndex pending_corpus_index(pending_chunks);
    impl_->chunks = std::move(pending_chunks);
    impl_->corpus_index = std::move(pending_corpus_index);
}

const std::vector<Chunk>& ProcessingCore::chunks() const noexcept { return impl_->chunks; }
std::size_t ProcessingCore::chunk_count() const noexcept { return impl_->chunks.size(); }

std::size_t ProcessingCore::document_frequency(const std::string& term) const {
    const auto terms = TextProcessor::terms(term);
    if (terms.empty()) return 0;
    if (terms.size() != 1) throw std::invalid_argument("term must normalize to one token");
    return impl_->corpus_index.document_frequency(terms.front());
}

std::size_t ProcessingCore::term_frequency(const std::string& term,
                                           const std::string& chunk_id) const {
    const auto terms = TextProcessor::terms(term);
    if (terms.empty()) return 0;
    if (terms.size() != 1) throw std::invalid_argument("term must normalize to one token");
    return impl_->corpus_index.term_frequency(terms.front(), chunk_id);
}

std::vector<SearchResult> ProcessingCore::search(const std::string& query, int k) const {
    return impl_->retrieval_strategy->search(query, k, impl_->chunks, impl_->corpus_index);
}

std::vector<ContextItem> ProcessingCore::build_context(const std::string& query,
                                                       int k,
                                                       std::size_t token_budget) const {
    if (k < 0) throw std::invalid_argument("k must be non-negative");
    if (token_budget == 0) return {};
    return impl_->context_strategy->build(search(query, k), token_budget);
}

}  // namespace aiws
