#!/usr/bin/env python3
"""Auto-break lines >80 chars using libclang AST - smarter version."""
import clang.cindex
import sys
import re
from pathlib import Path

MAX_LEN = 80

INCLUDE_DIRS = [
    "-std=c++20",
    "-Isrc/lib",
    "-Isrc/kernel",
    "-Isrc/arch",
]

clang.cindex.Config.set_library_path('/Library/Developer/CommandLineTools/usr/lib')

def is_in_comment_or_string(line: str, col: int) -> bool:
    """Check if column is inside a comment or string literal."""
    in_string = False
    in_char = False
    escape = False
    string_char = None
    
    for i, ch in enumerate(line):
        if i >= col:
            break
        if escape:
            escape = False
            continue
        if ch == '\\':
            escape = True
            continue
        if not in_string and not in_char:
            if ch == '"':
                in_string = True
                string_char = '"'
            elif ch == "'":
                in_char = True
                string_char = "'"
            elif ch == '/' and i + 1 < len(line) and line[i + 1] == '/':
                return True  # Line comment
        elif in_string and ch == string_char:
            in_string = False
        elif in_char and ch == string_char:
            in_char = False
    return in_string or in_char

def is_in_macro_continuation(line: str, col: int) -> bool:
    """Check if we're in a macro continuation (ends with \\)"""
    # Check if line ends with backslash (macro continuation)
    stripped = line.rstrip()
    if stripped.endswith('\\'):
        return True
    # Check if this looks like a macro definition line
    if line.lstrip().startswith('#define'):
        return True
    return False

def find_break_points(line: str, max_len: int) -> list[int]:
    """Find all safe break points in a line."""
    if len(line) <= max_len:
        return []
    
    # Skip macro continuation lines (end with \)
    if line.rstrip().endswith('\\'):
        return []
    # Skip macro definition lines
    if line.lstrip().startswith('#define'):
        return []
    # Skip lines with multiple semicolons (multiple statements)
    if line.count(';') > 1:
        return []
    # Skip lines with :: chains (complex qualified names/calls)
    if '::' in line and line.count('::') > 2:
        return []
    # Skip lines with label-like patterns (X: Y;)
    if re.search(r'\w+:\s*\w+', line):
        return []
    
    candidates = []
    paren_depth = 0
    brace_depth = 0
    bracket_depth = 0
    
    for i, ch in enumerate(line):
        if is_in_comment_or_string(line, i):
            if ch == '(':
                paren_depth += 1
            elif ch == ')':
                paren_depth = max(0, paren_depth - 1)
            elif ch == '{':
                brace_depth += 1
            elif ch == '}':
                brace_depth = max(0, brace_depth - 1)
            elif ch == '[':
                bracket_depth += 1
            elif ch == ']':
                bracket_depth = max(0, bracket_depth - 1)
            continue
        
        if ch == '(':
            paren_depth += 1
        elif ch == ')':
            paren_depth = max(0, paren_depth - 1)
        elif ch == '{':
            brace_depth += 1
        elif ch == '}':
            brace_depth = max(0, brace_depth - 1)
        elif ch == '[':
            bracket_depth += 1
        elif ch == ']':
            bracket_depth = max(0, bracket_depth - 1)
        
        # Only consider breaks at depth 0 (not inside nested expressions)
        if paren_depth == 0 and brace_depth == 0 and bracket_depth == 0:
            if ch == ',':
                candidates.append(i + 1)
            elif ch in '&|' and i + 1 < len(line) and line[i + 1] == ch:
                candidates.append(i + 2)
            elif ch in '?:':  # Ternary
                candidates.append(i + 1)
            elif ch in '({[':
                # Don't break after opening brackets at depth 0
                pass
            elif ch in ')}]':
                candidates.append(i)
        
        # Also allow breaking after commas inside function args (depth 1)
        elif paren_depth == 1 and brace_depth == 0 and bracket_depth == 0:
            if ch == ',':
                candidates.append(i + 1)
    
    return candidates

def get_ast_break_points(tu, file_path: str, line_num: int, max_len: int) -> list[int]:
    """Use AST to find semantic break points for long lines."""
    break_points = []
    
    def visit(cursor):
        if not cursor.location.file or str(cursor.location.file) != file_path:
            return
        start_line = cursor.extent.start.line
        end_line = cursor.extent.end.line
        
        # If this node spans the long line
        if start_line <= line_num <= end_line:
            # Function parameters
            if cursor.kind in (clang.cindex.CursorKind.FUNCTION_DECL,
                               clang.cindex.CursorKind.CXX_METHOD,
                               clang.cindex.CursorKind.FUNCTION_TEMPLATE,
                               clang.cindex.CursorKind.CONSTRUCTOR):
                for child in cursor.get_children():
                    if child.kind == clang.cindex.CursorKind.PARM_DECL:
                        if child.extent.start.line == line_num:
                            break_points.append(child.extent.end.column)
            
            # Template parameters
            if cursor.kind == clang.cindex.CursorKind.TEMPLATE_TYPE_PARAMETER:
                if cursor.extent.start.line == line_num:
                    break_points.append(cursor.extent.end.column)
            
            # Initializer list
            if cursor.kind == clang.cindex.CursorKind.CXX_CTOR_INITIALIZER:
                for child in cursor.get_children():
                    if child.extent.start.line == line_num:
                        break_points.append(child.extent.end.column)
            
            # Binary operator (&&, ||, etc.)
            if cursor.kind == clang.cindex.CursorKind.BINARY_OPERATOR:
                if cursor.extent.start.line == line_num:
                    # Find operator position
                    tokens = list(cursor.get_tokens())
                    for tok in tokens:
                        if tok.spelling in ('&&', '||', ',', '?', ':'):
                            break_points.append(tok.extent.end.column)
            
            # Call expression (function arguments)
            if cursor.kind == clang.cindex.CursorKind.CALL_EXPR:
                for child in cursor.get_children():
                    if child.kind == clang.cindex.CursorKind.UNEXPOSED_EXPR:
                        for arg in child.get_children():
                            if arg.extent.start.line == line_num:
                                break_points.append(arg.extent.end.column)
        
        for child in cursor.get_children():
            visit(child)
    
    visit(tu.cursor)
    return sorted(set(break_points))

def fix_file(file_path: Path) -> tuple[bool, int]:
    """Fix lines >80 chars in a file. Returns (changed, count)."""
    source = file_path.read_text()
    lines = source.split('\n')
    changed = False
    fix_count = 0
    
    # Parse with libclang
    index = clang.cindex.Index.create()
    tu = index.parse(str(file_path), args=INCLUDE_DIRS)
    
    for diag in tu.diagnostics:
        if diag.severity >= clang.cindex.Diagnostic.Error:
            print(f"  Parse error: {diag}")
    
    # Process lines that are too long
    # Work backwards to avoid line number shifts
    long_lines = []
    for i, line in enumerate(lines):
        if len(line) > MAX_LEN and not line.strip().startswith('#'):
            long_lines.append((i + 1, line))
    
    if not long_lines:
        return False, 0
    
    # Sort by line number descending for safe insertion
    long_lines.sort(reverse=True)
    
    for line_num, line in long_lines:
        # Try AST-based break points first
        ast_breaks = get_ast_break_points(tu, str(file_path), line_num, MAX_LEN)
        heuristic_breaks = find_break_points(line, MAX_LEN)
        
        all_breaks = sorted(set(ast_breaks + heuristic_breaks))
        
        # Filter to those that keep first part <= MAX_LEN
        valid_breaks = [b for b in all_breaks if b <= MAX_LEN]
        
        if valid_breaks:
            break_col = max(valid_breaks)  # Rightmost valid break
            
            indent = len(line) - len(line.lstrip())
            continuation_indent = indent + 4
            before = line[:break_col].rstrip()
            after = line[break_col:].lstrip()
            
            lines[line_num - 1] = before
            lines.insert(line_num, ' ' * continuation_indent + after)
            changed = True
            fix_count += 1
            print(f"  {file_path}:{line_num} -> broken at col {break_col}")
        else:
            print(f"  {file_path}:{line_num} -> no safe break point found (len={len(line)})")
    
    if changed:
        file_path.write_text('\n'.join(lines))
    
    return changed, fix_count

def main():
    if len(sys.argv) < 2:
        print("Usage: fix_line_length.py <file> [file...]")
        return 1
    
    total_fixed = 0
    for arg in sys.argv[1:]:
        path = Path(arg)
        if path.is_file():
            print(f"Processing {path}...")
            changed, count = fix_file(path)
            if changed:
                print(f"  Fixed {count} lines")
                total_fixed += count
            else:
                print(f"  No changes needed")
    
    print(f"\nTotal lines fixed: {total_fixed}")
    return 0

if __name__ == '__main__':
    sys.exit(main())