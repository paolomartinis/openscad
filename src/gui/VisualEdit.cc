#include "gui/VisualEdit.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/AST.h"
#include "core/Assignment.h"
#include "core/Expression.h"
#include "core/LocalScope.h"
#include "core/ModuleInstantiation.h"

namespace VisualEdit {

namespace {

constexpr double kEpsilon = 1e-9;
constexpr double kPositionTolerance = 1e-4;  // mm

// Named argument `name`, or else the positional argument number `position`.
const Expression *argument(const ModuleInstantiation& inst, const char *name, size_t position)
{
  size_t positional = 0;
  const Expression *byPosition = nullptr;
  for (const auto& arg : inst.arguments) {
    if (arg->getName() == name) return arg->getExpr().get();
    if (arg->getName().empty()) {
      if (positional == position) byPosition = arg->getExpr().get();
      ++positional;
    }
  }
  return byPosition;
}

std::optional<double> literalNumber(const Expression *expr)
{
  const auto *literal = dynamic_cast<const Literal *>(expr);
  if (literal && literal->isDouble()) return literal->toDouble();
  return std::nullopt;
}

// [x, y] or [x, y, z] made of number literals ([x, y] has z = 0).
std::optional<Vector3d> literalVector(const Expression *expr, bool requireThree = false)
{
  const auto *vec = dynamic_cast<const Vector *>(expr);
  if (!vec) return std::nullopt;
  const auto& children = vec->getChildren();
  if (children.size() != 3 && (requireThree || children.size() != 2)) return std::nullopt;
  Vector3d result = Vector3d::Zero();
  for (size_t i = 0; i < children.size(); ++i) {
    const auto value = literalNumber(children[i].get());
    if (!value) return std::nullopt;
    result[static_cast<int>(i)] = *value;
  }
  return result;
}

// center absent or a literal false.
bool notCentered(const ModuleInstantiation& inst, size_t position)
{
  const Expression *center = argument(inst, "center", position);
  if (!center) return true;
  const auto *literal = dynamic_cast<const Literal *>(center);
  return literal && literal->isBool() && !literal->toBool();
}

// The single child statement of `inst` ("translate(..) cube(..);").
const ModuleInstantiation *onlyChild(const ModuleInstantiation& inst)
{
  if (!inst.scope || !inst.scope->assignments.empty() || inst.scope->moduleInstantiations.size() != 1) {
    return nullptr;
  }
  return inst.scope->moduleInstantiations.front().get();
}

std::string vectorText(const Vector3d& v)
{
  return "[" + formatNumber(v[0]) + ", " + formatNumber(v[1]) + ", " + formatNumber(v[2]) + "]";
}

TextEdit replaceAt(const Location& loc, std::string text)
{
  return {loc.firstLine(), loc.firstColumn(), loc.lastLine(), loc.lastColumn(), std::move(text)};
}

// New values for a literal [x, y, z]. Elements are replaced one by one: the
// parser's location of a Vector does not span the whole "[...]".
std::vector<TextEdit> replaceVector(const Expression *expr, const Vector3d& value)
{
  std::vector<TextEdit> edits;
  const auto *vec = dynamic_cast<const Vector *>(expr);
  if (!vec) return edits;
  const auto& children = vec->getChildren();
  for (size_t i = 0; i < children.size() && i < 3; ++i) {
    edits.push_back(replaceAt(children[i]->location(), formatNumber(value[static_cast<int>(i)])));
  }
  return edits;
}

TextEdit insertAt(int line, int column, std::string text)
{
  return {line, column, line, column, std::move(text)};
}

TextEdit prefixStatement(const ModuleInstantiation& inst, std::string text)
{
  const Location& loc = inst.location();
  return insertAt(loc.firstLine(), loc.firstColumn(), std::move(text));
}

// " + 5" or " - 5"
std::string signedTerm(double d)
{
  return (d < 0 ? " - " : " + ") + formatNumber(std::fabs(d));
}

// "translate(P) <middle>(<vector>) translate(-P) ..." with literal vectors.
struct Sandwich {
  Vector3d pivot;
  Vector3d value;
  const Expression *valueExpr;
};

std::optional<Sandwich> matchSandwich(const ModuleInstantiation& inst, const std::string& middleName)
{
  if (inst.name() != "translate") return std::nullopt;
  const auto pivot = literalVector(argument(inst, "v", 0));
  const ModuleInstantiation *middle = onlyChild(inst);
  if (!pivot || !middle || middle->name() != middleName) return std::nullopt;
  const Expression *valueExpr = argument(*middle, middleName == "rotate" ? "a" : "v", 0);
  const auto value = literalVector(valueExpr, true);
  const ModuleInstantiation *back = onlyChild(*middle);
  if (!value || !back || back->name() != "translate") return std::nullopt;
  const auto backVector = literalVector(argument(*back, "v", 0));
  if (!backVector || (*backVector + *pivot).norm() > kPositionTolerance) return std::nullopt;
  return Sandwich{*pivot, *value, valueExpr};
}

std::string sandwichText(const Vector3d& pivot, const std::string& middle, const Vector3d& value)
{
  return "translate(" + vectorText(pivot) + ") " + middle + "(" + vectorText(value) + ") translate(" +
         vectorText(-pivot) + ") ";
}

constexpr double kDegToRad = M_PI / 180.0;

// OpenSCAD rotate([x, y, z]) = Rz * Ry * Rx.
Eigen::Matrix3d eulerToMatrix(const Vector3d& deg)
{
  return (Eigen::AngleAxisd(deg[2] * kDegToRad, Vector3d::UnitZ()) *
          Eigen::AngleAxisd(deg[1] * kDegToRad, Vector3d::UnitY()) *
          Eigen::AngleAxisd(deg[0] * kDegToRad, Vector3d::UnitX()))
    .toRotationMatrix();
}

Vector3d matrixToEuler(const Eigen::Matrix3d& m)
{
  const double sy = std::clamp(-m(2, 0), -1.0, 1.0);
  const double y = std::asin(sy);
  double x, z;
  if (std::cos(y) > 1e-9) {
    x = std::atan2(m(2, 1), m(2, 2));
    z = std::atan2(m(1, 0), m(0, 0));
  } else {
    // Gimbal lock: put the whole rotation about Z.
    x = 0.0;
    z = std::atan2(-m(0, 1), m(1, 1));
  }
  return Vector3d(x, y, z) / kDegToRad;
}

}  // namespace

namespace {

// Offset of the first character that is not whitespace or a comment.
size_t skipBlank(const std::string& text, size_t i)
{
  while (i < text.size()) {
    if (std::isspace(static_cast<unsigned char>(text[i]))) {
      ++i;
    } else if (text.compare(i, 2, "//") == 0) {
      i = text.find('\n', i);
      if (i == std::string::npos) return text.size();
    } else if (text.compare(i, 2, "/*") == 0) {
      i = text.find("*/", i + 2);
      if (i == std::string::npos) return text.size();
      i += 2;
    } else {
      break;
    }
  }
  return i;
}

}  // namespace

std::string formatNumber(double v)
{
  const double rounded = std::round(v * 1e6) / 1e6;
  if (rounded == 0.0) return "0";
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.6f", rounded);
  std::string s(buf);
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  return s;
}

std::vector<TextEdit> planTranslate(const ModuleInstantiation& inst, const Vector3d& delta)
{
  std::vector<TextEdit> edits;
  if (delta.cwiseAbs().maxCoeff() < kEpsilon) return edits;

  const auto *vec = inst.name() == "translate" ? dynamic_cast<const Vector *>(argument(inst, "v", 0)) : nullptr;
  const auto children = vec ? vec->getChildren() : std::vector<std::shared_ptr<Expression>>{};
  const bool editable = vec && (children.size() == 2 || children.size() == 3);

  if (!editable) {
    edits.push_back(prefixStatement(inst, "translate(" + vectorText(delta) + ") "));
    return edits;
  }

  for (size_t axis = 0; axis < 3; ++axis) {
    const double d = delta[static_cast<int>(axis)];
    if (std::fabs(d) < kEpsilon) continue;

    if (axis >= children.size()) {
      // translate([x, y]) gaining a z component.
      const Location& last = children.back()->location();
      edits.push_back(insertAt(last.lastLine(), last.lastColumn(), ", " + formatNumber(d)));
      continue;
    }

    const auto& child = children[axis];
    if (const auto value = literalNumber(child.get())) {
      edits.push_back(replaceAt(child->location(), formatNumber(*value + d)));
    } else {
      // Keep the expression (e.g. a variable) and offset it.
      const Location& loc = child->location();
      edits.push_back(insertAt(loc.lastLine(), loc.lastColumn(), signedTerm(d)));
    }
  }
  return edits;
}

std::vector<TextEdit> planRotate(const ModuleInstantiation& inst, const Vector3d& pivot,
                                 const Vector3d& axis, double angleDeg)
{
  if (std::fabs(angleDeg) < kEpsilon || axis.norm() < kEpsilon) return {};
  const Eigen::Matrix3d delta =
    Eigen::AngleAxisd(angleDeg * kDegToRad, axis.normalized()).toRotationMatrix();

  if (const auto sandwich = matchSandwich(inst, "rotate")) {
    const Vector3d angles = matrixToEuler(delta * eulerToMatrix(sandwich->value));
    return replaceVector(sandwich->valueExpr, angles);
  }
  return {prefixStatement(inst, sandwichText(pivot, "rotate", matrixToEuler(delta)))};
}

std::vector<TextEdit> planScale(const ModuleInstantiation& inst, const Vector3d& anchor,
                                const Vector3d& factors)
{
  if ((factors - Vector3d::Ones()).cwiseAbs().maxCoeff() < kEpsilon) return {};
  if (factors.minCoeff() <= 0.0) return {};

  if (const auto sandwich = matchSandwich(inst, "scale")) {
    return replaceVector(sandwich->valueExpr, sandwich->value.cwiseProduct(factors));
  }

  // A primitive whose corner sits on the anchor can simply change size.
  const ModuleInstantiation *primitive = &inst;
  Vector3d origin = Vector3d::Zero();
  if (inst.name() == "translate") {
    const auto offset = literalVector(argument(inst, "v", 0));
    primitive = offset ? onlyChild(inst) : nullptr;
    if (offset) origin = *offset;
  }
  // Only the axes that change size need the anchor on the primitive's corner.
  bool anchored = primitive != nullptr;
  for (int i = 0; i < 3; ++i) {
    if (std::fabs(factors[i] - 1.0) > kEpsilon && std::fabs(anchor[i] - origin[i]) > kPositionTolerance) {
      anchored = false;
    }
  }
  if (anchored) {
    if (primitive->name() == "cube" && notCentered(*primitive, 1)) {
      const Expression *size = argument(*primitive, "size", 0);
      if (const auto edge = literalNumber(size)) {
        const Vector3d scaled = *edge * factors;
        const bool uniform = (scaled - Vector3d::Constant(scaled[0])).cwiseAbs().maxCoeff() < kEpsilon;
        return {replaceAt(size->location(), uniform ? formatNumber(scaled[0]) : vectorText(scaled))};
      }
      if (const auto dims = literalVector(size, true)) {
        return replaceVector(size, dims->cwiseProduct(factors));
      }
    }
    const bool onlyZ = std::fabs(factors[0] - 1.0) < kEpsilon && std::fabs(factors[1] - 1.0) < kEpsilon;
    if (primitive->name() == "cylinder" && onlyZ && notCentered(*primitive, 3)) {
      const Expression *height = argument(*primitive, "h", 0);
      if (const auto h = literalNumber(height)) {
        return {replaceAt(height->location(), formatNumber(*h * factors[2]))};
      }
    }
  }

  return {prefixStatement(inst, sandwichText(anchor, "scale", factors))};
}
std::vector<TextEdit> planColor(const ModuleInstantiation& inst, const Vector3d& rgb)
{
  // An existing color() on the statement or its single-child chain.
  for (const ModuleInstantiation *current = &inst; current; current = onlyChild(*current)) {
    if (current->name() != "color") continue;
    const Expression *value = argument(*current, "c", 0);
    const auto *literal = dynamic_cast<const Literal *>(value);
    if (literal && literal->isString()) {
      char hex[16];
      std::snprintf(hex, sizeof(hex), "\"#%02x%02x%02x\"", static_cast<int>(std::round(rgb[0] * 255)),
                    static_cast<int>(std::round(rgb[1] * 255)), static_cast<int>(std::round(rgb[2] * 255)));
      return {replaceAt(value->location(), hex)};
    }
    const auto *vec = dynamic_cast<const Vector *>(value);
    if (vec && (vec->getChildren().size() == 3 || vec->getChildren().size() == 4)) {
      std::vector<TextEdit> edits;
      for (size_t i = 0; i < 3; ++i) {
        if (!literalNumber(vec->getChildren()[i].get())) return {};
        edits.push_back(replaceAt(vec->getChildren()[i]->location(), formatNumber(std::round(rgb[i] * 1000) / 1000)));
      }
      return edits;
    }
    return {};  // computed color: leave the code alone
  }

  char hex[32];
  std::snprintf(hex, sizeof(hex), "color(\"#%02x%02x%02x\") ", static_cast<int>(std::round(rgb[0] * 255)),
                static_cast<int>(std::round(rgb[1] * 255)), static_cast<int>(std::round(rgb[2] * 255)));
  return {prefixStatement(inst, hex)};
}

Vector3d anglesForNormal(const Vector3d& normal)
{
  const Vector3d n = normal.normalized();
  // Exact angles for faces along the axes.
  const struct {
    Vector3d n;
    Vector3d angles;
  } aligned[] = {
    {Vector3d::UnitZ(), {0, 0, 0}},   {-Vector3d::UnitZ(), {180, 0, 0}}, {Vector3d::UnitX(), {0, 90, 0}},
    {-Vector3d::UnitX(), {0, -90, 0}}, {Vector3d::UnitY(), {-90, 0, 0}}, {-Vector3d::UnitY(), {90, 0, 0}},
  };
  for (const auto& a : aligned) {
    if ((n - a.n).norm() < 1e-9) return a.angles;
  }
  return matrixToEuler(Eigen::Quaterniond::FromTwoVectors(Vector3d::UnitZ(), n).toRotationMatrix());
}

std::string placementPrefix(const Vector3d& point, const Vector3d& normal)
{
  std::string prefix = "translate(" + vectorText(point) + ") ";
  const Vector3d angles = anglesForNormal(normal);
  if (angles.cwiseAbs().maxCoeff() > 1e-9) prefix += "rotate(" + vectorText(angles) + ") ";
  return prefix;
}

size_t statementEnd(const std::string& text, size_t argsEnd)
{
  int depth = 0;
  for (size_t i = argsEnd; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '/' && (text.compare(i, 2, "//") == 0 || text.compare(i, 2, "/*") == 0)) {
      i = skipBlank(text, i) - 1;
    } else if (c == '"') {
      for (++i; i < text.size() && text[i] != '"'; ++i) {
        if (text[i] == '\\') ++i;
      }
    } else if (c == '(' || c == '[' || c == '{') {
      ++depth;
    } else if (c == ')' || c == ']' || c == '}') {
      if (--depth == 0 && c == '}') return i + 1;
      if (depth < 0) return std::string::npos;
    } else if (c == ';' && depth == 0) {
      return i + 1;
    }
  }
  return std::string::npos;
}

std::string addCut(const std::string& text, size_t start, size_t argsEnd, bool isDifference,
                   const std::string& cut, size_t *cutOffset)
{
  const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";

  // Include modifiers written before the statement ("#cube()", "% sphere()").
  size_t begin = start;
  for (size_t i = start; i > 0;) {
    const char c = text[--i];
    if (c == '#' || c == '%' || c == '!' || c == '*') begin = i;
    else if (c != ' ' && c != '\t') break;
  }
  // Indentation of the statement when it starts its line.
  const size_t lineStart = text.rfind('\n', begin == 0 ? 0 : begin - 1);
  const size_t indentFrom = lineStart == std::string::npos ? 0 : lineStart + 1;
  std::string indent = text.substr(indentFrom, begin - indentFrom);
  if (indent.find_first_not_of(" \t") != std::string::npos) indent.clear();

  const size_t end = statementEnd(text, argsEnd);
  if (end == std::string::npos) return {};

  const size_t brace = skipBlank(text, argsEnd);
  if (isDifference && brace < text.size() && text[brace] == '{') {
    // Append inside the existing block, before its closing brace.
    const size_t close = end - 1;
    const size_t closeLine = text.rfind('\n', close);
    const bool braceOnOwnLine =
      closeLine != std::string::npos && text.find_first_not_of(" \t\r", closeLine + 1) == close;
    std::string insertion;
    size_t at;
    if (braceOnOwnLine) {
      at = closeLine + 1;
      insertion = indent + "  " + cut + ";" + eol;
      *cutOffset = at + indent.size() + 2;
    } else {
      at = close;
      insertion = eol + indent + "  " + cut + ";" + eol + indent;
      *cutOffset = at + eol.size() + indent.size() + 2;
    }
    return text.substr(0, at) + insertion + text.substr(at);
  }

  // Wrap the statement: difference() { <statement> <cut>; }
  std::string body = text.substr(begin, end - begin);
  for (size_t i = 0; (i = body.find('\n', i)) != std::string::npos; i += 3) body.insert(i + 1, "  ");
  const std::string head = "difference() {" + eol + indent + "  " + body + eol + indent + "  ";
  *cutOffset = begin + head.size();
  return text.substr(0, begin) + head + cut + ";" + eol + indent + "}" + text.substr(end);
}

}  // namespace VisualEdit
