#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "RxLexer.h"
#include "RxParser.h"
#include "ast.h"
#include "semantic/semantic_checker.h"

int main(int argc, char *argv[]) {
    // 1. 检查命令行参数
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <source-file>\n";
        return 1;
    }

    const std::string filename = argv[1];

    // 2. 打开 Rust / Rx 源文件
    std::ifstream inputFile(filename);

    if (!inputFile.is_open()) {
        std::cerr << "Cannot open file: " << filename << '\n';
        return 1;
    }

    try {
        // 3. 文件内容 -> ANTLR 字符输入流
        antlr4::ANTLRInputStream input(inputFile);

        // 4. Lexer：字符 -> Token
        rx::RxLexer lexer(&input);

        // 5. Token 缓冲区
        antlr4::CommonTokenStream tokens(&lexer);

        // 6. Parser：Token -> Parse Tree
        rx::RxParser parser(&tokens);

        // 7. 从 grammar 的入口 crate 开始解析
        rx::RxParser::CrateContext *tree = parser.crate();

        // 8. 如果 parser 发现语法错误，就不继续构造 AST
        if (parser.getNumberOfSyntaxErrors() != 0) {
            std::cerr << "Parsing failed with "
                      << parser.getNumberOfSyntaxErrors()
                      << " syntax error(s).\n";
            return 1;
        }

        // 9. Parse Tree -> AST
        rx::ast::CrateNode ast(tree);

        // 10. Optional AST dump for debugging; disabled during test runs.
        if (std::getenv("RX_DUMP_AST") != nullptr) {
            ast.dump(std::cout, 0);
        }

        // 11. Semantic analysis
        rx::semantic::SemanticChecker checker;
        if (!checker.check(ast)) {
            for (const auto &error : checker.errors()) {
                std::cerr << "Semantic error: " << error << '\n';
            }
            return 1;
        }

    } catch (const std::exception &e) {
        // Internal failures must not look like a normal semantic rejection.
        std::cerr << "Internal error: " << e.what() << '\n';
        return 2;
    }

    return 0;
}