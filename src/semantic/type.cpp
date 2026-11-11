#include "type.h"
#include "symbol.h"
#include <utility>

namespace rx::semantic {
namespace {

// &S → &T ：or &mut S → &T
bool canDerefToShared(const TypePtr &source, const TypePtr &target) {
  if (!source || !target) {
    return false;
  }
  TypePtr current = source;
  while (true) {
    if (sameType(current, target)) {
      return true;
    }
    switch (current->kind()) {
    case TypeKind::Reference:
    case TypeKind::Box:
      current = current->elementType();
      break;
    default:
      return false;
    }
    if (!current) {
      return false;
    }
  }
}

// &mut S -> &mut T
bool canDerefToMutable(const TypePtr &source, const TypePtr &target) {
  if (!source || !target) {
    return false;
  }
  TypePtr current = source;
  while (true) {
    if (sameType(current, target)) {
      return true;
    }
    switch (current->kind()) {
    case TypeKind::Box:
      current = current->elementType();
      break;
    case TypeKind::Reference:
      // mutable coercion path 不能经过 shared reference
      if (!current->isMutableReference()) {
        return false;
      }
      current = current->elementType();
      break;
    default:
      return false;
    }
    if (!current) {
      return false;
    }
  }
}
} // namespace

// Type constructors
Type::Type(TypeKind kind) : kind_(kind) {}
Type::Type(TypeKind kind, TypePtr elementType)
    : kind_(kind), elementType_(std::move(elementType)) {}
Type::Type(TypePtr referentType, bool mutableReference)
    : kind_(TypeKind::Reference), elementType_(std::move(referentType)),
      mutableReference_(mutableReference) {}
Type::Type(TypePtr elementType, std::uint32_t arrayLength)
    : kind_(TypeKind::Array), elementType_(std::move(elementType)),
      arrayLength_(arrayLength) {}
Type::Type(const StructSymbol *symbol)
    : kind_(TypeKind::Struct), structSymbol_(symbol) {}

// Primitive factories
TypePtr Type::makeI32() {
  static const TypePtr type(new Type(TypeKind::I32));
  return type;
}
TypePtr Type::makeU32() {
  static const TypePtr type(new Type(TypeKind::U32));
  return type;
}
TypePtr Type::makeISize() {
  static const TypePtr type(new Type(TypeKind::ISize));
  return type;
}
TypePtr Type::makeUSize() {
  static const TypePtr type(new Type(TypeKind::USize));
  return type;
}
TypePtr Type::makeBool() {
  static const TypePtr type(new Type(TypeKind::Bool));
  return type;
}
TypePtr Type::makeUnit() {
  static const TypePtr type(new Type(TypeKind::Unit));
  return type;
}
TypePtr Type::makeNever() {
  static const TypePtr type(new Type(TypeKind::Never));
  return type;
}

// Compound factories
TypePtr Type::makeStruct(const StructSymbol *symbol) {
  return TypePtr(new Type(symbol));
}
TypePtr Type::makeArray(TypePtr elementType, std::uint32_t arrayLength) {
  return TypePtr(new Type(std::move(elementType), arrayLength));
}
TypePtr Type::makeReference(TypePtr referentType, bool mutableReference) {
  return TypePtr(new Type(std::move(referentType), mutableReference));
}
TypePtr Type::makeBox(TypePtr elementType) {
  return TypePtr(new Type(TypeKind::Box, std::move(elementType)));
}
TypePtr Type::makeVec(TypePtr elementType) {
  return TypePtr(new Type(TypeKind::Vec, std::move(elementType)));
}

bool Type::isInteger() const {
  switch (kind_) {
  case TypeKind::I32:
  case TypeKind::U32:
  case TypeKind::ISize:
  case TypeKind::USize:
    return true;
  default:
    return false;
  }
}

bool Type::isSignedInteger() const {
  return kind_ == TypeKind::I32 || kind_ == TypeKind::ISize;
}

bool Type::isUnsignedInteger() const {
  return kind_ == TypeKind::U32 || kind_ == TypeKind::USize;
}

// Type equality
bool sameType(const TypePtr &lhs, const TypePtr &rhs) {
  if (lhs == rhs) {
    return true;
  }
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->kind() != rhs->kind()) {
    return false;
  }
  switch (lhs->kind()) {
  case TypeKind::I32:
  case TypeKind::U32:
  case TypeKind::ISize:
  case TypeKind::USize:
  case TypeKind::Bool:
  case TypeKind::Unit:
  case TypeKind::Never:
    return true;

  case TypeKind::Struct:
    return lhs->structSymbol() == rhs->structSymbol();

  case TypeKind::Array:
    return lhs->arrayLength() == rhs->arrayLength() &&
           sameType(lhs->elementType(), rhs->elementType());

  case TypeKind::Reference:
    return lhs->isMutableReference() == rhs->isMutableReference() &&
           sameType(lhs->elementType(), rhs->elementType());

  case TypeKind::Box:
  case TypeKind::Vec:
    return sameType(lhs->elementType(), rhs->elementType());
  }
  return false;
}

bool canCoerce(const TypePtr &source, const TypePtr &target) {
  if (!source || !target) {
    return false;
  }

  // T -> T
  if (sameType(source, target)) {
    return true;
  }

  // Never -> Any
  if (source->kind() == TypeKind::Never) {
    return true;
  }

  if (source->kind() != TypeKind::Reference ||
      target->kind() != TypeKind::Reference) {
    return false;
  }
  const TypePtr &sourceReferent = source->elementType();
  const TypePtr &targetReferent = target->elementType();

  // Target: &T
  if (!target->isMutableReference()) {
    return canDerefToShared(sourceReferent, targetReferent);
  }

  // Target: &mut T
  if (!source->isMutableReference()) {
    return false;
  }
  return canDerefToMutable(sourceReferent, targetReferent);
}

// Debug printing
std::string typeToString(const TypePtr &type) {
  if (!type) {
    return "<null-type>";
  }

  switch (type->kind()) {
  case TypeKind::I32:
    return "i32";
  case TypeKind::U32:
    return "u32";
  case TypeKind::ISize:
    return "isize";
  case TypeKind::USize:
    return "usize";
  case TypeKind::Bool:
    return "bool";
  case TypeKind::Unit:
    return "()";
  case TypeKind::Never:
    return "!";
  case TypeKind::Struct:
    if (type->structSymbol() == nullptr) {
      return "<invalid-struct>";
    }
    return type->structSymbol()->name;
  case TypeKind::Array:
    return "[" + typeToString(type->elementType()) + "; " +
           std::to_string(type->arrayLength()) + "]";
  case TypeKind::Reference: {
    std::string result = "&";
    if (type->isMutableReference()) {
      result += "mut ";
    }
    result += typeToString(type->elementType());
    return result;
  }
  case TypeKind::Box:
    return "Box<" + typeToString(type->elementType()) + ">";
  case TypeKind::Vec:
    return "Vec<" + typeToString(type->elementType()) + ">";
  }
  return "<unknown-type>";
}

} // namespace rx::semantic