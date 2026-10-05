#pragma once

#include <string>
#include <vector>

#include "geometry/linalg.h"

class ModuleInstantiation;

// Turns a graphical edit (e.g. a gizmo drag) into minimal edits of the
// source text, so comments, formatting and the rest of the file stay intact.
//
// All positions, directions and factors are given in the coordinate frame of
// the edited statement's parent.
namespace VisualEdit {

// A replacement of source text. Lines and columns are 1-based, columns count
// bytes (as the OpenSCAD lexer does), and the end position is exclusive.
// An insertion has start == end.
struct TextEdit {
  int firstLine;
  int firstColumn;
  int lastLine;
  int lastColumn;
  std::string text;
};

// Shortest decimal representation of v, rounded to 1e-6 ("10", "2.5", "-0.25").
std::string formatNumber(double v);

// Moves the object created by `inst` by `delta`.
// - translate() with a literal vector: its components are rewritten
//   (non-literal components get "+ d" appended).
// - Otherwise the statement is prefixed with "translate([dx, dy, dz]) ".
std::vector<TextEdit> planTranslate(const ModuleInstantiation& inst, const Vector3d& delta);

// Rotates the object by `angleDeg` about `axis` through `pivot`.
// - "translate(P) rotate([..]) translate(-P) ..." (as written by this
//   function): only the angles are updated, P stays the pivot.
// - Otherwise the statement is prefixed with that pattern.
std::vector<TextEdit> planRotate(const ModuleInstantiation& inst, const Vector3d& pivot,
                                 const Vector3d& axis, double angleDeg);

// Scales the object by `factors` along the parent's axes, keeping `anchor` fixed.
// - "translate(A) scale([..]) translate(-A) ...": only the factors are updated.
// - A non-centered cube() with a literal size (optionally inside a literal
//   translate()) whose corner is the anchor: the size is rewritten. Same for
//   the height of a cylinder() scaled along Z only.
// - Otherwise the statement is prefixed with the scale pattern.
std::vector<TextEdit> planScale(const ModuleInstantiation& inst, const Vector3d& anchor,
                                const Vector3d& factors);

}  // namespace VisualEdit
