#include "gui/VisualEdit.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "core/AST.h"
#include "core/Assignment.h"
#include "core/Expression.h"
#include "core/ModuleInstantiation.h"

namespace VisualEdit {

namespace {

constexpr double kEpsilon = 1e-9;

// The vector argument of translate(): first positional argument or "v".
const Vector *translateVector(const ModuleInstantiation& inst)
{
  for (const auto& arg : inst.arguments) {
    if (arg->getName().empty() || arg->getName() == "v") {
      return dynamic_cast<const Vector *>(arg->getExpr().get());
    }
  }
  return nullptr;
}

TextEdit replaceAt(const Location& loc, std::string text)
{
  return {loc.firstLine(), loc.firstColumn(), loc.lastLine(), loc.lastColumn(), std::move(text)};
}

TextEdit insertAt(int line, int column, std::string text)
{
  return {line, column, line, column, std::move(text)};
}

// " + 5" or " - 5"
std::string signedTerm(double d)
{
  return (d < 0 ? " - " : " + ") + formatNumber(std::fabs(d));
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

  const Vector *vec = inst.name() == "translate" ? translateVector(inst) : nullptr;
  const auto children = vec ? vec->getChildren() : std::vector<std::shared_ptr<Expression>>{};
  const bool editable = vec && (children.size() == 2 || children.size() == 3) &&
                        !vec->location().isNone();

  if (!editable) {
    const Location& loc = inst.location();
    edits.push_back(insertAt(loc.firstLine(), loc.firstColumn(),
                             "translate([" + formatNumber(delta[0]) + ", " + formatNumber(delta[1]) +
                               ", " + formatNumber(delta[2]) + "]) "));
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
    const auto *literal = dynamic_cast<const Literal *>(child.get());
    if (literal && literal->isDouble()) {
      edits.push_back(replaceAt(child->location(), formatNumber(literal->toDouble() + d)));
    } else {
      // Keep the expression (e.g. a variable) and offset it.
      const Location& loc = child->location();
      edits.push_back(insertAt(loc.lastLine(), loc.lastColumn(), signedTerm(d)));
    }
  }
  return edits;
}

}  // namespace VisualEdit
