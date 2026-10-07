/*
 * NeoCalculator - NumOS
 * Copyright (C) 2026 Juan Ramon
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "LatexToAst.h"

#include <cstring>
#include <string>

namespace numos {
namespace mathast {

namespace {

using vpam::NodePtr;
using vpam::OpKind;
using vpam::FuncKind;
using vpam::ConstKind;

inline bool isDigit(char c) { return c >= '0' && c <= '9'; }
inline bool isAlpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
inline bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

/// UTF-8 literals for the Greek letters the subset admits. Values are the
/// standard 2-byte sequences; kept here so the mapping is one table, not
/// scattered string literals.
struct GreekName { const char* name; const char* utf8; };

constexpr GreekName kGreek[] = {
    {"alpha",   "\xCE\xB1"}, {"beta",    "\xCE\xB2"}, {"gamma",   "\xCE\xB3"},
    {"delta",   "\xCE\xB4"}, {"epsilon", "\xCE\xB5"}, {"zeta",    "\xCE\xB6"},
    {"eta",     "\xCE\xB7"}, {"theta",   "\xCE\xB8"}, {"iota",    "\xCE\xB9"},
    {"kappa",   "\xCE\xBA"}, {"lambda",  "\xCE\xBB"}, {"mu",      "\xCE\xBC"},
    {"nu",      "\xCE\xBD"}, {"xi",      "\xCE\xBE"}, {"rho",     "\xCF\x81"},
    {"sigma",   "\xCF\x83"}, {"tau",     "\xCF\x84"}, {"phi",     "\xCF\x86"},
    {"chi",     "\xCF\x87"}, {"psi",     "\xCF\x88"}, {"omega",   "\xCF\x89"},
    {"Gamma",   "\xCE\x93"}, {"Delta",   "\xCE\x94"}, {"Theta",   "\xCE\x98"},
    {"Lambda",  "\xCE\x9B"}, {"Xi",      "\xCE\x9E"}, {"Pi",      "\xCE\xA0"},
    {"Sigma",   "\xCE\xA3"}, {"Phi",     "\xCE\xA6"}, {"Psi",     "\xCE\xA8"},
    {"Omega",   "\xCE\xA9"},
};

/// Map a bare letter to a node. x/y/z and A-F are the variable slots the AST
/// already models; everything else becomes an engine-owned Symbol so we never
/// invent a variable kind the renderer does not know.
NodePtr letterNode(char c) {
    if (c == 'x' || c == 'y' || c == 'z') return vpam::makeVariable(c);
    if (c >= 'A' && c <= 'F')             return vpam::makeVariable(c);
    return vpam::makeSymbol(std::string(1, c));
}

/// Operator token → OpKind. Returns false when the token is not an operator.
bool opKindFor(const char* tok, size_t len, OpKind* out) {
    if (len == 1) {
        switch (tok[0]) {
            case '+': *out = OpKind::Add; return true;
            case '-': *out = OpKind::Sub; return true;
            case '*': *out = OpKind::Mul; return true;
            case '/': *out = OpKind::Div; return true;
            case '=': *out = OpKind::Eq;  return true;
            case '<': *out = OpKind::Lt;  return true;
            case '>': *out = OpKind::Gt;  return true;
            default:  return false;
        }
    }
    return false;
}

}  // namespace

// ════════════════════════════════════════════════════════════════════════════
// Parser
// ════════════════════════════════════════════════════════════════════════════

class Parser {
public:
    explicit Parser(const char* s) : _p(s ? s : "") {}

    CompileResult run() {
        CompileResult r;
        NodePtr root = parseRow("");
        if (_failed) {
            r.status = _tooLarge ? CompileStatus::TooLarge : CompileStatus::Unsupported;
            r.error  = _error;
            return r;
        }
        // Trailing input we did not consume means a stray '}' or ')' — refuse.
        skipSpaces();
        if (*_p != '\0') { fail("unexpected trailing input"); r.status = CompileStatus::Unsupported; r.error = _error; return r; }
        if (!root) { r.status = CompileStatus::Unsupported; r.error = "empty"; return r; }
        r.root = std::move(root);
        r.status = CompileStatus::Ok;
        return r;
    }

private:
    const char* _p;
    bool _failed   = false;
    bool _tooLarge = false;
    const char* _error = "";
    int  _depth = 0;
    int  _nodes = 0;

    void fail(const char* what) {
        if (!_failed) { _failed = true; _error = what; }
    }
    void failTooLarge(const char* what) {
        _tooLarge = true;
        fail(what);
    }

    void skipSpaces() { while (isSpace(*_p)) ++_p; }

    /// Consume `name` if it appears at the cursor as a whole command word
    /// (i.e. not a prefix of a longer word). Does not consume the leading '\'.
    bool commandIs(const char* name) const {
        const size_t n = std::strlen(name);
        if (std::strncmp(_p + 1, name, n) != 0) return false;
        const char after = _p[1 + n];
        return !isAlpha(after) || after == '\0';
    }

    static void appendRow(NodePtr& row, NodePtr child) {
        if (child) static_cast<vpam::NodeRow*>(row.get())->appendChild(std::move(child));
    }

    /// Charge a freshly built node and hand it on. Takes an RVALUE only: a
    /// by-value NodePtr parameter could silently be copied from an lvalue (and
    /// unique_ptr has a deleted copy ctor, so that mistake is a compile error
    /// waiting for whoever next edits this).
    NodePtr budget(NodePtr&& n) {
        if (n && ++_nodes > kMaxNodes) { failTooLarge("node budget"); return nullptr; }
        return std::move(n);
    }

    /// Charge a node the caller keeps owning (the row being filled is a local of
    /// the caller, so it cannot be passed through budget()).
    bool chargeNode() {
        if (++_nodes > kMaxNodes) { failTooLarge("node budget"); return false; }
        return true;
    }

    /// Parse atoms until NUL or one of `stops`. The closing delimiter itself is
    /// left unconsumed for the caller.
    NodePtr parseRow(const char* stops) {
        if (++_depth > kMaxDepth) { failTooLarge("nesting too deep"); return nullptr; }
        NodePtr row = vpam::makeRow();
        if (!chargeNode()) return nullptr;

        while (true) {
            skipSpaces();
            if (*_p == '\0') break;
            if (stops[0] && std::strchr(stops, *_p)) break;

            // Spacing commands (\ , \; \! \quad) carry no content.
            if (*_p == '\\') {
                const char n = _p[1];
                if (n == ',' || n == ';' || n == '!' || n == ' ') { _p += 2; continue; }
            }

            // Big operators swallow the rest of the row as their body.
            if (*_p == '\\' && (commandIs("int") || commandIs("sum"))) {
                NodePtr big = parseBigOperator();
                if (_failed) return nullptr;
                appendRow(row, std::move(big));
                continue;
            }

            NodePtr atom = parseScriptedAtom();
            if (_failed) return nullptr;
            if (!atom) break;   // e.g. end of input
            appendRow(row, std::move(atom));
        }

        --_depth;
        return row;
    }

    /// Parse `{ ... }` returning the inner Row (a brace group is just grouping).
    NodePtr parseBraceGroup() {
        // cursor is on '{'
        ++_p;
        NodePtr inner = parseRow("}");
        if (_failed) return nullptr;
        skipSpaces();
        if (*_p != '}') { fail("missing '}'"); return nullptr; }
        ++_p;
        return inner;
    }

    /// A required single-argument group: `{...}` or a single atom.
    NodePtr parseRequiredArg() {
        skipSpaces();
        if (*_p == '{') return parseBraceGroup();
        NodePtr a = parseScriptedAtom();
        if (!_failed && !a) fail("missing argument");
        return a;
    }

    NodePtr parseBigOperator() {
        skipSpaces();
        const bool isSum = commandIs("sum");
        _p += 4;   // "\sum" or "\int"

        NodePtr lower, upper;
        // Limits: _{...} / ^{...} in either order.
        for (int i = 0; i < 2; ++i) {
            skipSpaces();
            if (*_p == '_') { ++_p; lower = parseRequiredArg(); }
            else if (*_p == '^') { ++_p; upper = parseRequiredArg(); }
            else break;
            if (_failed) return nullptr;
        }
        // Body = the remainder of the enclosing row (documented rule).
        NodePtr body = parseRow("");
        if (_failed) return nullptr;

        if (isSum) {
            return budget(vpam::makeSummation(lower ? std::move(lower) : vpam::makeRow(),
                                              upper ? std::move(upper) : vpam::makeRow(),
                                              body  ? std::move(body)  : vpam::makeRow()));
        }
        return budget(vpam::makeDefIntegral(lower ? std::move(lower) : vpam::makeRow(),
                                            upper ? std::move(upper) : vpam::makeRow(),
                                            body  ? std::move(body)  : vpam::makeRow(),
                                            /*variable=*/nullptr));
    }

    /// An atom followed by any number of ^/_ scripts.
    NodePtr parseScriptedAtom() {
        NodePtr base = parseAtom();
        if (_failed || !base) return base;

        while (true) {
            skipSpaces();
            if (*_p == '^') {
                ++_p;
                NodePtr exp = parseRequiredArg();
                if (_failed) return nullptr;
                base = budget(vpam::makePower(std::move(base), std::move(exp)));
            } else if (*_p == '_') {
                ++_p;
                NodePtr sub = parseRequiredArg();
                if (_failed) return nullptr;
                base = budget(vpam::makeSubscript(std::move(base), std::move(sub)));
            } else {
                break;
            }
            if (!base) return nullptr;
        }
        return base;
    }

    NodePtr parseAtom() {
        skipSpaces();
        const char c = *_p;
        if (c == '\0') return nullptr;

        if (c == '{') return parseBraceGroup();

        if (c == '(') {
            ++_p;
            NodePtr inner = parseRow(")");
            if (_failed) return nullptr;
            skipSpaces();
            if (*_p != ')') { fail("missing ')'"); return nullptr; }
            ++_p;
            return budget(vpam::makeParen(std::move(inner), vpam::DelimKind::Paren));
        }
        if (c == ')') { fail("unmatched ')'"); return nullptr; }

        if (c == '[') {
            ++_p;
            NodePtr inner = parseRow("]");
            if (_failed) return nullptr;
            skipSpaces();
            if (*_p != ']') { fail("missing ']'"); return nullptr; }
            ++_p;
            return budget(vpam::makeParen(std::move(inner), vpam::DelimKind::Bracket));
        }

        if (c == '\\') return parseCommand();

        if (isDigit(c) || c == '.') {
            const char* start = _p;
            bool sawDot = false;
            while (isDigit(*_p) || (*_p == '.' && !sawDot)) {
                if (*_p == '.') sawDot = true;
                ++_p;
            }
            if (sawDot && (_p - start) == 1) { fail("lone '.'"); return nullptr; }
            return budget(vpam::makeNumber(std::string(start, static_cast<size_t>(_p - start))));
        }

        if (isAlpha(c)) {
            ++_p;
            return budget(letterNode(c));
        }

        // Operator token.
        {
            OpKind op;
            if (opKindFor(_p, 1, &op)) {
                ++_p;
                if (vpam::isRelation(op)) return budget(vpam::makeRelation(op));
                return budget(vpam::makeOperator(op));
            }
        }

        fail("unsupported character");
        return nullptr;
    }

    /// Cursor is on '\\'. `_p + 1` is the command name.
    NodePtr parseCommand() {
        struct Cmd { const char* name; };
        // Multi-char names first so `\sin` is not read as `\s`.
        if (commandIs("frac")) {
            _p += 5;
            NodePtr num = parseRequiredArg();
            if (_failed) return nullptr;
            NodePtr den = parseRequiredArg();
            if (_failed) return nullptr;
            return budget(vpam::makeFraction(std::move(num), std::move(den)));
        }
        if (commandIs("sqrt")) {
            _p += 5;
            NodePtr degree;
            skipSpaces();
            if (*_p == '[') {
                ++_p;
                degree = parseRow("]");
                if (_failed) return nullptr;
                skipSpaces();
                if (*_p != ']') { fail("missing ']' after \\sqrt["); return nullptr; }
                ++_p;
            }
            NodePtr rad = parseRequiredArg();
            if (_failed) return nullptr;
            return budget(vpam::makeRoot(std::move(rad), std::move(degree)));
        }

        // Binary operators spelled as commands.
        if (commandIs("cdot") || commandIs("times")) {
            _p += commandIs("cdot") ? 5 : 6;
            return budget(vpam::makeOperator(OpKind::Mul));
        }
        if (commandIs("div"))  { _p += 4; return budget(vpam::makeOperator(OpKind::Div)); }
        if (commandIs("pm"))   { _p += 3; return budget(vpam::makeOperator(OpKind::PlusMinus)); }
        if (commandIs("le") || commandIs("leq")) { _p += commandIs("le") ? 3 : 4; return budget(vpam::makeRelation(OpKind::Le)); }
        if (commandIs("ge") || commandIs("geq")) { _p += commandIs("ge") ? 3 : 4; return budget(vpam::makeRelation(OpKind::Ge)); }
        if (commandIs("ne") || commandIs("neq")) { _p += commandIs("ne") ? 3 : 4; return budget(vpam::makeRelation(OpKind::Ne)); }

        if (commandIs("pi"))     { _p += 3; return budget(vpam::makeConstant(ConstKind::Pi)); }
        // Advance 1 + strlen("infty") = 6. This said 7: commandIs() matches at _p + 1, so the
        // extra byte skipped the terminating NUL, which read as "unsupported character" for
        // a bare \infty and lost the closing brace in \sum_{n=1}^{\infty} ("missing '}'").
        if (commandIs("infty"))  { _p += 6; return budget(vpam::makeSpecialValue(vpam::SpecialValueKind::PositiveInfinity)); }

        // Functions.
        {
            struct Fn { const char* name; FuncKind kind; };
            static constexpr Fn kFns[] = {
                {"arcsin", FuncKind::ArcSin}, {"arccos", FuncKind::ArcCos},
                {"arctan", FuncKind::ArcTan}, {"sin", FuncKind::Sin},
                {"cos", FuncKind::Cos},       {"tan", FuncKind::Tan},
                {"ln", FuncKind::Ln},         {"log", FuncKind::Log},
            };
            for (const Fn& f : kFns) {
                if (commandIs(f.name)) {
                    _p += 1 + std::strlen(f.name);
                    // Argument is required: `\sin x` or `\sin{x}`.
                    NodePtr arg = parseRequiredArg();
                    if (_failed) return nullptr;
                    return budget(vpam::makeFunction(f.kind, std::move(arg)));
                }
            }
        }

        // Greek.
        for (const GreekName& g : kGreek) {
            if (commandIs(g.name)) {
                _p += 1 + std::strlen(g.name);
                return budget(vpam::makeSymbol(g.utf8));
            }
        }

        fail("unknown command");
        return nullptr;
    }
};

// ════════════════════════════════════════════════════════════════════════════
// Entry points
// ════════════════════════════════════════════════════════════════════════════

CompileResult compileSubsetLatex(const char* latex) {
    Parser p(latex);
    return p.run();
}

vpam::NodePtr tryCompileSubsetLatex(const char* latex) {
    CompileResult r = compileSubsetLatex(latex);
    return std::move(r.root);
}

}  // namespace mathast
}  // namespace numos
