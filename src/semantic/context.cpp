#include "context.h"

namespace rx::semantic {
SemanticContext::SemanticContext() { reset(); }

void SemanticContext::reset() {
  scopes_.clear();
  symbols_.clear();
  auto globalScope = std::make_unique<Scope>(nullptr);
  globalScope_ = globalScope.get();
  scopes_.push_back(std::move(globalScope));
}

Scope &SemanticContext::globalScope() noexcept { return *globalScope_; }

const Scope &SemanticContext::globalScope() const noexcept {
  return *globalScope_;
}

Scope &SemanticContext::createChildScope(Scope &parent) {
  auto scope = std::make_unique<Scope>(&parent);
  Scope *raw = scope.get();
  scopes_.push_back(std::move(scope));
  return *raw;
}

} // namespace rx::semantic