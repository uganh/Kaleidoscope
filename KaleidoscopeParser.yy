%require "3.2"
%language "c++"
%expect 0

%define api.value.type variant
%define api.token.constructor

%parse-param {llvm::Module &Module}
%parse-param {llvm::IRBuilder<> &Builder}
%parse-param {llvm::legacy::FunctionPassManager &PassManager}
%parse-param {SymbolTable &Symtab}

%lex-param {const SymbolTable &Symtab}

%token DEF
%token EXTERN
%token IF
%token THEN
%token ELSE
%token FOR
%token IN
%token UNARY
%token BINARY
%token VAR
%token <std::string> IDENTIFIER
%token <double> NUMBER
%token <char> OPERATOR

/* User-defined operators */
%token <char> OPERATOR1
%token <char> OPERATOR2
%token <char> OPERATOR3
%token <char> OPERATOR4
%token <char> OPERATOR5
%token <char> OPERATOR6
%token <char> OPERATOR7
%token <char> OPERATOR8
%token <char> OPERATOR9
%token <char> OPERATOR10

/* The lowest priority used to solve trivial shift-reduce conflicts */
%precedence DEFAULT

%left OPERATOR1 
%left OPERATOR2 '='
%left OPERATOR3 '<'
%left OPERATOR4
%left OPERATOR5
%left OPERATOR6 '+' '-'
%left OPERATOR7
%left OPERATOR8 '*'
%left OPERATOR9
%left OPERATOR10

/* Define the precedence of the unary operator */
%precedence UNARY

%nterm <std::unique_ptr<Expr>> Expr
%nterm <std::vector<std::unique_ptr<Expr>>> ExprList OptionalExprList
%nterm <std::unique_ptr<Prototype>> Prototype
%nterm <std::vector<std::string>> ParameterList OptionalParameterList
%nterm <char> Operator
%nterm <std::vector<std::pair<std::string, std::unique_ptr<Expr>>>> VarDefs
%nterm <std::pair<std::string, std::unique_ptr<Expr>>> VarDef

%code requires {
#include "Kaleidoscope.h"
}

%code {
namespace yy {
static bool Restart = true;
static int LastChar = ' ';

parser::symbol_type yylex(const SymbolTable &Symtab) {
  // Available user-defined operators
  const static std::string Operators = "!&./:>|";

  while (isspace(LastChar)) {
    if (LastChar == '\n') {
      if (Restart) {
        std::cout << ">>> " << std::flush;
      } else {
        std::cout << "... " << std::flush;
      }
    }
    LastChar = std::cin.get();
  }

  Restart = false;

  if (std::isalpha(LastChar)) {
    std::string Text;
    do {
      Text += LastChar;
    } while (isalnum(LastChar = getchar()));

    if (Text == "def") {
      return parser::make_DEF();
    }

    if (Text == "extern") {
      return parser::make_EXTERN();
    }

    if (Text == "if") {
      return parser::make_IF();
    }

    if (Text == "then") {
      return parser::make_THEN();
    }

    if (Text == "else") {
      return parser::make_ELSE();
    }

    if (Text == "for") {
      return parser::make_FOR();
    }

    if (Text == "in") {
      return parser::make_IN();
    }

    if (Text == "unary") {
      return parser::make_UNARY();
    }

    if (Text == "binary") {
      return parser::make_BINARY();
    }

    if (Text == "var") {
      return parser::make_VAR();
    }

    return parser::make_IDENTIFIER(Text);
  }

  bool HasDot = false;
  if (isdigit(LastChar) || LastChar == '.') {
    std::string NumStr;
    do {
      NumStr += LastChar;
      HasDot |= LastChar == '.';
      LastChar = std::cin.get();
    } while (isdigit(LastChar) || (LastChar == '.' && !HasDot));
    return parser::make_NUMBER(strtod(NumStr.c_str(), NULL));
  }

  if (LastChar == '#') {
    do {
      LastChar = std::cin.get();
    } while (LastChar != EOF && LastChar != '\n');
  
    if (LastChar != EOF) {
      // LastChar is '\n'
      LastChar = std::cin.get();
    }
  }

  if (LastChar == EOF) {
    return parser::make_YYEOF();
  }

  char ThisChar = static_cast<char>(LastChar);
  LastChar = std::cin.get();

  if (Operators.find(ThisChar) != std::string::npos) {
    switch (Symtab.getTokPrecedence(ThisChar)) {
      case 1: return parser::make_OPERATOR1(ThisChar);
      case 2: return parser::make_OPERATOR2(ThisChar);
      case 3: return parser::make_OPERATOR3(ThisChar);
      case 4: return parser::make_OPERATOR4(ThisChar);
      case 5: return parser::make_OPERATOR5(ThisChar);
      case 6: return parser::make_OPERATOR6(ThisChar);
      case 7: return parser::make_OPERATOR7(ThisChar);
      case 8: return parser::make_OPERATOR8(ThisChar);
      case 9: return parser::make_OPERATOR9(ThisChar);
      case 10: return parser::make_OPERATOR10(ThisChar);
      default: return parser::make_OPERATOR(ThisChar);
    }
  }

  return parser::symbol_type(ThisChar);
}

void parser::error(const std::string &msg) {
  std::cerr << msg << std::endl;
  // Drop reminder
  while (LastChar != EOF && LastChar != '\n') {
    LastChar = std::cin.get();
  }
  Restart = true;
}
} // namespace yy
}

%%

Program:
    %empty
  | StatementList
  ;

StatementList:
    Statement {
      Restart = true;
    }
  | StatementList Statement {
      Restart = true;
    }
  ;

Statement:
    ';'
  | Expr ';' {
      auto Proto = std::make_unique<Prototype>(
        "__anon_expr", std::vector<std::string>());
      auto Func = std::make_unique<Function>(std::move(Proto), std::move($1));
      auto *ir = Func->codegen(Module, Builder, PassManager, Symtab);
      std::cout << "Read top-level expression:" << std::endl;
      ir->print(llvm::outs());

      // Remove the anonymous expression
      ir->eraseFromParent();
    }
  | EXTERN Prototype ';' {
      auto *ir = $2->codegen(Module, Builder, Symtab);
      std::cout << "Read extern:" << std::endl;
      ir->print(llvm::outs());
    }
  | DEF Prototype Expr ';' {
      auto Func = std::make_unique<Function>(std::move($2), std::move($3));
      auto *ir = Func->codegen(Module, Builder, PassManager, Symtab);
      std::cout << "Read function definition:" << std::endl;
      ir->print(llvm::outs());
    }
  ;

Prototype:
    IDENTIFIER '(' OptionalParameterList ')' {
      $$ = std::make_unique<Prototype>($1, std::move($3));
    }
  | UNARY Operator '(' IDENTIFIER ')' {
      $$ = std::make_unique<Prototype>($2, $4);
    }
  | BINARY Operator '(' IDENTIFIER IDENTIFIER ')' {
      /* TODO: Undo operator define if error occurs */
      Symtab.define($2, 4);
      $$ = std::make_unique<Prototype>($2, $4, $5);
    }
  | BINARY Operator NUMBER '(' IDENTIFIER IDENTIFIER ')' {
      unsigned Precedence = static_cast<unsigned>($3);
      if (Precedence < 1 || Precedence > 10) {
        parser::error("Invalid precedence: must be 1..10");
        YYERROR;
      }
      /* TODO: Undo operator define if error occurs */
      Symtab.define($2, Precedence);
      $$ = std::make_unique<Prototype>($2, $5, $6);
    }
  ;

OptionalParameterList:
    %empty {
      $$ = std::vector<std::string>();
    }
  | ParameterList {
      $$ = std::move($1);
    }
  ;

ParameterList:
    IDENTIFIER {
      $$ = std::vector<std::string> { std::move($1) };
    }
  | ParameterList IDENTIFIER {
      $$ = std::move($1);
      $$.emplace_back(std::move($2));
    }
  ;

Expr:
    NUMBER {
      $$ = std::make_unique<Constant>($1);
    }
  | IDENTIFIER {
      $$ = std::make_unique<Variable>($1);
    }
  | IDENTIFIER '(' OptionalExprList ')' {
      $$ = std::make_unique<CallExpr>($1, std::move($3));
    }
  | '(' Expr ')' {
      $$ = std::move($2);
    }
  | Operator Expr %prec UNARY {
      $$ = std::make_unique<UnaryExpr>($1, std::move($2));
    }
  | IDENTIFIER '=' Expr {
      $$ = std::make_unique<BinaryExpr>(
        '=', std::make_unique<Variable>($1), std::move($3));
    }
  | Expr '<' Expr {
      $$ = std::make_unique<BinaryExpr>('<', std::move($1), std::move($3));
    }
  | Expr '+' Expr {
      $$ = std::make_unique<BinaryExpr>('+', std::move($1), std::move($3));
    }
  | Expr '-' Expr {
      $$ = std::make_unique<BinaryExpr>('-', std::move($1), std::move($3));
    }
  | Expr '*' Expr {
      $$ = std::make_unique<BinaryExpr>('*', std::move($1), std::move($3));
    }
  | Expr OPERATOR1 Expr {
      $$ = std::make_unique<BinaryExpr>($2, std::move($1), std::move($3));
    }
  | Expr OPERATOR2 Expr {
      $$ = std::make_unique<BinaryExpr>($2, std::move($1), std::move($3));
    }
  | Expr OPERATOR3 Expr {
      $$ = std::make_unique<BinaryExpr>($2, std::move($1), std::move($3));
    }
  | Expr OPERATOR4 Expr {
      $$ = std::make_unique<BinaryExpr>($2, std::move($1), std::move($3));
    }
  | Expr OPERATOR5 Expr {
      $$ = std::make_unique<BinaryExpr>($2, std::move($1), std::move($3));
    }
  | Expr OPERATOR6 Expr {
      $$ = std::make_unique<BinaryExpr>($2, std::move($1), std::move($3));
    }
  | Expr OPERATOR7 Expr {
      $$ = std::make_unique<BinaryExpr>($2, std::move($1), std::move($3));
    }
  | Expr OPERATOR8 Expr {
      $$ = std::make_unique<BinaryExpr>($2, std::move($1), std::move($3));
    }
  | Expr OPERATOR9 Expr {
      $$ = std::make_unique<BinaryExpr>($2, std::move($1), std::move($3));
    }
  | Expr OPERATOR10 Expr {
      $$ = std::make_unique<BinaryExpr>($2, std::move($1), std::move($3));
    }
  | IF Expr THEN Expr ELSE Expr %prec DEFAULT {
      $$ = std::make_unique<IfExpr>(std::move($2), std::move($4), std::move($6));
    }
  | FOR IDENTIFIER '=' Expr ',' Expr IN Expr %prec DEFAULT {
      $$ = std::make_unique<ForExpr>($2, std::move($4), std::move($6), nullptr, std::move($8));
    }
  | FOR IDENTIFIER '=' Expr ',' Expr ',' Expr IN Expr %prec DEFAULT {
      $$ = std::make_unique<ForExpr>($2, std::move($4), std::move($6), std::move($8), std::move($10));
    }
  | VAR VarDefs IN Expr %prec DEFAULT {
      $$ = std::make_unique<VarExpr>(std::move($2), std::move($4));
    }
  ;

OptionalExprList:
    %empty {
      $$ = std::vector<std::unique_ptr<Expr>>();
    }
  | ExprList {
      $$ = std::move($1);
    }
  ;

ExprList:
    Expr {
      $$ = std::vector<std::unique_ptr<Expr>>();
      $$.emplace_back(std::move($1));
    }
  | ExprList ',' Expr {
      $$ = std::move($1);
      $$.emplace_back(std::move($3));
    }
  ;

VarDefs:
    VarDef {
      $$ = std::vector<std::pair<std::string, std::unique_ptr<Expr>>>();
      $$.emplace_back(std::move($1));
    }
  | VarDefs ',' VarDef {
      $$ = std::move($1);
      $$.emplace_back(std::move($3));
    }
  ;

VarDef:
    IDENTIFIER {
      $$ = std::make_pair($1, nullptr);
    }
  | IDENTIFIER '=' Expr {
      $$ = std::make_pair($1, std::move($3));
    }
  ;

Operator:
    OPERATOR
  | OPERATOR1
  | OPERATOR2
  | OPERATOR3
  | OPERATOR4
  | OPERATOR5
  | OPERATOR6
  | OPERATOR7
  | OPERATOR8
  | OPERATOR9
  | OPERATOR10
  ;

%%
