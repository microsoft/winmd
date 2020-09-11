
#include <iostream>
#include <string>
#include <filesystem>
#include <sstream>
#include <winmd_reader.h>
using namespace std;
using namespace winmd::reader;

string currentNamespace;
string_view ObjectClassName = "Object"; // corresponds to IInspectable in C++/WinRT
string_view ctorName = ".ctor";

std::string_view ToString(ElementType elementType) {
  switch (elementType) {
  case ElementType::Boolean:
    return "bool";
  case ElementType::I2:
    return "short";
  case ElementType::I4:
    return "int";
  case ElementType::I8:
    return "long long";
  case ElementType::U2:
    return "unsigned short";
  case ElementType::U4:
    return "unsigned int";
  case ElementType::R4:
    return "float";
  case ElementType::R8:
    return "double";
  case ElementType::String:
    return "string";
  case ElementType::Class:
    return "{class}";
  case ElementType::GenericInst:
    return "{generic}";
  case ElementType::ValueType:
    return "{ValueType}";
  case ElementType::Object:
    return ObjectClassName;
  default:
    cout << std::hex << (int)elementType << endl;
    return "{type}";

  }
}

std::string GetTypeKind(const TypeDef& type)
{
  if (type.Flags().Semantics() == TypeSemantics::Interface) {
    return "interface";
  }
  else if (type.is_enum()) {
    return "enum";
  }
  else {
    return "class";
  }
}

string GetNamespacePrefix(std::string_view ns)
{
  if (ns == currentNamespace) return "";
  else return string(ns) + ".";
}

string ToString(const coded_index<TypeDefOrRef>& tdr)
{
  switch (tdr.type()) {
  case TypeDefOrRef::TypeDef:
  {
    const auto& td = tdr.TypeDef();
    return GetNamespacePrefix(td.TypeNamespace()) + string(td.TypeName());
  }
  case TypeDefOrRef::TypeRef:
  {
    const auto& tr = tdr.TypeRef();
    return GetNamespacePrefix(tr.TypeNamespace()) + string(tr.TypeName());
  }
  case TypeDefOrRef::TypeSpec:
  {
    const auto& ts = tdr.TypeSpec();
    return "TypeSpec";
  }
  default:
    throw std::invalid_argument("");
  }

}

string GetType(const TypeSig& type) {
  if (type.element_type() != ElementType::Class &&
    type.element_type() != ElementType::ValueType &&
    type.element_type() != ElementType::GenericInst
    ) {
    return string(ToString(type.element_type()));
  }
  else {
    auto valueType = type.Type();
    switch (valueType.index())
    {
    case 0: // ElementType
      break;
    case 1: // coded_index<TypeDefOrRef>
    {
      const auto& t = std::get<coded_index<TypeDefOrRef>>(valueType);
      return string(ToString(t));
    }
    case 2: // GenericTypeIndex
      break;
    case 3: // GenericTypeInstSig
    {
      const auto& gt = std::get<GenericTypeInstSig>(valueType);
      const auto& genericType = gt.GenericType();
      const auto& outerType = std::string(ToString(genericType));
      stringstream ss;
      const auto& prettyOuterType = outerType.substr(0, outerType.find('`'));
      ss << prettyOuterType << '<';

      bool first = true;
      for (const auto& arg : gt.GenericArgs()) {
        if (!first) {
          ss << ", ";
        }
        first = false;
        ss << GetType(arg);
      }

      ss << '>';
      return ss.str();
    }
    case 4: // GenericMethodTypeIndex
      break;

    default:
      break;
    }

    return "{NYI}some type";
  }
}


void print_enum(const TypeDef& type) {
  cout << "enum " << type.TypeName() << endl;
  for (auto const& value : type.FieldList()) {
    if (value.Flags().SpecialName()) {
      continue;
    }
    cout << "    " << value.Name() << " = " << std::hex << "0x" << ((value.Signature().Type().element_type() == ElementType::U4) ? value.Constant().ValueUInt32() : value.Constant().ValueInt32()) << endl;
  }
}

void print_property(const Property& prop) {
  cout << "    " << GetType(prop.Type().Type()) << " " << prop.Name() << endl;
}

void print_method(const MethodDef& method, string_view realName = "") {
  std::string returnType;
  const auto& signature = method.Signature();
  if (method.Signature().ReturnType()) {
    const auto& type = method.Signature().ReturnType().Type();
    returnType = GetType(type);
  }
  else {
    returnType = "void";
  }
  const auto flags = method.Flags();
  const auto& name = realName.empty() ? method.Name() : realName;
  std::cout << "    "
    << (flags.Static() ? "static " : "")
//    << (flags.Abstract() ? "abstract " : "")
    << returnType << " " << name << "(";

  int i = 0;

  std::vector<string_view> paramNames;
  for (auto const& p : method.ParamList()) {
    paramNames.push_back(p.Name());
  }

  for (const auto& param : signature.Params()) {
    if (i != 0) {
      cout << ", ";
    }
    cout << GetType(param.Type()) << " " << paramNames[i];
    i++;
  }
  cout << ")" << endl;
}


void print_field(const Field& field) {
  cout << "    " << GetType(field.Signature().Type())  << " " << field.Name() << endl;
}

void print_struct(const TypeDef& type) {
  cout << "struct " << type.TypeName() << endl;
  for (auto const& field : type.FieldList()) {
    print_field(field);
  }
}

void print_class(const TypeDef& type) {
  cout << "class " << type.TypeName() << endl;
  for (auto const& prop : type.PropertyList()) {
    print_property(prop);
  }
  for (auto const& method : type.MethodList()) {
    if (method.Name() == ctorName) {
      print_method(method, type.TypeName());
    } else if (method.SpecialName()) {
      continue; // get_ / put_ methods that are properties
    }
    else {
      print_method(method);
    }
  }
}

void print(const cache::namespace_members& ns) {
  for (auto const& enumEntry : ns.enums) {
    print_enum(enumEntry);
  }

  for (auto const& interfaceEntry : ns.interfaces) {
    print_class(interfaceEntry);
  }

  for (auto const& structEntry : ns.structs) {
    print_struct(structEntry);
  }

  for (auto const& classEntry : ns.classes) {
    print_class(classEntry);
  }
}

int main(int argc, char** argv)
{
  if (argc != 2) {
    cerr << "Usage: " << argv[0] << " pathToMetadata.winmd";
    return -1;
  }

  std::string winmdPath(argv[1]);
  cache cache(winmdPath);
  
  for (auto const& namespaceEntry : cache.namespaces()) {
    cout << "namespace " << namespaceEntry.first << endl;
    currentNamespace = namespaceEntry.first;
    print(namespaceEntry.second);
  }
  return 0;
}
