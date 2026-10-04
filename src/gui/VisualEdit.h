#pragma once

#include <string>
#include <vector>

#include "geometry/linalg.h"

class ModuleInstantiation;

// Turns a graphical edit (e.g. a gizmo drag) into minimal edits of the
// source text, so comments, formatting and the rest of the file stay intact.
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

// Plans the edits that move the object created by `inst` by `delta`, given in
// the coordinate frame of the statement's parent.
// - If `inst` is a translate() with a literal vector, its components are
//   rewritten (non-literal components get "+ d" appended).
// - Otherwise the statement is prefixed with "translate([dx, dy, dz]) ".
// Returned edits never overlap; apply them from last to first.
std::vector<TextEdit> planTranslate(const ModuleInstantiation& inst, const Vector3d& delta);

}  // namespace VisualEdit
