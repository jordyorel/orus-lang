const std = @import("std");

pub const TokenType = enum {
    Number,
    String,
    Identifier,
    True,
    False,
    Nil,

    // --- Keywords ---
    Fn,
    Return,
    If,
    Elif,
    Else,
    While,
    For,
    And,
    Or,
    Not,
    Break,
    Continue,
    Print,

    // --- Arithmetic operators ---
    Plus,
    Minus,
    Star,
    Slash,
    Modulo,

    // --- Compound assignment ---
    PlusEqual,
    MinusEqual,
    StarEqual,
    SlashEqual,
    ModuloEqual,

    // --- Comparison ---
    EqualEqual,
    Bang,
    BangEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,

    // --- Bitwise ---
    BitAnd,
    BitOr,
    BitXor,
    BitNot,
    ShiftLeft,
    ShiftRight,

    // --- Assignment & declaration ---
    Equal,
    Define,

    // --- Punctuation ---
    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace,
    LeftBracket,
    RightBracket,
    Comma,
    Dot,
    DotDot,
    Colon,
    Semicolon,
    Pipe, // |   for loop capture  |val, i|
    Underscore, // _   ignore value     |_, i|

    // --- Special ---
    Error,
    Eof,
};

pub const Token = struct {
    type: TokenType,
    start: [*]const u8,
    length: usize,
    line: usize,
    column: u32,

    pub fn lexeme(self: Token) []const u8 {
        return self.start[0..self.length];
    }
};

pub const Lexer = struct {
    source: [*]const u8,
    start: [*]const u8,
    current: [*]const u8,
    end: [*]const u8,
    line: u32,
    column: u32,

    pub fn init(source: []const u8) Lexer {
        return .{
            .source = source.ptr,
            .start = source.ptr,
            .current = source.ptr,
            .end = source.ptr + source.len,
            .line = 1,
            .column = 1,
        };
    }

    pub fn identifierType(self: *Lexer) TokenType {
        const len = self.currentLength();
        const s = self.start[0..len];

        return switch (s[0]) {
            'a' => matchKeyword(s, "and", .And),
            'b' => matchKeyword(s, "break", .Break),
            'c' => matchKeyword(s, "continue", .Continue),
            'e' => if (std.mem.eql(u8, s, "else")) .Else else if (std.mem.eql(u8, s, "elif")) .Elif else .Identifier,
            'f' => if (std.mem.eql(u8, s, "fn")) .Fn else if (std.mem.eql(u8, s, "for")) .For else if (std.mem.eql(u8, s, "false")) .False else .Identifier,
            'i' => matchKeyword(s, "if", .If),
            'n' => if (std.mem.eql(u8, s, "not")) .Not else if (std.mem.eql(u8, s, "nil")) .Nil else .Identifier,
            'o' => matchKeyword(s, "or", .Or),
            'p' => matchKeyword(s, "print", .Print),
            'r' => matchKeyword(s, "return", .Return),
            't' => matchKeyword(s, "true", .True),
            'w' => matchKeyword(s, "while", .While),
            else => .Identifier,
        };
    }

    pub fn next(self: *Lexer) Token {
        self.skipWhiteSpaceAndComment();
        self.start = self.current;
        if (self.isAtEnd()) return self.makeToken(.Eof);

        const c = self.advance();

        if (isAlpha(c)) return self.identifier();
        if (isDigit(c)) return self.number();

        return switch (c) {
            '(' => self.makeToken(.LeftParen),
            ')' => self.makeToken(.RightParen),
            '{' => self.makeToken(.LeftBrace),
            '}' => self.makeToken(.RightBrace),

            ',' => self.makeToken(.Comma),
            ';' => self.makeToken(.Semicolon),
            ':' => if (self.match('=')) self.makeToken(.Define) else self.makeToken(.Semicolon),
            '|' => self.makeToken(.Pipe),
            '_' => if (isAlpha(self.peek()) or isDigit(self.peek())) self.identifier() else self.makeToken(.Underscore),
            '.' => if (self.match('.')) self.makeToken(.DotDot) else self.makeToken(.Dot),

            '+' => if (self.match('=')) self.makeToken(.PlusEqual) else self.makeToken(.Plus),
            '-' => if (self.match('=')) self.makeToken(.MinusEqual) else self.makeToken(.Minus),
            '*' => if (self.match('=')) self.makeToken(.StarEqual) else self.makeToken(.Star),
            '/' => if (self.match('=')) self.makeToken(.SlashEqual) else self.makeToken(.Slash),
            '%' => if (self.match('=')) self.makeToken(.ModuloEqual) else self.makeToken(.Modulo),

            // Comparison
            '=' => if (self.match('=')) self.makeToken(.EqualEqual) else self.makeToken(.Equal),
            '!' => if (self.match('=')) self.makeToken(.BangEqual) else self.makeToken(.Bang),
            '<' => if (self.match('<')) self.makeToken(.ShiftLeft) else if (self.match('=')) self.makeToken(.LessEqual) else self.makeToken(.Less),
            '>' => if (self.match('>')) self.makeToken(.ShiftRight) else if (self.match('=')) self.makeToken(.GreaterEqual) else self.makeToken(.Greater),

            '&' => self.makeToken(.BitAnd),
            '^' => self.makeToken(.BitXor),
            '~' => self.makeToken(.BitNot),

            '"' => self.string(),
            // String literal
            else => self.errorToken("Unexpected character."),
        };
    }

    inline fn makeToken(self: *Lexer, token_type: TokenType) Token {
        const len = self.currentLength();
        return .{
            .type = token_type,
            .start = self.start,
            .length = len,
            .line = self.line,
            .column = self.column - @as(u32, @intCast(len)),
        };
    }

    fn matchKeyword(s: []const u8, keyword: []const u8, token_type: TokenType) TokenType {
        return if (std.mem.eql(u8, s, keyword)) token_type else .Identifier;
    }

    fn identifier(self: *Lexer) Token {
        while (!self.isAtEnd() and (isAlpha(self.peek()) or isDigit(self.peek()))) {
            _ = self.advance();
        }

        return self.makeToken(self.identifierType());
    }

    inline fn currentLength(self: *Lexer) usize {
        return @intFromPtr(self.current) - @intFromPtr(self.start);
    }

    fn number(self: *Lexer) Token {
        if (self.start[0] == '0' and !self.isAtEnd() and (self.peek() == 'x' or self.peek() == 'X')) {
            _ = self.advance();
            if (!isHexDigit(self.peek())) {
                return self.errorToken("Invalid hexadecimal literal.");
            }
            while (!self.isAtEnd() and (isHexDigit(self.peek()) or self.peek() == '_')) {
                if (self.peek() == '_') {
                    if (!isHexDigit(self.peek())) {
                        return self.errorToken("Invalid underscore in hex literal.");
                    }
                } else {
                    _ = self.advance();
                }
            }
            return self.makeToken(.Number);
        }

        while (!self.isAtEnd() and (isDigit(self.peek()) or self.peek() == '_')) {
            if (self.peek() == '_') {
                _ = self.advance();
                if (!isDigit(self.peek())) {
                    return self.errorToken("Invalid underscore in number.");
                }
            } else {
                _ = self.advance();
            }
        }

        // Factorial part: 3.14
        if (!self.isAtEnd() and self.peek() == '.' and isDigit(self.peekNext())) {
            _ = self.advance();
            while (!self.isAtEnd() and (isDigit(self.peek()))) {
                if (self.peek() == '_') {
                    _ = self.advance();
                    if (!isDigit(self.peek())) {
                        return self.errorToken("Invalid underscore in number.");
                    }
                } else {
                    _ = self.advance();
                }
            }
        }

        // Exponent: 1.5e-10, 2E+3
        if (!self.isAtEnd() and (self.peek() == 'e' or self.peek() == 'E')) {
            _ = self.advance();
            if (!self.isAtEnd() and (self.peek() == '+' or self.peek() == '-')) {
                _ = self.advance();
            }
            if (!isDigit(self.peek())) {
                return self.errorToken("Expected digit after exponent");
            }
            while (!self.isAtEnd() and (isDigit(self.peek()) or self.peek() == '_')) {
                _ = self.advance();
            }
        }

        return self.makeToken(.Number);
    }

    pub fn string(self: *Lexer) Token {
        while (!self.isAtEnd() and self.peek() != '"') {
            if (self.peek() == '\\') {
                _ = self.advance();
                if (self.isAtEnd()) break;
                switch (self.peek()) {
                    'n', 't', 'r', '\\', '"', '0' => _ = self.advance(),
                    else => return self.errorToken("Invalid escape sequence."),
                }
            } else {
                _ = self.advance();
            }
        }

        if (self.isAtEnd()) return self.errorToken("Unterminated string");
        _ = self.advance();
        return self.makeToken(.String);
    }

    fn skipWhiteSpaceAndComment(self: *Lexer) void {
        while (!self.isAtEnd()) {
            switch (self.peek()) {
                ' ', '\t', '\r', '\n' => {
                    _ = self.advance();
                },
                '/' => {
                    if (self.peekNext() == '/') {
                        // commentaire ligne //
                        while (!self.isAtEnd() and self.peek() != '\n') {
                            _ = self.advance();
                        }
                    } else if (self.peekNext() == '*') {
                        // commentaire bloc /* */
                        _ = self.advance();
                        _ = self.advance();
                        while (!self.isAtEnd()) {
                            if (self.peek() == '*' and self.peekNext() == '/') {
                                _ = self.advance();
                                _ = self.advance();
                                break;
                            }
                            _ = self.advance();
                        }
                    } else {
                        return;
                    }
                },
                else => return,
            }
        }
    }

    inline fn isAtEnd(self: *Lexer) bool {
        return @intFromPtr(self.current) >= @intFromPtr(self.end);
    }

    inline fn peek(self: *Lexer) u8 {
        if (self.isAtEnd()) return 0;
        return self.current[0];
    }

    inline fn peekNext(self: *Lexer) u8 {
        const next_ptr = self.current + 1;
        if (@intFromPtr(next_ptr) >= @intFromPtr(self.end)) return 0;
        return next_ptr[0];
    }

    inline fn advance(self: *Lexer) u8 {
        const c = self.current[0];
        if (c == '\n') {
            self.line += 1;
            self.column = 1;
        } else {
            self.column += 1;
        }
        self.current += 1;
        return c;
    }

    inline fn match(self: *Lexer, expected: u8) bool {
        if (self.isAtEnd()) return false;
        if (self.current[0] != expected) return false;
        _ = self.advance();

        return true;
    }

    inline fn errorToken(self: *Lexer, msg: []const u8) Token {
        return .{
            .type = .Error,
            .start = msg.ptr,
            .length = msg.len,
            .line = self.line,
            .column = self.column,
        };
    }
};

inline fn isAlpha(c: u8) bool {
    return (c >= 'a' and c <= 'z') or
        (c >= 'A' and c <= 'Z') or
        c == '_';
}

inline fn isDigit(c: u8) bool {
    return c >= '0' and c <= '9';
}

inline fn isHexDigit(c: u8) bool {
    return isDigit(c) or
        (c >= 'a' and c <= 'f') or
        (c >= 'A' and c <= 'F');
}

pub fn tokenize(source: []const u8, allocator: std.mem.Allocator) std.mem.Allocator.Error![]Token {
    var list = std.ArrayList(Token).empty;
    errdefer list.deinit(allocator);

    var lex = Lexer.init(source);

    while (true) {
        const tok = lex.next();
        try list.append(allocator, tok);
        if (tok.type == .Eof) break;
    }

    return list.toOwnedSlice(allocator);
}

pub fn tokenTypeName(t: TokenType) []const u8 {
    return switch (t) {
        .Number => "NUMBER",
        .String => "STRING",
        .Identifier => "IDENTIFIER",
        .True => "TRUE",
        .False => "FALSE",
        .Nil => "NIL",
        .Fn => "FN",
        .Return => "RETURN",
        .If => "IF",
        .Elif => "ELIF",
        .Else => "ELSE",
        .While => "WHILE",
        .For => "FOR",
        .And => "AND",
        .Or => "OR",
        .Not => "NOT",
        .Break => "BREAK",
        .Continue => "CONTINUE",
        .Print => "PRINT",
        .Plus => "PLUS",
        .Minus => "MINUS",
        .Star => "STAR",
        .Slash => "SLASH",
        .Modulo => "MODULO",
        .PlusEqual => "PLUS_EQUAL",
        .MinusEqual => "MINUS_EQUAL",
        .StarEqual => "STAR_EQUAL",
        .SlashEqual => "SLASH_EQUAL",
        .ModuloEqual => "MODULO_EQUAL",
        .EqualEqual => "EQUAL_EQUAL",
        .Bang => "BANG",
        .BangEqual => "BANG_EQUAL",
        .Less => "LESS",
        .LessEqual => "LESS_EQUAL",
        .Greater => "GREATER",
        .GreaterEqual => "GREATER_EQUAL",
        .BitAnd => "BIT_AND",
        .BitOr => "BIT_OR",
        .BitXor => "BIT_XOR",
        .BitNot => "BIT_NOT",
        .ShiftLeft => "SHIFT_LEFT",
        .ShiftRight => "SHIFT_RIGHT",
        .Equal => "EQUAL",
        .Define => "DEFINE",
        .LeftParen => "LEFT_PAREN",
        .RightParen => "RIGHT_PAREN",
        .LeftBrace => "LEFT_BRACE",
        .RightBrace => "RIGHT_BRACE",
        .LeftBracket => "LEFT_BRACKET",
        .RightBracket => "RIGHT_BRACKET",
        .Comma => "COMMA",
        .Semicolon => "SEMICOLON",
        .Dot => "DOT",
        .DotDot => "DOT_DOT",
        .Colon => "COLON",
        .Pipe => "PIPE",
        .Underscore => "UNDERSCORE",
        .Error => "ERROR",
        .Eof => "EOF",
    };
}

pub fn printToken(token: Token) void {
    std.debug.print(" s: {s} (line {d}, col {d})\n", .{
        tokenTypeName(token.type),
        token.line,
        token.column,
    });
}

pub fn debugTokens(source: []const u8) void {
    std.debug.print("=== ORUS TOKEN DEBUG ===\n", .{});
    var lexer = Lexer.init(source);
    while (true) {
        const tok = lexer.next();
        printToken(tok);
        if (tok.type == .Eof or tok.type == .Error) break;
    }
    std.debug.print("=== END ===", .{});
}
