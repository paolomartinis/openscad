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

// Colors the object: an existing literal color() on the statement (or on its
// chain of single children) gets the new value, otherwise the statement is
// prefixed with color("#rrggbb"). `rgb` is in 0..1. Returns no edits when
// the existing color is computed by an expression.
std::vector<TextEdit> planColor(const ModuleInstantiation& inst, const Vector3d& rgb);

// ---- Placing parts on faces (byte offsets into the source text) ----

// Angles for rotate([x, y, z]) that turn +Z into `normal`.
Vector3d anglesForNormal(const Vector3d& normal);

// "translate([..]) rotate([..]) " that puts a part's z = 0 plane at `point`
// with its +Z along `normal`; rotate() is left out when not needed.
std::string placementPrefix(const Vector3d& point, const Vector3d& normal);

// Offset just past the statement whose module call ends at `argsEnd` (right
// after its closing parenthesis): past its ';' or the '}' closing its block.
// Comments and strings are skipped. Returns std::string::npos if unterminated.
size_t statementEnd(const std::string& text, size_t argsEnd);

// Adds `cut` (one statement, without ';') as a cutting tool of the statement
// starting at `start` whose module call ends at `argsEnd`: inside its braces
// when it is a difference() block, otherwise the statement is wrapped in
// "difference() { ... }". Returns the new text and sets *cutOffset to where
// `cut` starts in it; returns an empty string when the statement is not
// terminated.
std::string addCut(const std::string& text, size_t start, size_t argsEnd, bool isDifference,
                   const std::string& cut, size_t *cutOffset);

}  // namespace VisualEdit
