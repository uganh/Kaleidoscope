%require "3.2"
%language "c++"
%expect 0

%define api.value.type variant
%define api.token.constructor

%parse-param {llvm::Module &Module}
%parse-param {llvm::IRBuilder<> &Builder}
%parse-param {llvm::legacy::FunctionPassManager &PassManager}
%parse-param {SymbolTable &Symtab}

%token DEF
%token EXTERN
%token IF
%token THEN
%token ELSE
%token <std::string> IDENTIFIER
%token <double> NUMBER

/* The lowest priority used to solve trivial shift-reduce conflicts */
%precedence DEFAULT

%nonassoc '<'
%left '+' '-'
%left '*'

%nterm <std::unique_ptr<Expr>> Expr
%nterm <std::vector<std::unique_ptr<Expr>>> ExprList
%nterm <std::unique_ptr<Prototype>> Prototype
%nterm <std::vector<std::string>> ParameterList OptionalParameterList

%code requires {
#include "Kaleidoscope.h"
}

%code {
namespace yy {
static bool Restart = true;
static int LastChar = ' ';

parser::symbol_type yylex(void) {
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

  int ThisChar = LastChar;
  LastChar = std::cin.get();
  return parser::symbol_type(ThisChar);
}

void parser::error(const std::string &msg) {
  std::cerr << msg << std::endl;
  // Drop reminder
  while (LastChar != EOF && LastChar != '\n') {
    LastChar = std::cin.get();
  }
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
  | IDENTIFIER '(' ExprList ')' {
      $$ = std::make_unique<CallExpr>($1, std::move($3));
    }
  | '(' Expr ')' {
      $$ = std::move($2);
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
  | IF Expr THEN Expr ELSE Expr %prec DEFAULT {
      $$ = std::make_unique<IfExpr>(std::move($2), std::move($4), std::move($6));
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

%%
