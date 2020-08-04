/**
 * Copyright 2020 Michel Fäh
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once
#include "ast.hpp"
#include "instruction.hpp"
#include "scanner.hpp"
#include "token.hpp"
#include <vector>

enum class ParseState { GLOBAL_SCOPE, FUNC_BODY, INSTR_BODY, END };

class Parser {
  public:
    Parser(Source* src, std::vector<Token>* tokens, Global* global);
    bool buildAST();
    bool typeCheck();

  private:
    uint64_t Cursor = 0;
    std::vector<Token>* Tokens;
    Global* Glob;
    Source* Src;
    uint8_t getUVMType(Token* tok);
    uint8_t getRegisterTypeFromName(std::string& regName);
    int64_t strToInt(std::string& str);
    Token* eatToken();
    bool peekToken(Token** tok);
    bool parseRegOffset(Instruction* instr);
    void throwError(const char* msg, Token& tok);
    bool typeCheckInstrParams(Instruction* instr);
};