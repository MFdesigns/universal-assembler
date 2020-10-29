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
#include <iomanip>
#include <iostream>

/**
 * Constructs a new Parser
 * @param src Pointer to the source file
 * @param tokens Pointer to the token array
 * @param global [out] Pointer to the Global AST node
 * @param funcDefs [out] Pointer to the FuncDefLookup
 */
Parser::Parser(Source* src,
               std::vector<Token>* tokens,
               Global* global,
               std::vector<FuncDefLookup>* funcDefs)
    : Src(src), Tokens(tokens), Glob(global), FuncDefs(funcDefs){};

uint8_t Parser::getUVMType(Token* tok) {
    std::string typeName;
    Src->getSubStr(tok->Index, tok->Size, typeName);

    uint8_t type = 0;
    if (typeName == "i8") {
        type = UVM_TYPE_I8;
    } else if (typeName == "i16") {
        type = UVM_TYPE_I16;
    } else if (typeName == "i32") {
        type = UVM_TYPE_I32;
    } else if (typeName == "i64") {
        type = UVM_TYPE_I64;
    } else if (typeName == "f32") {
        type = UVM_TYPE_F32;
    } else if (typeName == "f64") {
        type = UVM_TYPE_F64;
    }
    return type;
}

int64_t Parser::strToInt(std::string& str) {
    int64_t num = 0;
    int32_t base = 10;
    if (str.size() >= 3) {
        if (str[0] == '0' && str[1] == 'x') {
            base = 16;
        }
    }
    // Warning: this only works with unsigned numbers and  does not handle
    // sigend numbers
    num = std::stoull(str, 0, base);
    return num;
}

uint8_t Parser::getRegisterTypeFromName(std::string& regName) {
    uint8_t id = 0;
    if (regName == "ip") {
        id = 1;
    } else if (regName == "bp") {
        id = 3;
    } else if (regName == "sp") {
        id = 2;
    } else {
        uint8_t base = 0;
        if (regName[0] == 'r') {
            base = 0x5;
        } else if (regName[0] == 'f') {
            base = 0x16;
        }

        const char* num = regName.c_str();
        uint32_t offset = std::atoi(&num[1]);
        id = base + offset;
    }
    return id;
}

Token* Parser::eatToken() {
    Token* tok = nullptr;
    if (Cursor < Tokens->size()) {
        // Because Cursor starts at index 0 return current token before
        // increasing the cursor
        tok = &(*Tokens)[Cursor];
        Cursor++;
    } else {
        tok = &(*Tokens)[Tokens->size() - 1];
    }
    return tok;
}

bool Parser::peekToken(Token** tok) {
    if (Cursor < Tokens->size()) {
        *tok = &(*Tokens)[Cursor];
        return true;
    }
    return false;
}

void Parser::throwError(const char* msg, Token& tok) {
    std::string line;
    uint32_t lineIndex = 0;
    Src->getLine(tok.Index, line, lineIndex);
    std::string lineNr = std::to_string(lineIndex);

    uint32_t errOffset = tok.Index - lineIndex;

    std::cout << "[Parser Error] " << msg << " at Ln " << tok.LineRow
              << ", Col " << tok.LineCol << '\n'
              << "  " << tok.LineRow << " | " << line << '\n'
              << std::setw(2 + lineNr.size()) << std::setfill(' ') << " |"
              << std::setw(errOffset + 1) << std::setfill(' ') << ' '
              << std::setw(tok.Size) << std::setfill('~') << '~' << '\n';
}

bool Parser::parseRegOffset(Instruction* instr) {
    constexpr uint8_t RO_LAYOUT_NEG = 0b1000'0000;
    constexpr uint8_t RO_LAYOUT_POS = 0b0000'0000;
    RegisterOffset* regOff = new RegisterOffset();
    Token* t = eatToken();

    if (t->Type == TokenType::REGISTER_DEFINITION) {
        std::string regName;
        Src->getSubStr(t->Index, t->Size, regName);
        uint8_t regId = getRegisterTypeFromName(regName);
        regOff->Base = new RegisterId(t->Index, t->LineRow, t->LineCol, regId);
        t = eatToken();
    } else {
        throwError("Expected register in register offset", *t);
        return false;
    }

    if (t->Type == TokenType::RIGHT_SQUARE_BRACKET) {
        regOff->Position = t->Index;
        regOff->LineNumber = t->LineRow;
        regOff->LineColumn = t->LineCol;
        regOff->Layout = RO_LAYOUT_IR;
        instr->Params.push_back(regOff);
        return true;
    } else if (t->Type == TokenType::PLUS_SIGN) {
        regOff->Layout |= RO_LAYOUT_POS;
    } else if (t->Type == TokenType::MINUS_SIGN) {
        regOff->Layout |= RO_LAYOUT_NEG;
    } else {
        throwError("Unexpected token in register offset", *t);
        return false;
    }

    t = eatToken();
    // <iR> +/- <i32>
    if (t->Type == TokenType::INTEGER_NUMBER) {
        Token* peek;
        bool eof = !peekToken(&peek);
        if (!eof && peek->Type == TokenType::RIGHT_SQUARE_BRACKET) {
            // Get int string and convert to an int
            std::string numStr;
            Src->getSubStr(t->Index, t->Size, numStr);
            int64_t num = strToInt(numStr);

            // <iR> + <i32> expects integer to have a maximum size of 32 bits
            // Check if the requirement is meet otherwise throw error
            if (num >> 32 != 0) {
                throwError(
                    "Register offset immediate does not fit into 32-bit value",
                    *t);
                return false;
            }
            regOff->Immediate.U32 = (uint32_t)num;

            // TODO: Register offset position is not correct
            regOff->Position = t->Index;
            regOff->LineNumber = t->LineRow;
            regOff->LineColumn = t->LineCol;
            regOff->Layout |= RO_LAYOUT_IR_INT;
            instr->Params.push_back(regOff);
            t = eatToken();
        } else {
            throwError("Expected closing bracket after immediate offset inside "
                       "register offset ]",
                       *t);
            return false;
        }
    } else if (t->Type == TokenType::REGISTER_DEFINITION) {
        std::string regName;
        Src->getSubStr(t->Index, t->Size, regName);
        uint8_t regIdType = getRegisterTypeFromName(regName);
        regOff->Offset =
            new RegisterId(t->Index, t->LineRow, t->LineCol, regIdType);
        t = eatToken();
        if (t->Type == TokenType::ASTERISK) {
            t = eatToken();
        } else {
            throwError("Expected * after offset inside register offset", *t);
            return false;
        }

        std::string numStr;
        Src->getSubStr(t->Index, t->Size, numStr);
        int64_t num = strToInt(numStr);

        // <iR> +/- <iR> * <i16> expects integer to have a maximum size of 16
        // bits Check if the requirement is meet otherwise throw error
        if (num >> 16 != 0) {
            throwError(
                "Register offset immediate does not fit into 16-bit value", *t);
            return false;
        }
        regOff->Immediate.U16 = (uint16_t)num;
        t = eatToken();

        if (t->Type == TokenType::RIGHT_SQUARE_BRACKET) {
            // TODO: Register offset position is not correct
            regOff->Position = t->Index;
            regOff->LineNumber = t->LineRow;
            regOff->LineColumn = t->LineCol;
            regOff->Layout |= RO_LAYOUT_IR_IR_INT;
            instr->Params.push_back(regOff);
        } else {
            throwError("Expectd closing bracket after factor", *t);
            return false;
        }

    } else {
        throwError("Expected register or int number as offset", *t);
        return false;
    }
    return true;
}

bool Parser::buildAST() {
    ParseState state = ParseState::GLOBAL_SCOPE;
    // Pointer to currently parsed function or instruction
    FuncDef* func = nullptr;
    Instruction* instr = nullptr;

    while (state != ParseState::END) {
        Token* t = eatToken();
        switch (state) {
        case ParseState::GLOBAL_SCOPE: {
            // Ignore new line token
            if (t->Type == TokenType::EOL) {
                t = eatToken();
            }

            if (t->Type == TokenType::END_OF_FILE) {
                state = ParseState::END;
                continue;
            }

            // If the current state is the Global scope we expect a identifier
            // followed by an open curly bracket
            if (t->Type != TokenType::IDENTIFIER) {
                throwError("Expected identifier", *t);
                return false;
            }

            std::string funcName;
            Src->getSubStr(t->Index, t->Size, funcName);
            Token* peek = nullptr;
            bool eof = !peekToken(&peek);
            if (eof || peek->Type != TokenType::LEFT_CURLY_BRACKET) {
                throwError("Expected { in function definition", *t);
                return false;
            }

            t = eatToken();
            func = new FuncDef(t->Index, t->LineRow, t->LineCol, funcName);
            Glob->Body.push_back(func);
            state = ParseState::FUNC_BODY;
        } break;
        case ParseState::FUNC_BODY: {
            // Skip new line token
            if (t->Type == TokenType::EOL) {
                t = eatToken();
                if (t->Type == TokenType::END_OF_FILE) {
                    throwError("Unexpected end of file in function body", *t);
                    return false;
                }
            }

            if (t->Type == TokenType::RIGHT_CURLY_BRACKET) {
                state = ParseState::GLOBAL_SCOPE;
                continue;
            }

            switch (t->Type) {
            case TokenType::INSTRUCTION: {
                std::string instrName;
                Src->getSubStr(t->Index, t->Size, instrName);
                instr = new Instruction(t->Index, t->LineRow, t->LineCol,
                                        instrName);
                func->Body.push_back(instr);

                Token* peek = nullptr;
                bool eof = !peekToken(&peek);
                if (eof) {
                    throwError("Unexpected end of file after instruction", *t);
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
                LabelDef* label =
                    new LabelDef(t->Index, t->LineRow, t->LineCol, labelName);
                func->Body.push_back(label);

                Token* peek = nullptr;
                bool eof = !peekToken(&peek);
                if (eof || peek->Type != TokenType::EOL) {
                    throwError("Expected new line after label definition", *t);
                    return false;
                }
                t = eatToken(); // TODO: BUG ?
            } break;
            case TokenType::RIGHT_CURLY_BRACKET:
                state = ParseState::GLOBAL_SCOPE;
                continue;
                break;
            default:
                throwError("Unexpected token in function body", *t);
                return false;
                break;
            }
        } break;
        case ParseState::INSTR_BODY: {
            if (t->Type == TokenType::TYPE_INFO) {
                uint8_t typeUVMType = getUVMType(t);
                TypeInfo* typeInfo =
                    new TypeInfo(t->Index, t->LineRow, t->LineCol, typeUVMType);
                instr->Params.push_back(typeInfo);
                t = eatToken();
            }

            bool endOfParamList = false;
            while (!endOfParamList) {
                switch (t->Type) {
                case TokenType::IDENTIFIER: {
                    std::string idName;
                    Src->getSubStr(t->Index, t->Size, idName);
                    Identifier* id = new Identifier(t->Index, t->LineRow,
                                                    t->LineCol, idName);
                    instr->Params.push_back(id);
                } break;
                case TokenType::REGISTER_DEFINITION: {
                    std::string regName;
                    Src->getSubStr(t->Index, t->Size, regName);
                    uint8_t regType = getRegisterTypeFromName(regName);
                    RegisterId* reg = new RegisterId(t->Index, t->LineRow,
                                                     t->LineCol, regType);
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
                    int64_t num = strToInt(numStr);
                    IntegerNumber* iNum = new IntegerNumber(
                        t->Index, t->LineRow, t->LineCol, num);
                    instr->Params.push_back(iNum);
                } break;
                case TokenType::FLOAT_NUMBER: {
                    std::string floatStr;
                    Src->getSubStr(t->Index, t->Size, floatStr);
                    double num = std::atof(floatStr.c_str());
                    FloatNumber* iNum =
                        new FloatNumber(t->Index, t->LineRow, t->LineCol, num);
                    instr->Params.push_back(iNum);
                } break;
                default:
                    throwError("Expected parameter", *t);
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
            state = ParseState::FUNC_BODY;
        } break;
        }
    }
    return true;
}

bool Parser::typeCheckInstrParams(Instruction* instr,
                                  std::vector<Identifier*>& labelRefs,
                                  std::vector<Identifier*>& funcRefs) {
    // TODO: Move this to the scanner phase
    uint8_t instrID = 0;
    bool foundInstr = false;
    uint32_t instrNameDefIndex = 0;
    while (!foundInstr && instrNameDefIndex < INSTR_NAMES.size()) {
        const InstrNameDef* nameDef = &INSTR_NAMES[instrNameDefIndex];
        if (instr->Name == nameDef->Str) {
            instrID = nameDef->Id;
            foundInstr = true;
        }
        instrNameDefIndex++;
    }
    const std::vector<InstrParamList>* paramLists = &INSTR_ASM_DEFS.at(instrID);

    // Try to find instruction paramter signature
    const InstrParamList* paramList = nullptr;
    bool foundSign = false;
    uint32_t paramListIndex = 0;
    while (!foundSign && paramListIndex < paramLists->size()) {
        paramList = &(*paramLists)[paramListIndex];
        if (paramList->Params.size() == instr->Params.size()) {
            bool validList = true;
            uint32_t listIndex = 0;

            // This is a reference used to tag every float/int paramter with the
            // correct type
            TypeInfo* type = nullptr;

            while (validList && listIndex < paramList->Params.size()) {
                ASTNode* node = instr->Params[listIndex];
                switch (paramList->Params[listIndex]) {
                case InstrParamType::INT_TYPE: {
                    // TODO: Move this into the build AST phase
                    if (node->Type != ASTType::TYPE_INFO) {
                        validList = false;
                        break;
                    }

                    type = dynamic_cast<TypeInfo*>(node);
                    if (type->DataType != UVM_TYPE_I8 &&
                        type->DataType != UVM_TYPE_I16 &&
                        type->DataType != UVM_TYPE_I32 &&
                        type->DataType != UVM_TYPE_I64) {
                        validList = false;
                    }
                } break;
                case InstrParamType::FLOAT_TYPE: {
                    // TODO: Move this into the build AST phase
                    if (node->Type != ASTType::TYPE_INFO) {
                        validList = false;
                        break;
                    }

                    type = dynamic_cast<TypeInfo*>(node);
                    if (type->DataType != UVM_TYPE_F32 &&
                        type->DataType != UVM_TYPE_F64) {
                        validList = false;
                    }
                } break;
                case InstrParamType::FUNC_ID: {
                    if (node->Type != ASTType::IDENTIFIER) {
                        validList = false;
                        break;
                    }
                    Identifier* funcId = dynamic_cast<Identifier*>(node);
                    funcRefs.push_back(funcId);
                } break;
                case InstrParamType::LABEL_ID: {
                    if (node->Type != ASTType::IDENTIFIER) {
                        validList = false;
                    }
                    Identifier* labelId = dynamic_cast<Identifier*>(node);
                    labelRefs.push_back(labelId);
                } break;
                case InstrParamType::INT_REG: {
                    if (node->Type != ASTType::REGISTER_ID) {
                        validList = false;
                        break;
                    }

                    RegisterId* regId = dynamic_cast<RegisterId*>(node);
                    // TODO: What about flag register ?
                    if (regId->Id < 0x1 && regId->Id > 0x15) {
                        validList = false;
                    }
                } break;
                case InstrParamType::FLOAT_REG: {
                    if (node->Type != ASTType::REGISTER_ID) {
                        validList = false;
                        break;
                    }

                    RegisterId* regId = dynamic_cast<RegisterId*>(node);
                    if (regId->Id < 0x16 && regId->Id > 0x26) {
                        validList = false;
                    }
                } break;
                case InstrParamType::REG_OFFSET: {
                    if (node->Type != ASTType::REGISTER_OFFSET) {
                        validList = false;
                    }
                } break;
                case InstrParamType::INT_NUM: {
                    if (node->Type != ASTType::INTEGER_NUMBER) {
                        validList = false;
                        break;
                    }

                    IntegerNumber* num = dynamic_cast<IntegerNumber*>(node);
                    num->DataType = type->DataType;
                } break;
                case InstrParamType::FLOAT_NUM: {
                    if (node->Type != ASTType::FLOAT_NUMBER) {
                        validList = false;
                        break;
                    }

                    FloatNumber* num = dynamic_cast<FloatNumber*>(node);
                    num->DataType = type->DataType;
                } break;
                case InstrParamType::SYS_INT: {
                    if (node->Type != ASTType::INTEGER_NUMBER) {
                        validList = false;
                        break;
                    }

                    IntegerNumber* num = dynamic_cast<IntegerNumber*>(node);
                    num->DataType =
                        UVM_TYPE_I8; // syscall args are always 8-bit
                } break;
                }
                listIndex++;
            }
            foundSign = validList;
        }
        paramListIndex++;
    }

    if (!foundSign) {
        // TODO: If function name is not resolved to Instructions enum them
        // assembler will just assume its a NOP
        std::cout << "Error no matching parameter list found for instruction "
                  << instr->Name << " at Ln " << instr->LineNumber << " Col "
                  << instr->LineColumn << '\n';
        return false;
    }

    // TODO: Range check int and float number for given uvm type
    instr->ParamList =
        (InstrParamList*)paramList; // Attach encoding info to instruction

    return true;
}

/**
 * Performs a complete type checking pass over the AST
 * @return Returns true if no errors occured otherwise false
 */
bool Parser::typeCheck() {
    // Check if Global AST node has no children which means the main function is
    // missing for sure
    if (Glob->Body.size() == 0) {
        std::cout << "[Type Checker] Missing main function\n";
        return false;
    }

    // Try to find main function
    FuncDef* mainFunc = nullptr;
    for (uint32_t i = 0; i < Glob->Body.size(); i++) {
        ASTNode* node = Glob->Body[i];
        if (node->Type == ASTType::FUNCTION_DEFINTION) {
            FuncDef* func = dynamic_cast<FuncDef*>(node);
            if (func->Name == "main") {
                mainFunc = func;
            }
        }
    }

    // Check if main function was found
    if (mainFunc == nullptr) {
        std::cout << "[Type Checker] Missing main function\n";
        return false;
    }

    // This is used to keep track of the localy referenced function and label
    // identifiers aswell as localy defined label definitions
    std::vector<LabelDef*> scopeLabelDefs;
    std::vector<Identifier*> scopeLabelRefs;
    std::vector<Identifier*> scopeFuncRefs;

    // Tracks if an error occured while type checking
    bool typeCheckError = false;
    // Type check complete AST. This assumes that the build AST generated a
    // valid AST
    for (const auto& globElem : Glob->Body) {
        // This assumes that all global nodes are function definitions. This
        // might change with future features
        FuncDef* func = dynamic_cast<FuncDef*>(globElem);

        // Check if function definition is redifined
        bool funcRedef = false;
        for (uint32_t i = 0; i < FuncDefs->size(); i++) {
            if ((*FuncDefs)[i].Def->Name == func->Name) {
                funcRedef = true;
                break;
            }
        }

        // If function is a redefinition continue with parsing the function body
        // anyway
        if (funcRedef) {
            typeCheckError = true;
            std::cout << "[Type Checker] Error: function is already defined\n";
        }
        FuncDefs->push_back(FuncDefLookup{func, 0});

        // Clear all scoped definitions and references
        scopeLabelDefs.clear();

        // Check if function body is empty, display a warning and continue to
        // parse next function
        if (func->Body.size() == 0) {
            typeCheckError = true;
            std::cout << "[Type Checker] Warning: empty function body\n";
            continue;
        }

        // Type check every function element. Again this assumes that the
        // generated AST is valid
        for (const auto& funcElem : func->Body) {
            if (funcElem->Type == ASTType::INSTRUCTION) {
                Instruction* instr = dynamic_cast<Instruction*>(funcElem);
                if (!typeCheckInstrParams(instr, scopeLabelRefs,
                                          scopeFuncRefs)) {
                    typeCheckError = true;
                    continue;
                }
            } else if (funcElem->Type == ASTType::LABEL_DEFINITION) {
                LabelDef* label = dynamic_cast<LabelDef*>(funcElem);

                // Check if label definiton was already defined in the local
                // scope
                bool labelRedef = false;
                for (uint32_t i = 0; i < scopeLabelDefs.size(); i++) {
                    if (scopeLabelDefs[i]->Name == label->Name) {
                        labelRedef = true;
                        break;
                    }
                }

                if (labelRedef) {
                    typeCheckError = true;
                    std::cout << "[Type Checker] Error: label is already "
                                 "defined in this scope\n";
                    continue;
                }

                // If label is not a redefinition add it to the localy defined
                // labels
                scopeLabelDefs.push_back(label);
            }
        }
    }

    // Check if all function references are resolved
    for (const auto& funcRef : scopeFuncRefs) {
        bool foundDef = false;
        for (uint32_t i = 0; i < FuncDefs->size(); i++) {
            if (funcRef->Name == (*FuncDefs)[i].Def->Name) {
                foundDef = true;
                break;
            }
        }

        if (!foundDef) {
            std::cout << "[Type Checker] Error: unresolved function '"
                      << funcRef->Name << "'\n";
            typeCheckError = true;
        }
    }

    return !typeCheckError;
}
