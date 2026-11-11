#pragma once
#include "scope.h"
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace rx::semantic {
class SemanticContext {
private:
  std::vector<std::unique_ptr<Symbol>> symbols_;
  std::vector<std::unique_ptr<Scope>> scopes_;
  Scope *globalScope_ = nullptr;

public:
  SemanticContext();
  void reset();
  ~SemanticContext() = default;
  SemanticContext(const SemanticContext &) = delete;
  SemanticContext &operator=(const SemanticContext &) = delete;
  SemanticContext(SemanticContext &&) = delete;
  SemanticContext &operator=(SemanticContext &&) = delete;

  Scope &globalScope() noexcept;
  const Scope &globalScope() const noexcept;
  Scope &createChildScope(Scope &parent);

  template <typename T, typename... Args> T *makeSymbol(Args &&...args) {
    static_assert(std::is_base_of_v<Symbol, T>,
                  "T must derive from rx::semantic::Symbol");
    auto symbol = std::make_unique<T>(std::forward<Args>(args)...);
    T *raw = symbol.get();
    symbols_.push_back(std::move(symbol));
    return raw;
  }
};

} // namespace rx::semantic
