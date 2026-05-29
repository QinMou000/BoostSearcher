# Research: Fuzzy Search Technologies for Boost-Searcher

- **Query**: Fuzzy search technologies suitable for a C++ document search engine with Chinese text support (cppjieba)
- **Scope**: external / mixed
- **Date**: 2025-05-29

## Current Architecture Summary

Boost-Searcher is a C++14 local Boost documentation search engine with:
- **In-memory inverted index**: `unordered_map<string, InvertedList>` in `/Users/weizhao/boost-searcher/index.hpp`
- **Exact keyword matching only**: `GetInvertedList(word)` does a hash lookup -- no tolerance
- **Chinese tokenization**: cppjieba `CutForSearch` mode in `/Users/weizhao/boost-searcher/util.hpp`
- **Simple TF weighting**: title freq * 10 + content freq * 1
- **No fuzzy search**: misspelled or variant queries return zero results

## Findings

### 1. Open-Source Search Engines with Fuzzy Search

| Engine | Language | Fuzzy Support | Chinese Support | Embeddable | License | Notes |
|---|---|---|---|---|---|---|
| **Elasticsearch** | Java (Lucene) | Excellent (edit distance, phonetic, n-gram) | ICU analyzer + IK/jieba plugin | No (standalone server) | SSPL/Elastic 2.0 | Heavy resource usage, JVM required |
| **Meilisearch** | Rust | Good (typo tolerance built-in, configurable distance) | Basic (segmentatizer crate, not as good as jieba) | No (standalone server, but has C SDK) | MIT | Simple setup, fast, good default fuzzy matching |
| **Tantivy** | Rust | Good (Levenshtein automata, prefix search) | Via custom tokenizer (tantivy-jieba crate exists) | Yes (FFI from C/C++) | MIT | Lucene-inspired, lightweight, best for embedding |
| **Manticore Search** | C++ | Good (extended query syntax, CALL SNIPPETS) | Built-in Chinese support (cjk charset table) | No (standalone server) | GPL-2.0 | Fork of Sphinx, native C++ project |
| **Typesense** | C++ | Good (typo tolerance, prefix search) | Limited (basic CJK tokenization) | No (standalone server) | GPL-3.0 | Purpose-built for instant search, typo tolerance is a core feature |
| **SQLite FTS5** | C | Limited (simple prefix matching via `*`, no edit-distance fuzzy) | Via external tokenizer (e.g., ICU or jieba) | Yes (single-file embed) | Public domain | Very lightweight, but fuzzy is minimal |
| **Bleve** | Go | Good (Levenshtein, regexp, wildcard) | Via custom analyzer | Via CGo bridge | Apache-2.0 | Lucene-inspired, Go-native |

### 2. Fuzzy Matching Algorithms

#### Levenshtein Distance (Edit Distance)
- Measures minimum single-character edits (insert, delete, substitute) to transform one string to another
- **Complexity**: O(m*n) naive; can be optimized with Bitap or Levenshtein automata
- **Best for**: Typo tolerance in European languages (small alphabet)
- **For Chinese**: Less useful -- one wrong character in Chinese is a completely different character, and edit distance doesn't capture visual or phonetic similarity
- **Used by**: Elasticsearch, Lucene, Tantivy, Meilisearch

#### N-gram (Character-level)
- Breaks text into overlapping character sequences (bigrams, trigrams)
- Example: "搜索引擎" -> "搜索", "索引", "引擎"
- **Best for**: CJK languages, partial matching, fuzzy recall
- **For Chinese**: Very effective because Chinese doesn't use spaces; bigrams capture natural word boundaries
- **Used by**: Elasticsearch n-gram tokenizer, Lucene NGramFilter

#### BK-trees (Burkhard-Keller Trees)
- Tree data structure optimized for metric space searches (edit distance)
- Sub-linear search time for approximate matching
- **Best for**: In-memory dictionary lookups with edit-distance threshold
- **For Chinese**: Can work but requires meaningful distance metric for CJK characters

#### Phonetic Algorithms
- Soundex, Metaphone, Double Metaphone
- **For Chinese**: Not directly applicable; instead use **pinyin-based matching**
- Chinese pinyin conversion: convert characters to romanized pinyin, then do fuzzy matching on pinyin
- Example: "搜索" -> "sou suo", user types "sou suo" or "so shuo" -> fuzzy match
- Libraries: `libpinyin`, `pypinyin`, `pinyin4cpp`

#### Symspell / Symmetric Delete
- Pre-computes all strings within edit distance k by deleting characters
- O(1) lookup time, very fast
- **Best for**: Large dictionaries with European languages
- **For Chinese**: Less natural due to character structure

#### Trigram Similarity (PostgreSQL-style)
- Computes trigram overlap between two strings
- **For Chinese**: Bigrams (2-grams) are more appropriate for CJK text

### 3. Adding Fuzzy Search to a C++ Project: Options

#### Option A: Embed Tantivy (via C FFI)
- **Pros**: Full-featured Lucene-like engine, Levenshtein automata, supports custom tokenizers (tantivy-jieba), good Rust-C FFI
- **Cons**: Requires Rust toolchain for building, FFI complexity, substantial dependency
- **Effort**: Medium-High
- **Best when**: You want a production-grade embedded search engine

#### Option B: Embed SQLite FTS5 + Custom Tokenizer
- **Pros**: Single-file embed (sqlite3.c + sqlite3.h), battle-tested, existing jieba tokenizer bindings, minimal dependency
- **Cons**: FTS5 fuzzy is limited (only prefix `*` matching by default; needs extension for true edit-distance fuzzy)
- **Extensions**: `fts5` + custom token filter for n-gram or pinyin
- **Effort**: Low-Medium
- **Best when**: You want minimal dependency and can accept limited fuzzy

#### Option C: Add N-gram + Fuzzy Matching to Current In-Memory Index
- **Pros**: No new dependencies, keeps the current simple architecture, full control
- **Cons**: Need to implement algorithm yourself, potential performance issues at scale
- **Sub-options**:
  - C1: Build a **bigram index** alongside the existing inverted index. On query, expand to bigrams, do set-intersection scoring
  - C2: Implement **edit-distance fuzzy lookup** with a BK-tree or linear scan of index keys
  - C3: Add **pinyin-based fallback** layer: convert query to pinyin, match against pinyin-annotated index
- **Effort**: Medium
- **Best when**: You want to keep the project simple and self-contained

#### Option D: Standalone Engine (Meilisearch / Typesense / Manticore)
- **Pros**: Rich feature set out of the box, good UI/Dashboard, production-ready
- **Cons**: Need to run a separate server process, resource overhead, less integration with existing cppjieba pipeline
- **Effort**: Low (to set up), Medium (to integrate)
- **Best when**: Fuzzy search is just one of many features needed

### 4. Chinese Fuzzy Search: Specific Considerations

Chinese fuzzy search is fundamentally different from English:

**Why English fuzzy search techniques don't directly apply:**
- Edit distance assumes small alphabet (26 letters). Chinese has thousands of characters.
- Typo patterns differ: Chinese input errors come from IME (Input Method Editor) mistakes -- selecting wrong character with same/similar pinyin
- No whitespace word boundaries

**Effective Chinese fuzzy strategies:**

1. **Pinyin-based fuzzy matching**
   - Convert all indexed terms to pinyin at index time
   - Convert query to pinyin at search time
   - Match pinyin strings with edit distance (much smaller alphabet, 26 letters)
   - Handles the most common Chinese input error: wrong character with same pronunciation
   - Example: user types "搜索" (sou suo) but intended "搜锁" (sou suo) -- same pinyin, different characters

2. **Bigram/N-gram indexing**
   - Index bigrams of Chinese text
   - Query bigrams give partial match capability
   - Naturally handles "fuzzy" by allowing partial overlap
   - Example: "搜索引擎" -> bigrams: "搜索", "索引", "引擎"; query "索引" matches

3. **Synonym/variant dictionaries**
   - Common character confusions: 同音字 (homophones), 形近字 (visually similar)
   - Maintain a dictionary mapping characters to their common variants
   - Expand queries to include variants

4. **Fuzzy pinyin matching**
   - Handle common pinyin input errors: z/zh, c/ch, s/sh, n/ng, an/ang, en/eng, in/ing
   - These are the most frequent Chinese input mistakes

### 5. Simplest Approach for a Local Document Search Engine

For Boost-Searcher specifically, the **simplest effective approach** is:

#### Recommended: Bigram Index + Pinyin Fallback (Option C variant)

**Phase 1: Bigram augmentation (minimal effort, immediate value)**
- During index building, also index bigrams extracted by cppjieba (jieba already produces bigram-level tokens in CutForSearch mode)
- At search time, if exact word lookup fails, fall back to bigram lookup
- This gives partial matching with zero new dependencies

**Phase 2: Pinyin fallback (moderate effort, high value)**
- Add a lightweight pinyin conversion step (header-only library or small function)
- Index pinyin alongside Chinese terms
- At search time, if both exact and bigram lookups yield few results, try pinyin-based matching
- Handle fuzzy pinyin (z/zh, c/ch, s/sh confusion)

**Phase 3 (optional): Edit-distance on term keys**
- For the inverted index keys (which are Chinese words, typically 2-4 characters)
- Implement Levenshtein distance with max distance 1
- Only practical because the key space is manageable (Chinese word dictionary, not free-form text)

#### Why this is the best fit:
- **Zero new dependencies**: cppjieba already does bigram segmentation
- **In-memory**: Keeps the current fast architecture
- **C++ native**: No FFI or inter-process communication
- **Incremental**: Can be added layer by layer without breaking existing search
- **Appropriate for the scale**: Boost documentation is thousands of documents, not millions -- in-memory approaches work fine

### Related Source Files

| File Path | Description |
|---|---|
| `/Users/weizhao/boost-searcher/index.hpp` | Inverted index -- where fuzzy matching would be added |
| `/Users/weizhao/boost-searcher/searcher.hpp` | Search logic -- where fuzzy fallback would be integrated |
| `/Users/weizhao/boost-searcher/util.hpp` | cppjieba wrapper -- where tokenization changes would go |
| `/Users/weizhao/boost-searcher/parser.cc` | HTML parser -- where pre-processing changes would be |

### Key Technical Details from Current Code

**Current inverted index lookup** (`index.hpp:61-68`):
```cpp
InvertedList *GetInvertedList(std::string &word) {
    auto ret = inverted_index.find(word);
    if (ret == inverted_index.end()) {
        return nullptr;
    }
    return &ret->second;
}
```
This is a strict `unordered_map::find` -- no tolerance whatsoever.

**Current search flow** (`searcher.hpp:34-98`):
1. Tokenize query with jieba `CutForSearch`
2. For each token, do exact `GetInvertedList` lookup
3. Merge results by doc_id, sum weights
4. Sort and return top-100

**Integration point for fuzzy**: Insert fallback logic between steps 2 and 3 -- when `GetInvertedList` returns nullptr, try bigram expansion and/or pinyin matching.

## Caveats / Not Found

- No web search tools were available for this research; findings are based on domain knowledge
- Quantitative benchmarks (throughput, latency) for each engine were not fetched from external sources
- The specific `tantivy-jieba` crate API was not verified against its latest version
- Pinyin conversion C++ library availability and quality was not verified via package search
- The exact size of the Boost documentation corpus in this project was not measured (affects feasibility of in-memory approaches)
