#include <iostream>
#include <vector>
#include <string>


namespace Diagnostic{

    enum class DiagLevel {
        LEXER,
        PARSER
    };

    inline const char* to_string(DiagLevel level) {                                                                                  
        switch (level) {
            case DiagLevel::LEXER:  return "Lexer";                                                                           
            case DiagLevel::PARSER: return "Parser";
        }
        return "Unknown";
    }

    struct Location{
        int line;
        int column;
    };

    struct Diagnostic{
        DiagLevel level;
        Location location;
        std::string message; 
        
        void print() const{
            std::cerr<<to_string(level)<< " error at line "<< location.line<<" and column "<< location.column<<". msg: "<<message<<std::endl;
        }
    };

    class DiagnosticEngine{
        std::vector<Diagnostic> errors;
    public:
        DiagnosticEngine() = default;
        DiagnosticEngine(const DiagnosticEngine&) = delete;                                                                   
        DiagnosticEngine& operator=(const DiagnosticEngine&) = delete;
        void report(DiagLevel level, Location loc, std::string msg);
        bool hasErrors() const;
        void print();
    };

}