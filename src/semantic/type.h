#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace rx::semantic {

class StructSymbol;

enum class TypeKind {
  // Integer types
  I32,
  U32,
  ISize,
  USize,
  // Primitive types
  Bool,
  Unit,
  // User-defined / compound types
  Struct,
  Array,
  Reference,
  Box,
  Vec,
  Never,
};

class Type;
using TypePtr = std::shared_ptr<const Type>;

class Type final {
public:
  // Primitive type factories
  static TypePtr makeI32();
  static TypePtr makeU32();
  static TypePtr makeISize();
  static TypePtr makeUSize();
  static TypePtr makeBool();
  static TypePtr makeUnit();
  static TypePtr makeNever();
  // Compound type factories
  static TypePtr makeStruct(const StructSymbol *symbol);
  static TypePtr makeArray(TypePtr elementType, std::uint32_t arrayLength);
  static TypePtr makeReference(TypePtr referentType, bool mutableReference);
  // Box<T>
  static TypePtr makeBox(TypePtr elementType);
  // Vec<T>
  static TypePtr makeVec(TypePtr elementType);

  TypeKind kind() const { return kind_; }
  bool isInteger() const;
  bool isSignedInteger() const;
  bool isUnsignedInteger() const;
  bool isUnit() const { return kind_ == TypeKind::Unit; }
  bool isBool() const { return kind_ == TypeKind::Bool; }
  bool isNever() const { return kind_ == TypeKind::Never; }
  bool isStruct() const { return kind_ == TypeKind::Struct; }
  bool isArray() const { return kind_ == TypeKind::Array; }
  bool isReference() const { return kind_ == TypeKind::Reference; }
  bool isBox() const { return kind_ == TypeKind::Box; }
  bool isVec() const { return kind_ == TypeKind::Vec; }

  const TypePtr &elementType() const { return elementType_; }
  std::uint32_t arrayLength() const { return arrayLength_; }
  bool isMutableReference() const { return mutableReference_; }
  const StructSymbol *structSymbol() const { return structSymbol_; }

private:
  TypeKind kind_;
  TypePtr elementType_;
  std::uint32_t arrayLength_ = 0;
  bool mutableReference_ = false;
  const StructSymbol *structSymbol_ = nullptr;

  explicit Type(TypeKind kind);
  // Box<T> / Vec<T>
  Type(TypeKind kind, TypePtr elementType);
  // Reference
  Type(TypePtr referentType, bool mutableReference);
  // Array
  Type(TypePtr elementType, std::uint32_t arrayLength);
  // Struct
  explicit Type(const StructSymbol *symbol);
};

bool sameType(const TypePtr &lhs, const TypePtr &rhs);
bool canCoerce(const TypePtr &source, const TypePtr &target);
std::string typeToString(const TypePtr &type);
} // namespace rx::semantic