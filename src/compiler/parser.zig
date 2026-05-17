const std = @import("std");
const Token = @import("lexer.zig").Token;
const TokenType = @import("lexer.zig").TokenType;
const diagnostic = @import("../diagnostic.zig");

pub const ParseError = error{
    UnexpectedToken,
    ExpectedExpression,
};

pub const Expr = union(enum) {
    Number: f64,
    Bool: bool,
    Nil: void,
    String: []const u8,
    Variable: []const u8,
    Unary: struct {
        op: TokenType,
        right: *Expr,
    },
    Binary: struct {
        left: *Expr,
        op: TokenType,
        right: *Expr,
    },
};

pub const Assign = struct {
    name: []const u8,
    op: TokenType,
    value: *Expr,
};

pub const Stmt = union(enum) {
    Print: []*Expr,
    Assign: Assign,
};

pub const Parser = struct {
    tokens: []Token,
    current: usize,
    allocator: std.mem.Allocator,
    source: []const u8,

    pub fn init(tokens: []Token, allocator: std.mem.Allocator, source: []const u8) Parser {
        return .{
            .tokens = tokens,
            .current = 0,
            .allocator = allocator,
            .source = source,
        };
    }

    pub fn parse(self: *Parser) ![]Stmt {
        var staments = std.ArrayList(Stmt).empty;

        while (!self.isAtEnd()) {
            try staments.append(self.allocator, try self.parseStatement());
        }

        return staments.toOwnedSlice(self.allocator);
    }

    pub fn parseStatement(self: *Parser) !Stmt {
        if (self.match(.Print)) {
            return self.parsePrint();
        } else {
            return self.parseAssign();
        }
    }

    pub fn parsePrint(self: *Parser) !Stmt {
        _ = try self.expect("Expected '('", .LeftParen);
        var args = std.ArrayList(*Expr).empty;

        if (!self.check(.RightParen)) {
            try args.append(self.allocator, try self.parseExpression());
            while (self.match(.Comma)) {
                try args.append(self.allocator, try self.parseExpression());
            }
        }

        _ = try self.expect("Expected ')'", .RightParen);
        _ = try self.expect("Expected ';' After print statement", .Semicolon);

        return Stmt{ .Print = try args.toOwnedSlice(self.allocator) };
    }

    pub fn parseAssign(self: *Parser) !Stmt {
        const name_token = try self.expect("Expected identifier", .Identifier);

        const op_type: TokenType = switch (self.peek().type) {
            .Define, .Equal, .PlusEqual, .MinusEqual, .StarEqual, .SlashEqual, .ModuloEqual => blk: {
                _ = self.advance();
                break :blk self.previous().type;
            },
            else => return self.errorAt(self.peek(), "Expected assignment operator."),
        };
        _ = self.previous();

        const value = try self.parseExpression();
        _ = try self.expect("Expected ';'", .Semicolon);

        return Stmt{ .Assign = .{
            .name = name_token.lexeme(),
            .op = op_type,
            .value = value,
        } };
    }

    fn parseExpression(self: *Parser) (ParseError || std.mem.Allocator.Error || std.fmt.ParseFloatError)!*Expr {
        return self.parseComparison();
    }

    fn parseComparison(self: *Parser) !*Expr {
        var left = try self.parseAddition();
        while (self.match(.EqualEqual) or
            self.match(.LessEqual) or self.match(.GreaterEqual) or
            self.match(.BangEqual) or self.match(.Less) or self.match(.Greater))
        {
            const op = self.previous().type;
            const right = try self.parseAddition();
            left = try self.makeBinary(left, op, right);
        }

        return left;
    }

    fn parseAddition(self: *Parser) !*Expr {
        var expr = try self.parseMultiplication();
        while (self.match(.Minus) or self.match(.Plus)) {
            const op = self.previous().type;
            const right = try self.parseMultiplication();
            expr = try self.makeBinary(expr, op, right);
        }
        return expr;
    }

    fn parseMultiplication(self: *Parser) !*Expr {
        var expr = try self.parseUnary();
        while (self.match(.Star) or self.match(.Slash) or self.match(.Modulo)) {
            const op = self.previous().type;
            const right = try self.parseUnary();
            expr = try self.makeBinary(expr, op, right);
        }
        return expr;
    }

    fn parseUnary(self: *Parser) !*Expr {
        if (self.match(.Minus)) {
            const right = try self.parseUnary();
            const node = try self.makeUnary(right, .Minus);
            return node;
        } else if (self.match(.BitNot)) {
            const right = try self.parseUnary();
            const node = try self.makeUnary(right, .BitNot);
            return node;
        }

        return self.parsePrimary();
    }

    pub fn parsePrimary(self: *Parser) !*Expr {
        if (self.match(.Number)) {
            return self.makeNumber(self.previous());
        }

        if (self.match(.Identifier)) {
            return self.makeVariable(self.previous());
        }

        if (self.match(.String)) {
            return self.makeString(self.previous());
        }

        if (self.match(.Nil)) {
            const node = try self.allocator.create(Expr);
            node.* = Expr{ .Nil = {} };
            return node;
        }

        if (self.match(.True)) {
            const node = try self.allocator.create(Expr);
            node.* = Expr{ .Bool = true };
            return node;
        }

        if (self.match(.False)) {
            const node = try self.allocator.create(Expr);
            node.* = Expr{ .Bool = false };
            return node;
        }

        if (self.match(.LeftParen)) {
            const expr = try self.parseExpression();
            _ = try self.expect("Expected ')'", .RightParen);
            return expr;
        }

        return self.errorAt(self.peek(), "Unexpected Token");
    }

    fn makeBinary(self: *Parser, left: *Expr, op: TokenType, right: *Expr) !*Expr {
        const node = try self.allocator.create(Expr);

        node.* = Expr{ .Binary = .{
            .left = left,
            .op = op,
            .right = right,
        } };
        return node;
    }

    fn makeUnary(self: *Parser, right: *Expr, op: TokenType) !*Expr {
        const node = try self.allocator.create(Expr);

        node.* = Expr{ .Unary = .{
            .op = op,
            .right = right,
        } };
        return node;
    }

    fn makeNumber(self: *Parser, token: Token) !*Expr {
        const node = try self.allocator.create(Expr);
        const value = try std.fmt.parseFloat(f64, token.lexeme());
        node.* = Expr{ .Number = value };
        return node;
    }

    fn makeVariable(self: *Parser, token: Token) !*Expr {
        const node = try self.allocator.create(Expr);
        node.* = Expr{ .Variable = token.lexeme() };
        return node;
    }

    fn makeString(self: *Parser, token: Token) !*Expr {
        const node = try self.allocator.create(Expr);
        node.* = Expr{ .String = token.lexeme()[1 .. token.lexeme().len - 1] };
        return node;
    }

    fn expect(self: *Parser, comptime msg: []const u8, t: TokenType) !Token {
        if (self.check(t)) {
            return self.advance();
        } else {
            return self.errorAt(self.peek(), msg);
        }
    }

    fn errorAt(self: *Parser, token: Token, comptime msg: []const u8) ParseError {
        diagnostic.report(self.source, token, msg);
        return ParseError.UnexpectedToken;
    }

    // Returns the current token without advancing
    fn peek(self: *Parser) Token {
        return self.tokens[self.current];
    }

    // Checks if the current token is Eof
    fn isAtEnd(self: *Parser) bool {
        return self.peek().type == .Eof;
    }

    // Return the current token and advances by 1 token
    fn advance(self: *Parser) Token {
        if (!self.isAtEnd()) self.current += 1;
        return self.previous();
    }

    // Returns the previous token just before the current token
    fn previous(self: *Parser) Token {
        return self.tokens[self.current - 1];
    }

    // True if the current token is of type t without advancing
    fn check(self: *Parser, t: TokenType) bool {
        if (self.isAtEnd()) return false;
        return self.peek().type == t;
    }

    // If the current token is t, advance and return true else return false
    fn match(self: *Parser, t: TokenType) bool {
        if (self.check(t)) {
            _ = self.advance();
            return true;
        }
        return false;
    }
};
