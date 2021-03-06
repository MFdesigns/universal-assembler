// ======================================================================== //
// Copyright 2020 Michel Fäh
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// ======================================================================== //

#include "parser.hpp"
#include "asm/asm.hpp"
#include "asm/encoding.hpp"
#include "cli.hpp"
#include <cfloat>
#include <cmath>
#include <iomanip>
#include <iostream>

/**
 * Returns the register type
 * @param regId Register id
 * @return Type of register
 */
RegisterType getRegisterType(uint8_t regId) {
    if (regId >= 0 && regId <= 0x14 && regId != 0x4) {
        return RegisterType::INTEGER;
    }
    return RegisterType::FLOAT;
}

/**
 * Checks if an integer can fit into given type assmuning that type is less than
 * 64-bit
 * @param num Integer to be checked
 * @param type Target type
 * @return On success returns true otherwise false
 */
bool checkIntWidth(uint64_t num, uint8_t type, bool isSigned) {
    bool fits = false;

    if (isSigned) {
        num = std::abs(static_cast<int64_t>(num));
    }

    switch (type) {
    case UVM_TYPE_I8:
        if (num <= 0xFF) {
            fits = true;
        }
        break;
    case UVM_TYPE_I16:
        if (num <= 0xFFFF) {
            fits = true;
        }
        break;
    case UVM_TYPE_I32:
        if (num <= 0xFFFFFFFF) {
            fits = true;
        }
        break;
    case UVM_TYPE_I64:
        fits = true;
        break;
    }
    return fits;
}

/**
 * Checks if a float can fit into given type assmuning that type is less than
 * 64-bit
 * @param num Integer to be checked
 * @param type Target type
 * @return On success returns true otherwise false
 */
bool checkFloatWidth(double num, uint8_t type) {
    bool fits = false;
    switch (type) {
    case UVM_TYPE_F32:
        if (num <= FLT_MAX) {
            fits = true;
        }
        break;
    case UVM_TYPE_F64:
        if (num <= DBL_MAX) {
            fits = true;
        }
        break;
    }
    return fits;
}

/**
 * Converts a string to an integer
 * @param str String to be converted
 * @param num [out] Converted integer
 * @return If integer fits into a 64-bit integer returns true otherwhise false
 */
bool strToInt(std::string& str, uint64_t& num) {
    int32_t base = 10;
    if (str.size() >= 3) {
        if (str[0] == '0' && str[1] == 'x') {
            base = 16;
        }
    }
    // Warning: this only works with unsigned numbers and  does not handle
    // signed numbers
    // If string number is bigger than 64-bit exception is thrown.
    try {
        num = std::stoull(str, 0, base);
    } catch (const std::out_of_range) {
        return false;
    }
    return true;
}

/**
 * Converts a string to a floating-point value
 * @param str String to be converted
 * @param num [out] Converted float
 * @return If a floating-point value fits into a 64-bit float returns true
 * otherwhise false
 */
bool strToFP(std::string& str, double& num) {
    try {
        num = std::stod(str, nullptr);
    } catch (const std::out_of_range& e) {
        return false;
    } catch (const std::invalid_argument& e) {
        return false;
    }
    return true;
}

/**
 * Constructs a new Parser
 * @param instrDefs Pointer to instruction definitons
 * @param src Pointer to the source file
 * @param tokens Pointer to the token array
 * @param global [out] Pointer to the Global AST node
 * @param funcDefs [out] Pointer to the FuncDefLookup
 */
Parser::Parser(std::vector<InstrDefNode>* instrDefs,
               SourceFile* src,
               const std::vector<Token>* tokens,
               ASTFileNode* fileNode,
               std::vector<LabelDefLookup>* funcDefs,
               std::vector<VarDeclaration>* varDecls)
    : InstrDefs(instrDefs), Src(src), Tokens(tokens), FileNode(fileNode),
      LabelDefs(funcDefs), VarDecls(varDecls){};

/**
 * Returns token at current Cursor and increases the Cursor
 * @return Pointer to current Token, if Cursor is at the end will always return
 * the last token
 */
Token* Parser::eatToken() {
    const Token* tok = nullptr;
    if (Cursor < Tokens->size()) {
        // Because Cursor starts at index 0 return current token before
        // increasing the cursor
        tok = &(*Tokens)[Cursor];
        Cursor++;
    } else {
        tok = &Tokens->back();
    }
    return const_cast<Token*>(tok);
}

/**
 * Return the next Token without increasing the Cursor
 * @return Pointer to current Token, if Cursor is at the end will always return
 * the last token
 */
Token* Parser::peekToken() {
    const Token* tok = nullptr;
    if (Cursor >= Tokens->size()) {
        tok = &Tokens->back();
    } else {
        tok = &(*Tokens)[Cursor];
    }
    return const_cast<Token*>(tok);
}

/**
 * Skips token input until new line
 */
void Parser::skipLine() {
    Token* tok = eatToken();
    while (tok->Type != TokenType::END_OF_FILE && tok->Type != TokenType::EOL) {
        tok = eatToken();
    }
}

/**
 * Prints an error to the console
 * @param msg Pointer to error message string
 * @param tok Token to be displayed
 */
void Parser::printTokenError(const char* msg, Token& tok) {
    printError(Src, tok.Index, tok.Size, tok.LineRow, tok.LineCol, msg);
}

/**
 * Parses a string and replaces escaped characters and removes surrounding
 * quotes
 * @param inStr String containing the input
 * @param outStr [out] String containing the parsed output
 */
// TODO: RETURN ERROR VALUE
void Parser::parseStringEscape(std::string& inStr, std::string& outStr) {
    // If string only consists of "" (two double quotes) string is empty so just
    // return
    if (inStr.size() == 2) {
        return;
    }

    // Cursor starts at 1 to skip the first quote and size - 1 in loop to ignore
    // end quote
    size_t cursor = 1;
    while (cursor < inStr.size() - 1) {
        char c = inStr[cursor];
        char p = inStr[cursor + 1];

        if (c == '\\') {
            switch (p) {
            case 't':
                c = 0x09;
                break;
            case 'v':
                c = 0x0B;
                break;
            case '0':
                c = 0x00;
                break;
            case 'b':
                c = 0x08;
                break;
            case 'f':
                c = 0x0C;
                break;
            case 'n':
                c = 0x0A;
                break;
            case 'r':
                c = 0x0D;
                break;
            case '"':
                c = 0x22;
                break;
            case '\\':
                c = 0x5C;
                break;
            default:
                return;
                break;
            }
            cursor++;
        }

        outStr.push_back(c);
        cursor++;
    }
}

/**
 * Parses a register offset and appends it to the parent instruction node
 * @param instr Pointer to parent instruction node
 * @return On valid register offset returns true otherwise false
 */
bool Parser::parseRegOffset(Instruction* instr) {
    constexpr uint8_t RO_LAYOUT_NEG = 0b1000'0000;
    constexpr uint8_t RO_LAYOUT_POS = 0b0000'0000;
    RegisterOffset* regOff = new RegisterOffset();
    Token* t = eatToken();

    // Check if register offset is a variable offset e.g "[staticVar]"
    if (t->Type == TokenType::IDENTIFIER) {
        std::string idString;
        Src->getSubStr(t->Index, t->Size, idString);
        regOff->Var =
            new Identifier(t->Index, t->Size, t->LineRow, t->LineCol, idString);

        t = eatToken();
        // Closing bracket
        if (t->Type != TokenType::RIGHT_SQUARE_BRACKET) {
            printTokenError(
                "Expected closing bracket ] after variable reference", *t);
            return false;
        }
        instr->Params.push_back(regOff);
        return true;
    }

    if (t->Type == TokenType::REGISTER_DEFINITION) {
        if (getRegisterType(t->Tag) != RegisterType::INTEGER) {
            printTokenError("Expected integer register as base", *t);
            return false;
        }
        regOff->Base =
            new RegisterId(t->Index, t->Size, t->LineRow, t->LineCol, t->Tag);
        t = eatToken();
    } else {
        printTokenError("Expected register in register offset", *t);
        return false;
    }

    if (t->Type == TokenType::RIGHT_SQUARE_BRACKET) {
        regOff->Index = t->Index;
        regOff->LineRow = t->LineRow;
        regOff->LineCol = t->LineCol;
        regOff->Layout = RO_LAYOUT_IR;
        instr->Params.push_back(regOff);
        return true;
    } else if (t->Type == TokenType::PLUS_SIGN) {
        regOff->Layout |= RO_LAYOUT_POS;
    } else if (t->Type == TokenType::MINUS_SIGN) {
        regOff->Layout |= RO_LAYOUT_NEG;
    } else {
        printTokenError("Unexpected token in register offset", *t);
        return false;
    }

    t = eatToken();
    // <iR> +/- <i32>
    if (t->Type == TokenType::INTEGER_NUMBER) {
        Token* peek = peekToken();
        if (peek != nullptr && peek->Type == TokenType::RIGHT_SQUARE_BRACKET) {
            // Get int string and convert to an int
            std::string numStr;
            Src->getSubStr(t->Index, t->Size, numStr);
            uint64_t num = 0;
            if (!strToInt(numStr, num)) {
                printTokenError(
                    "Register offset immediate does not fit into 32-bit value",
                    *t);
                return false;
            }

            // <iR> + <i32> expects integer to have a maximum size of 32 bits
            // Check if the requirement is meet otherwise throw error
            if (num >> 32 != 0) {
                printTokenError(
                    "Register offset immediate does not fit into 32-bit value",
                    *t);
                return false;
            }
            regOff->Immediate.U32 = (uint32_t)num;

            // TODO: Register offset position is not correct
            regOff->Index = t->Index;
            regOff->LineRow = t->LineRow;
            regOff->LineCol = t->LineCol;
            regOff->Layout |= RO_LAYOUT_IR_INT;
            instr->Params.push_back(regOff);
            t = eatToken();
        } else {
            printTokenError(
                "Expected closing bracket after immediate offset inside "
                "register offset ]",
                *t);
            return false;
        }
    } else if (t->Type == TokenType::REGISTER_DEFINITION) {
        if (getRegisterType(t->Tag) != RegisterType::INTEGER) {
            printTokenError("Expected integer register as offset", *t);
            return false;
        }
        regOff->Offset =
            new RegisterId(t->Index, t->Size, t->LineRow, t->LineCol, t->Tag);
        t = eatToken();
        if (t->Type == TokenType::ASTERISK) {
            t = eatToken();
        } else {
            printTokenError("Expected * after offset inside register offset",
                            *t);
            return false;
        }

        std::string numStr;
        Src->getSubStr(t->Index, t->Size, numStr);
        uint64_t num = 0;
        if (!strToInt(numStr, num)) {
            printTokenError(
                "Register offset immediate does not fit into 16-bit value", *t);
            return false;
        }

        // <iR> +/- <iR> * <i16> expects integer to have a maximum size of 16
        // bits Check if the requirement is meet otherwise throw error
        if (num >> 16 != 0) {
            printTokenError(
                "Register offset immediate does not fit into 16-bit value", *t);
            return false;
        }
        regOff->Immediate.U16 = (uint16_t)num;
        t = eatToken();

        if (t->Type == TokenType::RIGHT_SQUARE_BRACKET) {
            // TODO: Register offset position is not correct
            regOff->Index = t->Index;
            regOff->LineRow = t->LineRow;
            regOff->LineCol = t->LineCol;
            regOff->Layout |= RO_LAYOUT_IR_IR_INT;
            instr->Params.push_back(regOff);
        } else {
            printTokenError("Expectd closing bracket after factor", *t);
            return false;
        }

    } else {
        printTokenError("Expected register or int number as offset", *t);
        return false;
    }
    return true;
}

/**
 * Builds AST for static section
 * @param sec Parent section
 * @return On valid input returns true otherwise false
 */
bool Parser::parseSectionVars(ASTSection* sec) {
    bool validSec = true;
    Token* tok = eatToken();

    // Ignore end of line
    if (tok->Type == TokenType::EOL) {
        tok = eatToken();
    }

    while (tok->Type != TokenType::RIGHT_CURLY_BRACKET) {
        Identifier* id = nullptr;
        TypeInfo* typeInfo = nullptr;
        ASTNode* val = nullptr;

        // Variable name
        if (tok->Type != TokenType::IDENTIFIER) {
            printTokenError("Expected static variable identifier", *tok);
            validSec = false;
            break;
        }

        std::string idName;
        Src->getSubStr(tok->Index, tok->Size, idName);
        id = new Identifier(tok->Index, tok->Size, tok->LineRow, tok->LineCol,
                            idName);

        // Colon
        tok = eatToken();
        if (tok->Type != TokenType::COLON) {
            printTokenError("Expected colon after variable identifier", *tok);
            validSec = false;
            break;
        }

        // Type
        tok = eatToken();
        if (tok->Type != TokenType::TYPE_INFO) {
            printTokenError("Expected type info in variable declaration", *tok);
            validSec = false;
            break;
        }
        typeInfo = new TypeInfo(tok->Index, tok->Size, tok->LineRow,
                                tok->LineCol, tok->Tag);

        // Equals
        tok = eatToken();
        if (tok->Type != TokenType::EQUALS_SIGN) {
            printTokenError(
                "Expected equals sign after type info in variable declaration",
                *tok);
            validSec = false;
            break;
        }
        tok = eatToken();

        Token* signToken = nullptr;
        char signTokenText = '\0';
        if (tok->Type == TokenType::PLUS_SIGN ||
            tok->Type == TokenType::MINUS_SIGN) {
            signToken = tok;
            Src->getChar(signToken->Index, signTokenText);
            tok = eatToken();
        }

        std::string tokString;
        Src->getSubStr(tok->Index, tok->Size, tokString);

        if (tok->Type == TokenType::STRING) {
            std::string parsedStr;
            parseStringEscape(tokString, parsedStr);
            ASTString* str = new ASTString(tok->Index, tok->Size, tok->LineRow,
                                           tok->LineCol, parsedStr);
            val = dynamic_cast<ASTNode*>(str);
        } else if (tok->Type == TokenType::INTEGER_NUMBER) {
            bool isSigned = false;
            // Check if sign token +/- is followed immediately by number. If so
            // insert +/- into token string to convert it to an number
            if (signToken != nullptr) {
                if (signToken->Index + 1 == tok->Index) {
                    tokString.insert(0, 1, signTokenText);
                    if (signToken->Type == TokenType::MINUS_SIGN) {
                        isSigned = true;
                    }
                } else {
                    printTokenError("Unexpected operator", *signToken);
                    validSec = false;
                    break;
                }
            }

            uint64_t intVal = 0;
            if (!strToInt(tokString, intVal)) {
                printTokenError("Integer does not fit into 64-bit value", *tok);
                validSec = false;
                break;
            }

            if (!checkIntWidth(intVal, typeInfo->DataType, isSigned)) {
                printTokenError("Integer does not fit into given type value",
                                *tok);
                validSec = false;
                break;
            }

            ASTInt* integer = new ASTInt(tok->Index, tok->Size, tok->LineRow,
                                         tok->LineCol, intVal, isSigned);
            val = dynamic_cast<ASTNode*>(integer);
        } else if (tok->Type == TokenType::FLOAT_NUMBER) {
            // Check if sign token +/- is followed immediately by number. If so
            // insert +/- into token string to convert it to an number
            if (signToken != nullptr) {
                if (signToken->Index + 1 == tok->Index) {
                    tokString.insert(0, 1, signTokenText);
                } else {
                    printTokenError("Unexpected operator", *signToken);
                    validSec = false;
                    break;
                }
            }

            double floatVal = 0;
            if (!strToFP(tokString, floatVal)) {
                printTokenError(
                    "Floating-point value does not fit into 64-bit value",
                    *tok);
                validSec = false;
                break;
            }

            if (!checkFloatWidth(floatVal, typeInfo->DataType)) {
                printTokenError(
                    "Floating-point value does not fit into given value", *tok);
                validSec = false;
                break;
            }

            ASTFloat* fl = new ASTFloat(tok->Index, tok->Size, tok->LineRow,
                                        tok->LineCol, floatVal);
            val = dynamic_cast<ASTNode*>(fl);
        } else {
            printTokenError(
                "Expected string, float or integer as variable value", *tok);
            validSec = false;
            break;
        }

        tok = eatToken();
        if (tok->Type != TokenType::EOL) {
            printTokenError("Expected new line after variable declaration",
                            *tok);
            validSec = false;
            break;
        }

        uint32_t varSize = (val->Index + val->Size) - id->Index;
        sec->Body.push_back(new ASTVariable(id->Index, varSize, id->LineRow,
                                            id->LineCol, id, typeInfo, val));
        tok = eatToken();
    }

    return validSec;
}

/**
 * Parses the code section
 * @return On valid input returns true otherwise false
 */
bool Parser::parseSectionCode() {
    ParseState state = ParseState::GLOBAL_SCOPE;
    // Pointer to currently parsed function or instruction
    Instruction* instr = nullptr;

    while (state != ParseState::END) {
        Token* t = eatToken();
        switch (state) {
        case ParseState::GLOBAL_SCOPE: {
            // Skip new line token
            if (t->Type == TokenType::EOL) {
                t = eatToken();
            }

            if (t->Type == TokenType::END_OF_FILE ||
                t->Type == TokenType::RIGHT_CURLY_BRACKET) {
                state = ParseState::END;
                continue;
            }

            switch (t->Type) {
            case TokenType::INSTRUCTION: {
                std::string instrName;
                Src->getSubStr(t->Index, t->Size, instrName);
                instr = new Instruction(t->Index, t->Size, t->LineRow,
                                        t->LineCol, instrName, t->Tag);
                FileNode->SecCode->Body.push_back(instr);

                Token* peek = peekToken();
                if (peek->Type == TokenType::END_OF_FILE) {
                    printTokenError("Unexpected end of file after instruction",
                                    *t);
                    return false;
                }

                if (peek->Type != TokenType::EOL) {
                    state = ParseState::INSTR_BODY;
                }
            } break;
            case TokenType::LABEL_DEF: {
                std::string labelName;
                // + 1 because @ sign at start of label should be ignored
                Src->getSubStr(t->Index + 1, t->Size - 1, labelName);
                LabelDef* label = new LabelDef(t->Index, t->Size, t->LineRow,
                                               t->LineCol, labelName);
                FileNode->SecCode->Body.push_back(label);

                Token* peek = peekToken();
                if (peek->Type != TokenType::EOL) {
                    printTokenError("Expected new line after label definition",
                                    *t);
                    return false;
                }
                t = eatToken(); // TODO: BUG ?
            } break;
            default:
                printTokenError("Unexpected token in function body", *t);
                return false;
                break;
            }
        } break;
        case ParseState::INSTR_BODY: {
            bool endOfParamList = false;

            if (t->Type == TokenType::TYPE_INFO) {
                TypeInfo* typeInfo = new TypeInfo(t->Index, t->Size, t->LineRow,
                                                  t->LineCol, t->Tag);
                instr->Params.push_back(typeInfo);
                t = eatToken();

                // This prevents the parser from trying to parse the instruction
                // body of instruction which only have a type a parameter like
                // "pop i8"
                if (t->Type == TokenType::EOL) {
                    endOfParamList = true;
                }
            }

            while (!endOfParamList) {
                Token* signToken = nullptr;
                char signTokenText = '\0';
                if (t->Type == TokenType::PLUS_SIGN ||
                    t->Type == TokenType::MINUS_SIGN) {
                    signToken = t;
                    Src->getChar(signToken->Index, signTokenText);
                    t = eatToken();
                }

                // Sign token must be followed by an integer or float number in
                // the code section otherwise print an error
                if (signToken != nullptr &&
                    (t->Type != TokenType::INTEGER_NUMBER &&
                     t->Type != TokenType::FLOAT_NUMBER)) {
                    printTokenError("Unexpected operator", *signToken);
                    return false;
                }

                switch (t->Type) {
                case TokenType::IDENTIFIER: {
                    std::string idName;
                    Src->getSubStr(t->Index, t->Size, idName);
                    Identifier* id = new Identifier(
                        t->Index, t->Size, t->LineRow, t->LineCol, idName);
                    instr->Params.push_back(id);
                } break;
                case TokenType::REGISTER_DEFINITION: {
                    RegisterId* reg = new RegisterId(
                        t->Index, t->Size, t->LineRow, t->LineCol, t->Tag);
                    instr->Params.push_back(reg);
                } break;
                case TokenType::LEFT_SQUARE_BRACKET: {
                    bool validRO = parseRegOffset(instr);
                    if (!validRO) {
                        return false;
                    }
                } break;
                case TokenType::INTEGER_NUMBER: {
                    std::string numStr;
                    Src->getSubStr(t->Index, t->Size, numStr);

                    bool isSigned = false;
                    // Check if sign token +/- is followed immediately by
                    // number. If so insert +/- into token string to convert it
                    // to an number
                    if (signToken != nullptr) {
                        if (signToken->Index + 1 == t->Index) {
                            numStr.insert(0, 1, signTokenText);
                            if (signToken->Type == TokenType::MINUS_SIGN) {
                                isSigned = true;
                            }
                        } else {
                            printTokenError("Unexpected operator", *signToken);
                            return false;
                        }
                    }

                    uint64_t num = 0;

                    if (!strToInt(numStr, num)) {
                        printTokenError(
                            "Integer does not fit into 64-bit value", *t);
                        return false;
                    }

                    ASTInt* iNum = new ASTInt(t->Index, t->Size, t->LineRow,
                                              t->LineCol, num, isSigned);
                    instr->Params.push_back(iNum);
                } break;
                case TokenType::FLOAT_NUMBER: {
                    std::string floatStr;
                    Src->getSubStr(t->Index, t->Size, floatStr);

                    // Check if sign token +/- is followed immediately by
                    // number. If so insert +/- into token string to convert it
                    // to an number
                    if (signToken != nullptr) {
                        if (signToken->Index + 1 == t->Index) {
                            floatStr.insert(0, 1, signTokenText);
                        } else {
                            printTokenError("Unexpected operator", *signToken);
                            return false;
                        }
                    }

                    double num = 0;
                    if (!strToFP(floatStr, num)) {
                        printTokenError("Float does not fit into 64-bit value",
                                        *t);
                        return false;
                    }

                    ASTFloat* iNum = new ASTFloat(t->Index, t->Size, t->LineRow,
                                                  t->LineCol, num);
                    instr->Params.push_back(iNum);
                } break;
                default:
                    printTokenError("Expected parameter", *t);
                    return false;
                    break;
                }
                t = eatToken();

                if (t->Type == TokenType::COMMA) {
                    t = eatToken();
                } else if (t->Type == TokenType::EOL) {
                    endOfParamList = true;
                }
            }
            state = ParseState::GLOBAL_SCOPE;
        } break;
        }
    }
    return true;
}

/**
 * Builds the abstract syntax tree
 * @return On valid input returns true otherwise false
 */
bool Parser::buildAST() {
    bool validInput = true;
    Token* currentToken = eatToken();
    while (currentToken->Type != TokenType::END_OF_FILE) {
        // Ignore EOL
        if (currentToken->Type == TokenType::EOL) {
            currentToken = eatToken();
            continue;
        }

        // Section identifier
        if (currentToken->Type != TokenType::IDENTIFIER) {
            printTokenError("Expected section identifier in global scope",
                            *currentToken);
            validInput = false;
            break;
        }
        Token* secToken = currentToken;

        // identifer {
        currentToken = eatToken();
        if (currentToken->Type != TokenType::LEFT_CURLY_BRACKET) {
            printTokenError("Expected { after section identifier",
                            *currentToken);
            validInput = false;
            break;
        }

        std::string secName;
        Src->getSubStr(secToken->Index, secToken->Size, secName);

        if (secName == "static") {
            if (FileNode->SecStatic != nullptr) {
                printTokenError("Section 'static' already defined", *secToken);
                validInput = false;
                break;
            }

            FileNode->SecStatic = new ASTSection(
                secToken->Index, secToken->Size, secToken->LineRow,
                secToken->LineCol, secName, ASTSectionType::STATIC);
            if (!parseSectionVars(FileNode->SecStatic)) {
                validInput = false;
                break;
            }
        } else if (secName == "global") {
            if (FileNode->SecGlobal != nullptr) {
                printTokenError("Section 'global' already defined", *secToken);
                validInput = false;
                break;
            }

            FileNode->SecGlobal = new ASTSection(
                secToken->Index, secToken->Size, secToken->LineRow,
                secToken->LineCol, secName, ASTSectionType::GLOBAL);
            if (!parseSectionVars(FileNode->SecGlobal)) {
                validInput = false;
                break;
            }
        } else if (secName == "code") {
            if (FileNode->SecCode != nullptr) {
                printTokenError("Section 'code' already defined", *secToken);
                validInput = false;
                break;
            }

            FileNode->SecCode = new ASTSection(
                secToken->Index, secToken->Size, secToken->LineRow,
                secToken->LineCol, secName, ASTSectionType::CODE);
            if (!parseSectionCode()) {
                validInput = false;
                break;
            }
        } else {
            printTokenError("Unknown section type", *secToken);
            validInput = false;
            break;
        }

        currentToken = eatToken();
    }

    // Check if required code section exists
    if (FileNode->SecCode == nullptr) {
        // TODO: add color
        std::cout << "Error: could not find code section\n";
        return false;
    }

    return validInput;
}

/**
 * Checks if instruction has valid parameters
 * @param instr Pointer to Instruction to type check
 * @param labelRefs Reference to array of label referenced
 * @param funcRefs Reference to array of function referenced
 * @return On success returns true otherwise false
 */
bool Parser::typeCheckInstrParams(Instruction* instr,
                                  std::vector<Identifier*>& labelRefs) {
    // Get top node of instruction paramters tree
    InstrDefNode* paramNode = &(*InstrDefs)[instr->ASMDefIndex];

    // Check if instr has no parameters and see if definiton accepts no
    // parameters. Important: this assumes that an instruction definiton either
    // has 0 parameters or only parameter definitons with at least 1. It cannot
    // have both
    if (instr->Params.size() == 0) {
        if (paramNode->Children.size() == 0) {
            // Attach opcode and return
            instr->Opcode = paramNode->ParamList->Opcode;
            instr->EncodingFlags = paramNode->ParamList->Flags;
            return true;
        } else {
            printError(Src, instr->Index, instr->Name.size(), instr->LineRow,
                       instr->LineCol, "Expected parameters found none");
            return false;
        }
    }

    // Try to find instruction parameter signature
    InstrParamList* paramList = nullptr;
    InstrDefNode* currentNode = paramNode;
    // This error does not indicate if a paramlist was found or not
    bool error = false;
    // This is a reference used to tag every float/int paramter with the correct
    // type and select the correct opcode variant. This assumes that there can
    // only ever be one TypeInfo in the parameters of an instruction.
    TypeInfo* type = nullptr;
    for (uint32_t i = 0; i < instr->Params.size(); i++) {
        InstrDefNode* nextNode = nullptr;
        for (uint32_t n = 0; n < currentNode->Children.size(); n++) {
            ASTNode* astNode = instr->Params[i];
            switch (currentNode->Children[n].Type) {
            case InstrParamType::INT_TYPE: {
                if (astNode->Type != ASTType::TYPE_INFO) {
                    break;
                }
                TypeInfo* typeInfo = dynamic_cast<TypeInfo*>(astNode);
                if (typeInfo->DataType != UVM_TYPE_I8 &&
                    typeInfo->DataType != UVM_TYPE_I16 &&
                    typeInfo->DataType != UVM_TYPE_I32 &&
                    typeInfo->DataType != UVM_TYPE_I64) {
                    printError(Src, typeInfo->Index, typeInfo->Size,
                               typeInfo->LineRow, typeInfo->LineCol,
                               "Expected int type found float type");
                    break;
                }
                type = typeInfo;
                nextNode = &currentNode->Children[n];
            } break;
            case InstrParamType::FLOAT_TYPE: {
                if (astNode->Type != ASTType::TYPE_INFO) {
                    break;
                }
                TypeInfo* typeInfo = dynamic_cast<TypeInfo*>(astNode);
                if (typeInfo->DataType != UVM_TYPE_F32 &&
                    typeInfo->DataType != UVM_TYPE_F64) {
                    printError(Src, typeInfo->Index, typeInfo->Size,
                               typeInfo->LineRow, typeInfo->LineCol,
                               "Expected float type found int type");
                    break;
                }
                type = typeInfo;
                nextNode = &currentNode->Children[n];
            } break;
            case InstrParamType::LABEL_ID: {
                if (astNode->Type != ASTType::IDENTIFIER) {
                    break;
                }
                Identifier* labelRef = dynamic_cast<Identifier*>(astNode);
                labelRefs.push_back(labelRef);
                nextNode = &currentNode->Children[n];
            } break;
            case InstrParamType::INT_REG: {
                if (astNode->Type != ASTType::REGISTER_ID) {
                    break;
                }
                RegisterId* regId = dynamic_cast<RegisterId*>(astNode);
                if (getRegisterType(regId->Id) != RegisterType::INTEGER) {
                    printError(Src, regId->Index, regId->Size, regId->LineRow,
                               regId->LineCol, "Expected integer register");
                    break;
                }
                nextNode = &currentNode->Children[n];
            } break;
            case InstrParamType::FLOAT_REG: {
                if (astNode->Type != ASTType::REGISTER_ID) {
                    break;
                }
                RegisterId* regId = dynamic_cast<RegisterId*>(astNode);
                if (getRegisterType(regId->Id) != RegisterType::FLOAT) {
                    printError(Src, regId->Index, regId->Size, regId->LineRow,
                               regId->LineCol, "Expected float register");
                    break;
                }
                nextNode = &currentNode->Children[n];
            } break;
            case InstrParamType::REG_OFFSET: {
                if (astNode->Type != ASTType::REGISTER_OFFSET) {
                    break;
                }
                nextNode = &currentNode->Children[n];
            } break;
            case InstrParamType::INT_NUM: {
                if (astNode->Type != ASTType::INTEGER_NUMBER) {
                    break;
                }

                ASTInt* num = dynamic_cast<ASTInt*>(astNode);
                num->DataType = type->DataType;
                if (!checkIntWidth(num->Num, num->DataType, num->IsSigned)) {
                    printError(Src, num->Index, num->Size, num->LineRow,
                               num->LineCol,
                               "Integer does not fit into given type");
                    error = true;
                }
                nextNode = &currentNode->Children[n];
            } break;
            case InstrParamType::FLOAT_NUM: {
                if (astNode->Type != ASTType::FLOAT_NUMBER) {
                    break;
                }

                ASTFloat* num = dynamic_cast<ASTFloat*>(astNode);
                num->DataType = type->DataType;
                if (!checkFloatWidth(num->Num, num->DataType)) {
                    printError(Src, num->Index, num->Size, num->LineRow,
                               num->LineCol,
                               "Float does not fit into given type");
                    error = true;
                }
                nextNode = &currentNode->Children[n];
            } break;
            case InstrParamType::SYS_INT: {
                if (astNode->Type != ASTType::INTEGER_NUMBER) {
                    break;
                }

                ASTInt* num = dynamic_cast<ASTInt*>(astNode);
                // syscall args are always 1 byte
                num->DataType = UVM_TYPE_I8;
                nextNode = &currentNode->Children[n];
            } break;
            }
        }

        if (nextNode == nullptr) {
            break;
        } else {
            currentNode = nextNode;
        }

        if (i + 1 == instr->Params.size()) {
            paramList = currentNode->ParamList;
        }
    }

    if (paramList == nullptr) {
        printError(Src, instr->Index, instr->Name.size(), instr->LineRow,
                   instr->LineCol,
                   "Error no matching parameter list found for instruction");
        return false;
    }

    if (error) {
        return false;
    }

    // Check if the opcode is determined by a type variant and attach the opcode
    // to the Instruction node
    if (paramList->Flags & INSTR_FLAG_TYPE_VARIANTS) {
        uint8_t opcode = 0;
        // Find opcode variant
        for (uint32_t i = 0; i < paramList->OpcodeVariants.size(); i++) {
            TypeVariant* variant = &paramList->OpcodeVariants[i];
            if (variant->Type == type->DataType) {
                opcode = variant->Opcode;
            }
        }
        instr->Opcode = opcode;
    } else {
        instr->Opcode = paramList->Opcode;
    }
    // Attach encoding information
    instr->EncodingFlags = paramList->Flags;

    return true;
}

/**
 * Checks if type and values of global and static variables match
 * @return On valid input return true otherwise false
 */
bool Parser::typeCheckVars(ASTSection* sec) {
    bool valid = true;

    // Check for variable redefinitons
    for (ASTNode* node : sec->Body) {
        ASTVariable* var = dynamic_cast<ASTVariable*>(node);

        // Check if var has already been declared
        bool exists = false;
        for (VarDeclaration& varDecl : *VarDecls) {
            if (varDecl.Id->Name == var->Id->Name) {
                exists = true;
                break;
            }
        }

        if (exists) {
            printError(Src, var->Index, var->Size, var->LineRow, var->LineCol,
                       "Variable redefiniton");
            valid = false;
            continue;
        }

        VarDeclaration varDecl{};
        varDecl.Id = var->Id;

        switch (sec->SecType) {
        case ASTSectionType::STATIC:
            varDecl.SecPerm = SEC_PERM_READ;
            break;
        case ASTSectionType::GLOBAL:
            varDecl.SecPerm = SEC_PERM_READ & SEC_PERM_WRITE;
            break;
        case ASTSectionType::CODE:
            varDecl.SecPerm = SEC_PERM_READ & SEC_PERM_EXECUTE;
            break;
        }

        var->VarDeclIndex = VarDecls->size();
        VarDecls->push_back(varDecl);
    }

    return valid;
}

/**
 * Checks if all referenced variables are resolved
 * @return On success returns true otherwise false
 */
bool Parser::checkVarRefs() {
    bool valid = true;

    for (ASTNode* node : FileNode->SecCode->Body) {
        if (node->Type == ASTType::INSTRUCTION) {
            Instruction* instr = dynamic_cast<Instruction*>(node);
            for (ASTNode* instrParam : instr->Params) {
                if (instrParam->Type == ASTType::REGISTER_OFFSET) {
                    RegisterOffset* ro =
                        dynamic_cast<RegisterOffset*>(instrParam);
                    // If register offset has label as its content check if the
                    // var exists
                    if (ro->Var != nullptr) {
                        bool exists = false;
                        for (VarDeclaration& decl : *VarDecls) {
                            if (decl.Id->Name == ro->Var->Name) {
                                exists = true;
                                break;
                            }
                        }

                        if (!exists) {
                            printError(Src, ro->Var->Index, ro->Var->Size,
                                       ro->Var->LineRow, ro->Var->LineCol,
                                       "Variable reference does not exist");
                            valid = false;
                        }
                    }
                }
            }
        }
    }

    return valid;
}

/**
 * Performs a complete type checking pass over the AST
 * @return Returns true if no errors occured otherwise false
 */
bool Parser::typeCheck() {
    // Tracks if an error occured while type checking
    bool typeCheckError = false;

    std::vector<ASTSection*> sections = {FileNode->SecStatic,
                                         FileNode->SecGlobal};
    for (ASTSection* sec : sections) {
        if (sec == nullptr) {
            continue;
        }

        if (!typeCheckVars(sec)) {
            typeCheckError = true;
        }
    }

    // Check if code section AST node has no children which means the main
    // function is missing for sure
    if (FileNode->SecCode->Body.size() == 0) {
        std::cout << "[Type Checker] Missing main label\n";
        return false;
    }

    // Try to find main entry point
    LabelDef* mainEntry = nullptr;
    for (uint32_t i = 0; i < FileNode->SecCode->Body.size(); i++) {
        ASTNode* node = FileNode->SecCode->Body[i];
        if (node->Type == ASTType::LABEL_DEFINITION) {
            LabelDef* label = dynamic_cast<LabelDef*>(node);
            if (label->Name == "main") {
                mainEntry = label;
            }
        }
    }

    // Check if main function was found
    if (mainEntry == nullptr) {
        std::cout << "[Type Checker] Missing main entry\n";
        return false;
    }

    // This is used to keep track of the localy referenced function and label
    // identifiers aswell as localy defined label definitions
    std::vector<Identifier*> labelRefs;

    // Type check complete AST. This assumes that the build AST generated a
    // valid AST
    for (const auto& globElem : FileNode->SecCode->Body) {
        if (globElem->Type == ASTType::LABEL_DEFINITION) {
            LabelDef* label = dynamic_cast<LabelDef*>(globElem);

            // Check if function definition is redifined
            bool labelRedef = false;
            for (uint32_t i = 0; i < LabelDefs->size(); i++) {
                if ((*LabelDefs)[i].Def->Name == label->Name) {
                    labelRedef = true;
                    break;
                }
            }

            // If function is a redefinition continue with parsing the function
            // body anyway
            if (labelRedef) {
                printError(Src, label->Index, label->Name.size(),
                           label->LineRow, label->LineCol,
                           "Label is already defined");
                typeCheckError = true;
            }
            LabelDefs->push_back(LabelDefLookup{label, 0});
        } else if (globElem->Type == ASTType::INSTRUCTION) {
            Instruction* instr = dynamic_cast<Instruction*>(globElem);
            if (!typeCheckInstrParams(instr, labelRefs)) {
                typeCheckError = true;
                continue;
            }
        }
    }

    // Check if all label references are resolved
    for (const auto& labelRef : labelRefs) {
        bool foundDef = false;
        for (uint32_t i = 0; i < LabelDefs->size(); i++) {
            if (labelRef->Name == (*LabelDefs)[i].Def->Name) {
                foundDef = true;
                break;
            }
        }

        if (!foundDef) {
            printError(Src, labelRef->Index, labelRef->Name.size(),
                       labelRef->LineRow, labelRef->LineCol,
                       "Unresolved label");
            typeCheckError = true;
        }
    }

    if (!checkVarRefs()) {
        typeCheckError = true;
    };

    return !typeCheckError;
}
