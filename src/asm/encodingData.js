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

// This script generates a encoding header file from encoding json data

// To run this file:
// deno run --unstable --allow-read --allow-write ./src/asm/encodingData.js

import { readJson } from "https://deno.land/std/fs/mod.ts";

let DIRNAME = '';
const FILE_NAME = 'encodingData.json';
// TODO: Make this a function
const TAB = '    ';
const TAB2 = `${TAB}${TAB}`;
const TAB3 = `${TAB}${TAB}${TAB}`;
const TAB4 = `${TAB}${TAB}${TAB}${TAB}`;

const HEADER =
    `/**
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
#include "asm.hpp"
#include <cstdint>
#include <map>
#include <vector>

/*
    THIS FILE IS GENERATED BY THE SCRIPT 'encodingData.js' DO NOT MODIFY!
*/
`;
const UVM_TYPES = {
    'i8': 'UVM_TYPE_I8',
    'i16': 'UVM_TYPE_I16',
    'i32': 'UVM_TYPE_I32',
    'i64': 'UVM_TYPE_I64',
    'f32': 'UVM_TYPE_F32',
    'f64': 'UVM_TYPE_F64',
};

const PARAM_TYPES = {
    'iT': 'INT_TYPE',
    'fT': 'FLOAT_TYPE',
    'iReg': 'INT_REG',
    'fReg': 'FLOAT_REG',
    'function': 'FUNC_ID',
    'label': 'LABEL_ID',
    'RO': 'REG_OFFSET',
    'int': 'INT_NUM',
    'float': 'FLOAT_NUM',
    'sysID': 'SYS_INT',
};

/**
 * Gets all required paths
 */
function getFilePath() {
    // Set current directory name
    const p = import.meta.url.replace('file:///', '').split('/');
    DIRNAME = p.slice(0, p.length - 1).join('/');
    // Return filepath of JSON file
    return `${DIRNAME}/${FILE_NAME}`;
}

function generateHeaderFile(data) {
    // File output buffer
    let buffer = `${HEADER}\n`;
    let instrNameDefBuffer = 'const static std::vector<InstrNameDef> INSTR_NAMES = {\n';
    // Buffer for instructions map
    let instrBuffer = `const static std::map<uint8_t, std::vector<InstrParamList>> INSTR_ASM_DEFS = {\n`;
    // Buffer for instruction enum
    let instrEnumBuffer = '';

    data.instructions.forEach((instr, i) => {
        const instrEnum = instr.name.toUpperCase();

        // Add instruction to enum
        instrEnumBuffer += `constexpr uint8_t INSTR_${instrEnum} = ${i};\n`;
        instrNameDefBuffer += `${TAB}InstrNameDef{"${instr.name}", INSTR_${instrEnum}},\n`;

        instrBuffer += `${TAB}{INSTR_${instrEnum}, `;

        // Add InstrParamList objects
        instrBuffer += '{\n';
        instr.paramList.forEach((param) => {
            instrBuffer += `${TAB2}InstrParamList{\n${TAB3}${param.opcode},\n`;

            let flagsTmp = '';
            // Add flags
            if (param.encodeType) {
                flagsTmp += 'INSTR_FLAG_ENCODE_TYPE';
            } else if (param.typeVariants.length > 0) {
                flagsTmp += 'INSTR_FLAG_TYPE_VARIANTS';
            }

            if (flagsTmp.length === 0) {
                flagsTmp = '0';
            }
            instrBuffer += `${TAB3}${flagsTmp},\n`;

            // Paramters
            instrBuffer += `${TAB3}{`;
            param.params.forEach((paramEntry, i) => {
                instrBuffer += `InstrParamType::${PARAM_TYPES[paramEntry]}`;
                if (i + 1 < param.params.length) {
                    instrBuffer += ', ';
                }
            });
            instrBuffer += '},\n'

            // Type variants
            instrBuffer += `${TAB3}{`;
            if (param.typeVariants.length > 0) {
                instrBuffer += '\n';
            }

            param.typeVariants.forEach((typeVar, i) => {
                instrBuffer += `${TAB4}{${UVM_TYPES[typeVar.type]}, ${typeVar.opcode}}`;
                if (i + 1 < param.typeVariants.length) {
                    instrBuffer += ',\n';
                }
            });

            if (param.typeVariants.length > 0) {
                instrBuffer += `\n${TAB3}}`;
            } else {
                instrBuffer += '}';
            }

            instrBuffer += `\n${TAB2}},\n`;
        });
        instrBuffer += `${TAB}${TAB}}\n`;

        instrBuffer += `${TAB}}`;
        if (i + 1 < data.instructions.length) {
            instrBuffer += `,\n`;
        }
    });

    instrNameDefBuffer += '};\n';
    instrBuffer += '\n};'

    buffer += `${instrEnumBuffer}\n${instrNameDefBuffer}\n${instrBuffer}\n`;

    return buffer;
}

(async function main() {
    const p = getFilePath();
    const data = await readJson(p);
    const content = generateHeaderFile(data);
    await Deno.writeTextFile(`${DIRNAME}/encoding.hpp`, content);
}())
