#include <iostream>
#include <fstream>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include <iomanip>
#include <string>
#include <map>

using namespace std;

enum GPR_NAMES { 
    EAX, 
    ECX, 
    EDX, 
    EBX, 
    ESP, 
    EBP, 
    ESI, 
    EDI 
};

enum SEGR_NAMES { 
    ES, 
    CS, 
    SS, 
    DS, 
    FS, 
    GS 
};

enum FLAG_NAMES { 
    CF, 
    PF, 
    AF, 
    ZF, 
    SF, 
    DF, 
    OF 
};

typedef struct{
    int32_t EIP;
    int32_t GPR[8]; //EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI
    int64_t MMX[8]; //MMX0 - MMX7
    int16_t SEGR[6]; //ES, CS, SS, DS, FS, GS
    bool FLAGS[7]; //CF, PF, AF, ZF, SF, DF, OF
    vector<uint8_t> INSTR;
}state_t;

typedef struct{
    uint8_t mod, reg, r_m;
}modrm_t;

state_t curr_state, next_state;
map <uint32_t, uint8_t> mem;
int32_t cycles = 0;
bool run = false;

void init_state(){
    curr_state.EIP = 0x00000000;
    for(int i = 0; i < 8; i++){ curr_state.GPR[i] = 0x00000000; curr_state.MMX[i] = 0x00000000;}
    for(int i = 0; i < 7; i++){curr_state.FLAGS[i] = false;}
    for(int i = 0; i < 6; i++){curr_state.SEGR[i] = 0x0000;}
    curr_state.INSTR.clear();
    next_state = curr_state;
    run = true;
}

void init_mem(string file_name){   
    ifstream inputFile(file_name);
    string line;

    while (getline(inputFile, line)){
        if(line.empty() || line.substr(0,2) != "0x") continue;

        size_t colon_index = line.find(':');
        uint32_t base_addr = (uint32_t)stoul(line.substr(0, colon_index), nullptr, 16);

        string raw_bytes = line.substr(colon_index + 1);
        string processed_bytes;
        for(size_t i = 0; i < raw_bytes.size(); i++){
            if(raw_bytes[i] == '/') break;
            if(raw_bytes[i] == ' ' || raw_bytes[i] == '\t') continue;
            processed_bytes += raw_bytes[i];
        }
        if (processed_bytes.size() % 2 != 0){
            throw runtime_error("Odd Number of Hex Chars"); //if ever occurs, always incorrect
        }
        uint32_t addr = base_addr;
        for(size_t i = 0; i < processed_bytes.size(); i += 2, addr++){
            string byte_str = processed_bytes.substr(i, 2);
            uint8_t byte = (uint8_t)stoul(byte_str, nullptr, 16);

            mem[addr] = byte; //each byte gets own mem loc
        }
    }
    inputFile.close();
}

bool w_bit_set(uint8_t opcode){
    if(opcode & 0x01) return true;
    return false;
}

bool sext_bit_set(uint8_t opcode){
    if(opcode & 0x02) return true;
    return false;
}

modrm_t get_modrm_byte(uint8_t modrm_byte){
    modrm_t modrm;
    modrm.mod = (modrm_byte & 0xC0) >> 6;
    modrm.reg = (modrm_byte & 0x38) >> 3;
    modrm.r_m = (modrm_byte & 0x07);
    return modrm;
}

bool parity(int num, int num_bits){
    num &= 0x0FF;
    while(num_bits > 1){
        num ^= num >> (num_bits/2);
        num_bits /= 2;
    }
    if(num & 0x00000001) return false;
    else return true;
}

bool adjust(int op1, int op2){
    int bcd1 = op1 & 0x0000000F;
    int bcd2 = op2 & 0x0000000F;
    int result = bcd1 + bcd2;
    if(result & 0x000010) return true;
    return false;
}

void update_flags_add(int operand1, int operand2, int num_bits){
    int64_t sum = (operand1 & 0x0FFFFFFFF) + (operand1 & 0x0FFFFFFFF);
    int sign_mask = 1 << (num_bits - 1);

    if((sum >> num_bits) & 0x01) next_state.FLAGS[CF] = true;
    else next_state.FLAGS[CF] = false;

    next_state.FLAGS[PF] = parity(sum, 8);
    next_state.FLAGS[AF] = adjust(operand1, operand2);

    if(sum == 0) next_state.FLAGS[ZF] = true;
    else next_state.FLAGS[ZF] = false;

    if(sum & sign_mask) next_state.FLAGS[SF] = true;
    else next_state.FLAGS[SF] = false;

    if((((operand1 ^ operand2) & sign_mask) == 0) && (((operand1 ^ sum) & sign_mask) != 0)) next_state.FLAGS[OF] = true;
    else next_state.FLAGS[OF] = false;
} 

uint32_t ea_sib_32bits(modrm_t sib_byte, int mod){
    uint32_t EA_sib = 0;
    uint32_t base = 0;
    int scale = 0;
    if(sib_byte.mod == 0){
        scale = 1;
    }
    else if(sib_byte.mod == 1){
        scale = 2;
    }
    else if(sib_byte.mod == 2){
        scale = 4;
    }
    else if(sib_byte.mod == 3){
        scale = 8;
    }
    switch (sib_byte.r_m){
        case 0:
            base = curr_state.GPR[EAX];
            break;
        case 1:
            base = curr_state.GPR[ECX];
            break;
        case 2:
            base = curr_state.GPR[EDX];
            break;
        case 3:
            base = curr_state.GPR[EBX];
            break;
        case 4:
            base = curr_state.GPR[ESP];
            break;
        case 5:
            if(mod == 0) base = 0;
            else base = curr_state.GPR[EBP];
            break;
        case 6:
            base = curr_state.GPR[ESI];
            break;
        case 7:
            base = curr_state.GPR[EDI];
            break;
    }
    switch (sib_byte.reg){
        case 0:
            EA_sib = curr_state.GPR[EAX] * scale + base;
            break;
        case 1:
            EA_sib = curr_state.GPR[ECX] * scale + base;
            break;
        case 2:
            EA_sib = curr_state.GPR[EDX] * scale + base;
            break;
        case 3:
            EA_sib = curr_state.GPR[EBX] * scale + base;
            break;
        case 4:
            EA_sib = base;
            break;
        case 5:
            EA_sib = curr_state.GPR[EBP] * scale + base;
            break;
        case 6:
            EA_sib = curr_state.GPR[ESI] * scale + base;
            break;
        case 7:
            EA_sib = curr_state.GPR[EDI] * scale + base;
            break;
        }  
        return EA_sib;
}

uint32_t ea_modrm_32bits(modrm_t modrm_byte, int32_t disp, int SIB_address){
    uint32_t EA = 0;
    if(modrm_byte.mod == 0){
            switch (modrm_byte.r_m){
                case 0:
                    EA = curr_state.GPR[EAX];
                    break;
                case 1:
                    EA = curr_state.GPR[ECX];
                    break;
                case 2:
                    EA = curr_state.GPR[EDX];
                    break;
                case 3:
                    EA = curr_state.GPR[EBX];
                    break;
                case 4: //SIB
                    EA = SIB_address;
                    break;
                case 5:
                    EA = disp;
                    break;
                case 6:
                    EA = curr_state.GPR[ESI];
                    break;
                case 7:
                    EA = curr_state.GPR[EDI];
                    break;
            }
        }
        else if(modrm_byte.mod == 1){
            switch (modrm_byte.r_m){
                case 0:
                    EA = curr_state.GPR[EAX] + disp;
                    break;
                case 1:
                    EA = curr_state.GPR[ECX] + disp;
                    break;
                case 2:
                    EA = curr_state.GPR[EDX] + disp;
                    break;
                case 3:
                    EA = curr_state.GPR[EBX] + disp;
                    break;
                case 4: //SIB 
                    EA = SIB_address + disp; 
                    break;
                case 5:
                    EA = curr_state.GPR[EBP] + disp;
                    break;
                case 6:
                    EA = curr_state.GPR[ESI] + disp;
                    break;
                case 7:
                    EA = curr_state.GPR[EDI] + disp;
                    break;
            }
        }
        else if(modrm_byte.mod == 2){
            switch (modrm_byte.r_m){
                case 0:
                    EA = curr_state.GPR[EAX] + disp;
                    break;
                case 1:
                    EA = curr_state.GPR[ECX] + disp;
                    break;
                case 2:
                    EA = curr_state.GPR[EDX] + disp;
                    break;
                case 3:
                    EA = curr_state.GPR[EBX] + disp;
                    break;
                case 4: //SIB 
                    EA = SIB_address + disp; 
                    break;
                case 5:
                    EA = curr_state.GPR[EBP] + disp;
                    break;
                case 6:
                    EA = curr_state.GPR[ESI] + disp;
                    break;
                case 7:
                    EA = curr_state.GPR[EDI] + disp;
                    break;
            }
        }
    return EA;
}

int eval_reg(int reg_rm){
    switch (reg_rm){
        case 0:
            reg_rm = EAX;
            break;
        case 1:
            reg_rm = ECX;
            break;
        case 2:
            reg_rm = EDX;
            break;
        case 3:
            reg_rm = EBX;
            break;
        case 4:
            reg_rm = ESP;
            break;
        case 5:
            reg_rm = EBP;
            break;
        case 6:
            reg_rm = ESI;
            break;
        case 7:
            reg_rm = EDI;
            break;
    }
    return reg_rm;
}

void fetch_and_execute(){
    int bytes_fetched = 0;
    curr_state.INSTR.clear();
    // set segemnt registers for correct access (CS for fetch and DS for any other acess)
    uint32_t CS_BASE = (uint32_t)((uint16_t)curr_state.SEGR[CS]) << 16;
    uint32_t DS_BASE = (uint32_t)((uint16_t)curr_state.SEGR[DS]) << 16;

    //helpers 
    auto fetch8 = [&](uint32_t off){
        return mem[CS_BASE + off];
    };

    auto read8_data = [&](uint32_t off){
        return mem[DS_BASE + off];
    };

    auto write8_data = [&](uint32_t off, uint8_t value){
        mem[DS_BASE + off] = value;
    };

    auto readN_data = [&](uint32_t off, int nbytes){
        uint64_t value = 0;
        for(int i = 0; i < nbytes; i++){
            uint64_t byte = (uint64_t)read8_data(off + i);
            value = value + (byte << (8*i));
        }
        return value;
    };

    auto writeN_data = [&](uint32_t off, int nbytes, uint64_t value){
        for(int i = 0; i < nbytes; i++){
            uint8_t byte = (uint8_t)((value >> (8*i)) & 0xFF);
            write8_data(off + i, byte);
        }
    };

    //fetch first byte
    curr_state.INSTR.push_back(fetch8(curr_state.EIP + bytes_fetched));
    bytes_fetched++;

    //cout << "Initial Byte Fetched: " << hex << (int)curr_state.INSTR[0] << '\n';

    //check for x66 prefix
    bool has_prefix_x66 = false;
    if(curr_state.INSTR[0] == 0x66) {
        has_prefix_x66 = true;
        curr_state.INSTR.push_back(fetch8(curr_state.EIP + bytes_fetched));
        bytes_fetched++;
    }

    uint8_t opcode_B1 = curr_state.INSTR[bytes_fetched - 1];
    bool w_bit = w_bit_set(opcode_B1);
    bool s_bit = sext_bit_set(opcode_B1);

    if(opcode_B1 == 0x04 || opcode_B1 == 0x05){//add to EAX, AX, AL
        if(has_prefix_x66){ //16 bit add to AX
            int imm_length = 2;
            uint16_t imm = 0;
            for(int i = 0; i < imm_length; i++){
                int new_byte = fetch8(curr_state.EIP + bytes_fetched);
                curr_state.INSTR.push_back(new_byte);
                bytes_fetched++;
                imm |= (new_byte << (8*i));
            }
            int result = (((next_state.GPR[EAX] & 0x0000FFFF) + imm) & 0x0FFFF);
            update_flags_add(imm, next_state.GPR[EAX] & 0x0000FFFF, 16);
            next_state.GPR[EAX] = (next_state.GPR[EAX] & 0xFFFF0000) + result;
        }
        else if(opcode_B1 & 0x01){ //32 bit add to EAX
            int imm_length = 4;
            uint32_t imm = 0;
            for(int i = 0; i < imm_length; i++){
                int new_byte = fetch8(curr_state.EIP + bytes_fetched);
                curr_state.INSTR.push_back(new_byte);
                bytes_fetched++;
                imm |= (new_byte << (8*i));
            }
            int result = (next_state.GPR[EAX]) + (int32_t)imm;
            update_flags_add(imm, next_state.GPR[EAX], 32);
            next_state.GPR[EAX] = result;
        }
        else{ //8 bit add to AL
            int imm_length = 1;
            uint32_t imm = 0;
            for(int i = 0; i < imm_length; i++){
                int new_byte = fetch8(curr_state.EIP + bytes_fetched);
                curr_state.INSTR.push_back(new_byte);
                bytes_fetched++;
                imm |= (new_byte << (8*i));
            }
            int result = (((next_state.GPR[EAX] & 0x000000FF) + imm) & 0x0FF);
            update_flags_add(imm, next_state.GPR[EAX] & 0x000000FF, 8);
            next_state.GPR[EAX] = (next_state.GPR[EAX] & 0xFFFFFF00) + result;
        }
        next_state.EIP = curr_state.EIP + bytes_fetched;
    }
    else if(opcode_B1 == 0x80 || opcode_B1 == 0x81 || opcode_B1 == 0x83){ // r/m adds with immediate
        curr_state.INSTR.push_back(fetch8(curr_state.EIP + bytes_fetched));
        modrm_t modrm_byte = get_modrm_byte(curr_state.INSTR[bytes_fetched]);
        bytes_fetched++;

        bool SIB = false;
        modrm_t SIB_byte;
        if((modrm_byte.mod != 3) && (modrm_byte.r_m == 4)) SIB = true;
        if(SIB){
            curr_state.INSTR.push_back(fetch8(curr_state.EIP + bytes_fetched));
            SIB_byte = get_modrm_byte(curr_state.INSTR[bytes_fetched]);
            bytes_fetched++;
        }

        if(modrm_byte.mod == 3){ //reg mode
            int dest_reg = 0;
            switch (modrm_byte.r_m){
                case 0: 
                    dest_reg = EAX; 
                    break;
                case 1: 
                    dest_reg = ECX; 
                    break;
                case 2: 
                    dest_reg = EDX; 
                    break;
                case 3: 
                    dest_reg = EBX; 
                    break;
                case 4: 
                    dest_reg = ESP; 
                    break;
                case 5: 
                    dest_reg = EBP; 
                    break;
                case 6: 
                    dest_reg = ESI; 
                    break;
                case 7: 
                    dest_reg = EDI; 
                    break;
            }

            if(has_prefix_x66){ // 16-bit
                int imm_length = 0;
                if (s_bit) imm_length = 1;
                else imm_length = 2;

                int imm = 0;
                for(int i = 0; i < imm_length; i++){
                    int new_byte = fetch8(curr_state.EIP + bytes_fetched);
                    curr_state.INSTR.push_back(new_byte);
                    bytes_fetched++;
                    imm |= (new_byte << (8*i));
                }
                if(s_bit){
                    if(imm & 0x80) imm |= 0xFFFFFF00; 
                }

                int result = ((curr_state.GPR[dest_reg] & 0x0000FFFF) + imm) & 0x0FFFF;
                update_flags_add(curr_state.GPR[dest_reg] & 0x0000FFFF, imm, 16);
                next_state.GPR[dest_reg] = (curr_state.GPR[dest_reg] & 0xFFFF0000) + result;
            }
            else if(w_bit){ // 32 bit
                int imm_length = 0;
                if (s_bit) imm_length = 1;
                else imm_length = 4;

                int imm = 0;
                for(int i = 0; i < imm_length; i++){
                    int new_byte = fetch8(curr_state.EIP + bytes_fetched);
                    curr_state.INSTR.push_back(new_byte);
                    bytes_fetched++;
                    imm |= (new_byte << (8*i));
                }
                if(s_bit){
                    if(imm & 0x80) imm |= 0xFFFFFF00;
                }

                int result = (curr_state.GPR[dest_reg] + imm);
                update_flags_add(curr_state.GPR[dest_reg], imm, 32);
                next_state.GPR[dest_reg] = result;
            }
            else{ // 8 bit
                int imm_length = 1;
                int imm = 0;
                for(int i = 0; i < imm_length; i++){
                    int new_byte = fetch8(curr_state.EIP + bytes_fetched);
                    curr_state.INSTR.push_back(new_byte);
                    bytes_fetched++;
                    imm |= (new_byte << (8*i));
                }

                if(dest_reg < 4){
                    int result = ((curr_state.GPR[dest_reg] & 0x000000FF) + imm) & 0x0FF;
                    update_flags_add(curr_state.GPR[dest_reg] & 0x000000FF, imm, 8);
                    next_state.GPR[dest_reg] = (curr_state.GPR[dest_reg] & 0xFFFFFF00) + result;
                }
                else{
                    int result = (((curr_state.GPR[dest_reg % 4] & 0x0000FF00)>>8) + imm) & 0x0FF;
                    update_flags_add(((curr_state.GPR[dest_reg % 4] & 0x0000FF00)>>8), imm, 8);
                    next_state.GPR[dest_reg % 4] = (curr_state.GPR[dest_reg % 4] & 0xFFFF00FF) + (result<<8);
                }
            }
        }
        else{
            int32_t disp = 0;
            int disp_bytes = 0;
            if((modrm_byte.mod == 0 && modrm_byte.r_m == 5) || modrm_byte.mod == 2) disp_bytes = 4;
            else if (modrm_byte.mod == 1) disp_bytes = 1;

            for(int i = 0; i < disp_bytes; i++){
                int new_byte = fetch8(curr_state.EIP + bytes_fetched);
                curr_state.INSTR.push_back(new_byte);
                bytes_fetched++;
                disp |= (new_byte << (8*i));
            }
            if(disp_bytes == 1){
                if(disp & 0x80) disp |= 0xFFFFFF00;
            }

            int sib_address = 0;
            if(SIB) sib_address = ea_sib_32bits(SIB_byte, modrm_byte.mod);

            uint32_t EA = (uint32_t)ea_modrm_32bits(modrm_byte, disp, sib_address);

            int mem_loc_value = 0;

            if(has_prefix_x66){ //16 bit r/m
                int imm_length = (s_bit ? 1 : 2);
                int imm = 0;
                for(int i = 0; i < imm_length; i++){
                    int new_byte = fetch8(curr_state.EIP + bytes_fetched);
                    curr_state.INSTR.push_back(new_byte);
                    bytes_fetched++;
                    imm |= (new_byte << (8*i));
                }
                if(s_bit){
                    if(imm & 0x80) imm |= 0xFFFFFF00;
                }

                mem_loc_value = (int)readN_data(EA, 2);
                uint16_t result = (uint16_t)((mem_loc_value + imm) & 0xFFFF);
                update_flags_add(imm, mem_loc_value, 16);
                writeN_data(EA, 2, result);
            }
            else if(w_bit){ //32 bit r/m
                int imm_length = (s_bit ? 1 : 4);
                int imm = 0;
                for(int i = 0; i < imm_length; i++){
                    int new_byte = fetch8(curr_state.EIP + bytes_fetched);
                    curr_state.INSTR.push_back(new_byte);
                    bytes_fetched++;
                    imm |= (new_byte << (8*i));
                }
                if(s_bit){
                    if(imm & 0x80) imm |= 0xFFFFFF00;
                }

                mem_loc_value = (int32_t)readN_data(EA, 4);
                int32_t result = (int32_t)(mem_loc_value + imm);
                update_flags_add(imm, mem_loc_value, 32);
                writeN_data(EA, 4, (uint32_t)result);
            }
            else{ //8 bit r/m
                int imm_length = 1;
                int imm = 0;
                for(int i = 0; i < imm_length; i++){
                    int new_byte = fetch8(curr_state.EIP + bytes_fetched);
                    curr_state.INSTR.push_back(new_byte);
                    bytes_fetched++;
                    imm |= (new_byte << (8*i));
                }

                mem_loc_value = (int)readN_data(EA, 1);
                uint8_t result = (uint8_t)((mem_loc_value + imm) & 0xFF);
                update_flags_add(imm, mem_loc_value, 8);
                writeN_data(EA, 1, result);
            }
        }

        next_state.EIP = curr_state.EIP + bytes_fetched;
    }
    else if(opcode_B1 == 0x00 || opcode_B1 == 0x01 || opcode_B1 == 0x02 || opcode_B1 == 0x03){ //r/m adds no immediate 
        curr_state.INSTR.push_back(fetch8(curr_state.EIP + bytes_fetched));
        modrm_t modrm_byte = get_modrm_byte(curr_state.INSTR[bytes_fetched]);
        bytes_fetched++;

        bool SIB = false;
        modrm_t SIB_byte;
        if((modrm_byte.mod != 3) && (modrm_byte.r_m == 4)) SIB = true;
        if(SIB){
            curr_state.INSTR.push_back(fetch8(curr_state.EIP + bytes_fetched));
            SIB_byte = get_modrm_byte(curr_state.INSTR[bytes_fetched]);
            bytes_fetched++;
        }

        if(modrm_byte.mod == 3){ //reg to reg
            int reg_REG = eval_reg(modrm_byte.reg);
            int reg_rm  = eval_reg(modrm_byte.r_m);

            if(has_prefix_x66){ //16-bit add
                int value_reg = curr_state.GPR[reg_REG] & 0x0000FFFF;
                int value_rm  = curr_state.GPR[reg_rm]  & 0x0000FFFF;
                int dest_reg = 0;
                if(opcode_B1 & 0x02) dest_reg = reg_rm;   
                else dest_reg = reg_rm;                   
                if(!(opcode_B1 & 0x02)) dest_reg = reg_rm; else dest_reg = reg_REG;

                int result = (value_reg + value_rm) & 0x0FFFF;
                update_flags_add(value_reg, value_rm, 16);
                next_state.GPR[dest_reg] = (curr_state.GPR[dest_reg] & 0xFFFF0000) + result;
            }
            else if(w_bit){ // 32 bit add
                int value_reg = curr_state.GPR[reg_REG];
                int value_rm  = curr_state.GPR[reg_rm];
                int dest_reg = 0;
                if(opcode_B1 & 0x02) dest_reg = reg_REG; else dest_reg = reg_rm;
                int result = (value_reg + value_rm);
                update_flags_add(value_reg, value_rm, 32);
                next_state.GPR[dest_reg] = result;
            }
            else { //8 bit add
                int value_reg = curr_state.GPR[reg_REG] & 0x000000FF;
                int value_rm  = curr_state.GPR[reg_rm]  & 0x000000FF;
                int dest_reg = 0;
                if(opcode_B1 & 0x02) dest_reg = reg_REG; else dest_reg = reg_rm;
                int result = (value_reg + value_rm) & 0x0FF;
                update_flags_add(value_reg, value_rm, 8);
                next_state.GPR[dest_reg] = (curr_state.GPR[dest_reg] & 0xFFFFFF00) + result;
            }
        }
        else{
            int32_t disp = 0;
            int disp_bytes = 0;
            if((modrm_byte.mod == 0 && modrm_byte.r_m == 5) || modrm_byte.mod == 2) disp_bytes = 4;
            else if (modrm_byte.mod == 1) disp_bytes = 1;

            for(int i = 0; i < disp_bytes; i++){
                int new_byte = fetch8(curr_state.EIP + bytes_fetched);
                curr_state.INSTR.push_back(new_byte);
                bytes_fetched++;
                disp |= (new_byte << (8*i));
            }
            if(disp_bytes == 1){
                if(disp & 0x80) disp |= 0xFFFFFF00;
            }

            int sib_address = 0;
            if(SIB) sib_address = ea_sib_32bits(SIB_byte, modrm_byte.mod);
            uint32_t EA = (uint32_t)ea_modrm_32bits(modrm_byte, disp, sib_address);

            int source_reg_name = eval_reg(modrm_byte.reg);

            bool store_to_mem_op = false;
            if(opcode_B1 & 0x02) store_to_mem_op = false; // 02/03 write to REG
            else store_to_mem_op = true;                  // 00/01 write to r/m

            if(has_prefix_x66){ //16 bit add
                int mem_loc_value = (int)readN_data(EA, 2);
                int reg_val = curr_state.GPR[source_reg_name] & 0x0000FFFF;

                int result = (mem_loc_value + reg_val) & 0x0FFFF;
                update_flags_add(reg_val, mem_loc_value, 16);

                if(store_to_mem_op) writeN_data(EA, 2, (uint16_t)result);
                else next_state.GPR[source_reg_name] = (curr_state.GPR[source_reg_name] & 0xFFFF0000) + (result & 0xFFFF);
            }
            else if(w_bit){ // 32 bit add
                int32_t mem_loc_value = (int32_t)readN_data(EA, 4);
                int32_t reg_val = curr_state.GPR[source_reg_name];

                int32_t result = mem_loc_value + reg_val;
                update_flags_add(reg_val, mem_loc_value, 32);

                if(store_to_mem_op) writeN_data(EA, 4, (uint32_t)result);
                else next_state.GPR[source_reg_name] = result;
            }
            else{ //8 bit add
                int mem_loc_value = (int)readN_data(EA, 1);
                int reg_index = source_reg_name;
                if(reg_index < 4){
                    int reg_val = curr_state.GPR[reg_index] & 0xFF;

                    if(store_to_mem_op){
                        int result = (mem_loc_value + reg_val) & 0xFF;
                        update_flags_add(reg_val, mem_loc_value, 8);
                        writeN_data(EA, 1, (uint8_t)result);
                    }
                    else{
                        int result = (reg_val + mem_loc_value) & 0xFF;
                        update_flags_add(reg_val, mem_loc_value, 8);
                        next_state.GPR[reg_index] = (curr_state.GPR[reg_index] & 0xFFFFFF00) + result;
                    }
                }
                else{
                    int lo_reg = reg_index % 4;
                    int reg_val = (curr_state.GPR[lo_reg] & 0x0000FF00) >> 8;

                    if(store_to_mem_op){
                        int result = (mem_loc_value + reg_val) & 0xFF;
                        update_flags_add(reg_val, mem_loc_value, 8);
                        writeN_data(EA, 1, (uint8_t)result);
                    }
                    else{
                        int result = (reg_val + mem_loc_value) & 0xFF;
                        update_flags_add(reg_val, mem_loc_value, 8);
                        next_state.GPR[lo_reg] = (curr_state.GPR[lo_reg] & 0xFFFF00FF) + (result << 8);
                    }
                }
            }
        }
        next_state.EIP = curr_state.EIP + bytes_fetched;
    }
    else if(opcode_B1 == 0x0F){ // multi-byte opcode instrs
        curr_state.INSTR.push_back(fetch8(curr_state.EIP + bytes_fetched));
        bytes_fetched++;
        int opcode_B2 = curr_state.INSTR[bytes_fetched - 1];
        if(opcode_B2 == 0x85){ //JNE
            int32_t disp32 = 0;
            for(int i = 0; i < 4; i++){
                int new_byte = fetch8(curr_state.EIP + bytes_fetched);
                curr_state.INSTR.push_back(new_byte);
                bytes_fetched++;
                disp32 |= (new_byte << (8*i));
            }
            
            uint32_t EIP_NT = curr_state.EIP + bytes_fetched;
            // cout << hex << "displacement: " << disp32 << endl;
            // cout << hex << "new EIP no disp" << EIP_NT << endl; 
            if(curr_state.FLAGS[ZF] == false){
                next_state.EIP = (int32_t)(EIP_NT + disp32);
            }
            else{
                next_state.EIP = (int32_t)EIP_NT;
            }
        }

        else if(opcode_B2 == 0xB1){ //CMPXCHG (16 bit operands)
            curr_state.INSTR.push_back(fetch8(curr_state.EIP + bytes_fetched));
            modrm_t modrm_byte = get_modrm_byte(curr_state.INSTR[bytes_fetched]);
            bytes_fetched++;
            int reg_r16 = eval_reg(modrm_byte.reg);

            if(modrm_byte.mod == 3){
                int rm_reg = eval_reg(modrm_byte.r_m);

                uint16_t AX_val = (uint16_t)(curr_state.GPR[EAX] & 0xFFFF);
                uint16_t rm_reg_val = (uint16_t)(curr_state.GPR[rm_reg] & 0xFFFF);
                uint16_t reg_REG_val = (uint16_t)(curr_state.GPR[reg_r16] & 0xFFFF);

                if(AX_val == rm_reg_val){
                    next_state.FLAGS[ZF] = true;
                    next_state.GPR[rm_reg] = (curr_state.GPR[rm_reg] & 0xFFFF0000) + (uint16_t)reg_REG_val;
                }
                else{
                    next_state.FLAGS[ZF] = false;
                    next_state.GPR[EAX] = (curr_state.GPR[EAX] & 0xFFFF0000) + (uint16_t)rm_reg_val;
                }
            }
            else{
                bool SIB = false;
                modrm_t SIB_byte;
                if((modrm_byte.mod != 3) && (modrm_byte.r_m == 4)) SIB = true;
                if(SIB){
                    curr_state.INSTR.push_back(fetch8(curr_state.EIP + bytes_fetched));
                    SIB_byte = get_modrm_byte(curr_state.INSTR[bytes_fetched]);
                    bytes_fetched++;
                }

                int32_t disp = 0;
                int disp_bytes = 0;
                if((modrm_byte.mod == 0 && modrm_byte.r_m == 5) || modrm_byte.mod == 2) disp_bytes = 4;
                else if(modrm_byte.mod == 1) disp_bytes = 1;

                for(int i = 0; i < disp_bytes; i++){
                    int new_byte = fetch8(curr_state.EIP + bytes_fetched);
                    curr_state.INSTR.push_back(new_byte);
                    bytes_fetched++;
                    disp |= (new_byte << (8*i));
                }
                if(disp_bytes == 1){
                    if(disp & 0x80) disp |= 0xFFFFFF00;
                }

                int sib_address = 0;
                if(SIB) sib_address = ea_sib_32bits(SIB_byte, modrm_byte.mod);

                uint32_t EA = (uint32_t)ea_modrm_32bits(modrm_byte, disp, sib_address);

                uint16_t AX_val = (uint16_t)(curr_state.GPR[EAX] & 0xFFFF);
                uint16_t rm_reg_val = (uint16_t)readN_data(EA, 2);
                uint16_t reg_REG_val = (uint16_t)(curr_state.GPR[reg_r16] & 0xFFFF);

                if(AX_val == rm_reg_val){
                    next_state.FLAGS[ZF] = true;
                    writeN_data(EA, 2, reg_REG_val);
                }else{
                    next_state.FLAGS[ZF] = false;
                    next_state.GPR[EAX] = (curr_state.GPR[EAX] & 0xFFFF0000) + rm_reg_val;
                }
            }
            next_state.EIP = curr_state.EIP + bytes_fetched;
        }
        else{ //MOVQ (MMX)
            curr_state.INSTR.push_back(fetch8(curr_state.EIP + bytes_fetched));
            modrm_t modrm_byte = get_modrm_byte(curr_state.INSTR[bytes_fetched]);
            bytes_fetched++;

            if(modrm_byte.mod == 3){
                int source_reg, dest_reg;
                if(opcode_B2 == 0xD6){source_reg = modrm_byte.reg; dest_reg = modrm_byte.r_m;}
                else {dest_reg = modrm_byte.reg; source_reg = modrm_byte.r_m;}
                next_state.MMX[dest_reg] = curr_state.MMX[source_reg];
            }
            else{
                int dest_reg = modrm_byte.reg;

                bool SIB = false;
                modrm_t SIB_byte;
                if((modrm_byte.mod != 3) && (modrm_byte.r_m == 4)) SIB = true;
                if(SIB){
                    curr_state.INSTR.push_back(fetch8(curr_state.EIP + bytes_fetched));
                    SIB_byte = get_modrm_byte(curr_state.INSTR[bytes_fetched]);
                    bytes_fetched++;
                }

                int32_t disp = 0;
                int disp_bytes = 0;
                if((modrm_byte.mod == 0 && modrm_byte.r_m == 5) || modrm_byte.mod == 2) disp_bytes = 4;
                else if (modrm_byte.mod == 1) disp_bytes = 1;

                for(int i = 0; i < disp_bytes; i++){
                    int new_byte = fetch8(curr_state.EIP + bytes_fetched);
                    curr_state.INSTR.push_back(new_byte);
                    bytes_fetched++;
                    disp |= (new_byte << (8*i));
                }
                if(disp_bytes == 1){
                    if(disp & 0x80) disp |= 0xFFFFFF00;
                }

                int sib_address = 0;
                if(SIB) sib_address = ea_sib_32bits(SIB_byte, modrm_byte.mod);
                uint32_t EA = (uint32_t)ea_modrm_32bits(modrm_byte, disp, sib_address);

                int64_t mem_loc_value = (int64_t)readN_data(EA, 8);
                next_state.MMX[dest_reg] = mem_loc_value;
            }

            next_state.EIP = curr_state.EIP + bytes_fetched;
        }
    }
    else if(opcode_B1 == 0x8E){ //MOV to SREG
        curr_state.INSTR.push_back(fetch8(curr_state.EIP + bytes_fetched));
        modrm_t modrm_byte = get_modrm_byte(curr_state.INSTR[bytes_fetched]);
        bytes_fetched++;

        if(modrm_byte.mod == 3){
            int source_reg_value = curr_state.GPR[modrm_byte.r_m] & 0x0000FFFF;
            int dest_sreg = modrm_byte.reg;
            switch (dest_sreg){
                case 0: 
                    dest_sreg = ES; 
                    break;
                case 1: 
                    dest_sreg = CS; 
                    break;
                case 2: 
                    dest_sreg = SS; 
                    break;
                case 3: 
                    dest_sreg = DS; 
                    break;
                case 4: 
                    dest_sreg = FS; 
                    break;
                case 5: 
                    dest_sreg = GS; 
                    break;
            }
            next_state.SEGR[dest_sreg] = (int16_t)source_reg_value;
        }
        else{
            int dest_reg = modrm_byte.reg;

            bool SIB = false;
            modrm_t SIB_byte;
            if((modrm_byte.mod != 3) && (modrm_byte.r_m == 4)) SIB = true;
            if(SIB){
                curr_state.INSTR.push_back(fetch8(curr_state.EIP + bytes_fetched));
                SIB_byte = get_modrm_byte(curr_state.INSTR[bytes_fetched]);
                bytes_fetched++;
            }

            int32_t disp = 0;
            int disp_bytes = 0;
            if((modrm_byte.mod == 0 && modrm_byte.r_m == 5) || modrm_byte.mod == 2) disp_bytes = 4;
            else if (modrm_byte.mod == 1) disp_bytes = 1;

            for(int i = 0; i < disp_bytes; i++){
                int new_byte = fetch8(curr_state.EIP + bytes_fetched);
                curr_state.INSTR.push_back(new_byte);
                bytes_fetched++;
                disp |= (new_byte << (8*i));
            }
            if(disp_bytes == 1){
                if(disp & 0x80) disp |= 0xFFFFFF00;
            }
            int sib_address = 0;
            if(SIB) sib_address = ea_sib_32bits(SIB_byte, modrm_byte.mod);
            uint32_t EA = (uint32_t)ea_modrm_32bits(modrm_byte, disp, sib_address);
            int16_t mem_loc_value = (int16_t)readN_data(EA, 2);
            next_state.SEGR[dest_reg] = mem_loc_value;
        }
        next_state.EIP = curr_state.EIP + bytes_fetched;
    }

    else if(opcode_B1 == 0x86){ //XCHG (8 bits)
        curr_state.INSTR.push_back(fetch8(curr_state.EIP + bytes_fetched));
        modrm_t modrm_byte = get_modrm_byte(curr_state.INSTR[bytes_fetched]);
        bytes_fetched++;

        if(modrm_byte.mod == 3){
            int reg1_name = eval_reg(modrm_byte.reg);
            int reg2_name = eval_reg(modrm_byte.r_m);

            if(reg1_name < 4 && reg2_name < 4){
                int reg1_val = curr_state.GPR[reg1_name] & 0x000000FF;
                int reg2_val = curr_state.GPR[reg2_name] & 0x000000FF;
                next_state.GPR[reg2_name] = (curr_state.GPR[reg2_name] & 0xFFFFFF00) + reg1_val;
                next_state.GPR[reg1_name] = (curr_state.GPR[reg1_name] & 0xFFFFFF00) + reg2_val;
            }
            else if(reg1_name >= 4 && reg2_name < 4){
                int reg1_val = (curr_state.GPR[reg1_name % 4] & 0x0000FF00) >> 8;
                int reg2_val = curr_state.GPR[reg2_name] & 0x000000FF;
                next_state.GPR[reg2_name] = (curr_state.GPR[reg2_name] & 0xFFFFFF00) + reg1_val;
                next_state.GPR[reg1_name % 4] = (curr_state.GPR[reg1_name% 4] & 0xFFFF00FF) + (reg2_val << 8);
            }
            else if(reg1_name < 4 && reg2_name >= 4){
                int reg2_val = (curr_state.GPR[reg2_name % 4] & 0x0000FF00) >> 8;
                int reg1_val = curr_state.GPR[reg1_name] & 0x000000FF;
                next_state.GPR[reg1_name] = (curr_state.GPR[reg1_name] & 0xFFFFFF00) + reg2_val;
                next_state.GPR[reg2_name % 4] = (curr_state.GPR[reg2_name % 4] & 0xFFFF00FF) + (reg1_val << 8);
            }
            else{
                int reg1_val = (curr_state.GPR[reg1_name % 4] & 0x0000FF00) >> 8;
                int reg2_val = (curr_state.GPR[reg2_name % 4] & 0x0000FF00) >> 8;
                next_state.GPR[reg2_name % 4] = (curr_state.GPR[reg2_name % 4] & 0xFFFF00FF) + (reg1_val << 8);
                next_state.GPR[reg1_name % 4] = (curr_state.GPR[reg1_name % 4] & 0xFFFF00FF) + (reg2_val << 8);
            }
        }
        else{
            int dest_reg = modrm_byte.reg;

            bool SIB = false;
            modrm_t SIB_byte;
            if((modrm_byte.mod != 3) && (modrm_byte.r_m == 4)) SIB = true;
            if(SIB){
                curr_state.INSTR.push_back(fetch8(curr_state.EIP + bytes_fetched));
                SIB_byte = get_modrm_byte(curr_state.INSTR[bytes_fetched]);
                bytes_fetched++;
            }

            int32_t disp = 0;
            int disp_bytes = 0;
            if((modrm_byte.mod == 0 && modrm_byte.r_m == 5) || modrm_byte.mod == 2) disp_bytes = 4;
            else if (modrm_byte.mod == 1) disp_bytes = 1;

            for(int i = 0; i < disp_bytes; i++){
                int new_byte = fetch8(curr_state.EIP + bytes_fetched);
                curr_state.INSTR.push_back(new_byte);
                bytes_fetched++;
                disp |= (new_byte << (8*i));
            }
            if(disp_bytes == 1){
                if(disp & 0x80) disp |= 0xFFFFFF00;
            }

            int sib_address = 0;
            if(SIB) sib_address = ea_sib_32bits(SIB_byte, modrm_byte.mod);
            uint32_t EA = (uint32_t)ea_modrm_32bits(modrm_byte, disp, sib_address);

            uint8_t mem_val = (uint8_t)readN_data(EA, 1);

            if(dest_reg < 4){
                uint8_t reg_val = (uint8_t)(curr_state.GPR[dest_reg] & 0xFF);
                next_state.GPR[dest_reg] = (curr_state.GPR[dest_reg] & 0xFFFFFF00) + mem_val;
                writeN_data(EA, 1, reg_val);
            }
            else{
                uint8_t reg_val = (uint8_t)((curr_state.GPR[dest_reg % 4] & 0x0000FF00) >> 8);
                next_state.GPR[dest_reg % 4] = (curr_state.GPR[dest_reg % 4] & 0xFFFF00FF) + ((uint32_t)mem_val << 8);
                writeN_data(EA, 1, reg_val);
            }
        }

        next_state.EIP = curr_state.EIP + bytes_fetched;
    }
    else if(opcode_B1 == 0xEA){
        uint32_t off32 = 0;
        for(int i = 0; i < 4; i++){
            int new_byte = fetch8(curr_state.EIP + bytes_fetched);
            curr_state.INSTR.push_back(new_byte);
            bytes_fetched++;
            off32 |= ((uint32_t)new_byte << (8*i));
        }

        uint16_t sel16 = 0;
        for(int i = 0; i < 2; i++){
            int new_byte = fetch8(curr_state.EIP + bytes_fetched);
            curr_state.INSTR.push_back(new_byte);
            bytes_fetched++;
            sel16 |= ((uint16_t)new_byte << (8*i));
        }

        next_state.SEGR[CS] = (int16_t)sel16;
        next_state.EIP = (int32_t)off32;
    }

    else if(opcode_B1 == 0xF4){
        run = false;
        cout<< "x86 Program Executed from file mem.txt" << endl;
    }

    else{
        // Unknown opcode exception: halt machine
        cout << "Unimplemented opcode: 0x" << hex << (int)opcode_B1 << dec << "\n";
        run = false;
    }
}

//The Formatting Framework Functions for Dump Files Below were Generated by an LLM and editted by Me
//For the visual pleasure of the TA grading this work
uint32_t u32(int32_t x) { return static_cast<uint32_t>(x); }
uint16_t lo16(uint32_t x) { return static_cast<uint16_t>(x & 0xFFFFu); }
uint8_t lo8(uint32_t x) { return static_cast<uint8_t >(x & 0xFFu); }
uint8_t hi8(uint32_t x) { return static_cast<uint8_t >((x >> 8) & 0xFFu); }
int flag01(bool b) { return b ? 1 : 0; }

void printBytes(std::ostream& os, const std::vector<uint8_t>& bytes, size_t perLine = 16) {
    if (bytes.empty()) { os << "(empty)\n"; return; }
    os << std::hex << std::setfill('0');
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (i % perLine == 0) os << "  ";
        os << std::setw(2) << static_cast<unsigned>(bytes[i]) << ' ';
        if ((i + 1) % perLine == 0) os << '\n';
    }
    if (bytes.size() % perLine != 0) os << '\n';
    os << std::dec << std::setfill(' ');
}

void dump_state(string path){
    ofstream out(path, std::ios::out | std::ios::app);
    static const char* GPR32[8] = {"EAX","ECX","EDX","EBX","ESP","EBP","ESI","EDI"};
    static const char* GPR16[8] = {"AX","CX","DX","BX","SP","BP","SI","DI"};
    static const char* SEGRN[6] = {"ES","CS","SS","DS","FS","GS"};
    static const char* FLGN[7]  = {"CF","PF","AF","ZF","SF","DF","OF"};

    auto printRegLine = [&](int idx) {
        uint32_t v = u32(curr_state.GPR[idx]);
        uint16_t r16 = lo16(v);
        uint8_t  r8l = lo8(v);

        out << std::left
            << std::setw(3) << GPR32[idx] << " = 0x" << std::right << std::hex << std::setw(8) << std::setfill('0') << v
            << "   "
            << std::left << std::setw(2) << GPR16[idx] << " = 0x" << std::right << std::setw(4) << r16
            << "   "
            << std::left << std::setw(2) << (std::string(1, GPR16[idx][0]) + "L") << " = 0x" << std::right << std::setw(2) << static_cast<unsigned>(r8l);
        if (idx <= 3) {
            uint8_t r8h = hi8(v);
            out << "   "
                << std::left << std::setw(2) << (std::string(1, GPR16[idx][0]) + "H") << " = 0x"
                << std::right << std::setw(2) << static_cast<unsigned>(r8h);
        } else {
            out << "   " << "  " << "    " << "   " << "  " << "    "; // spacing to keep columns aligned
        }

        out << std::dec << std::setfill(' ') << '\n';
    };

    out << "\n\n";
    out << "====================== x86 MACHINE STATE DUMP ======================\n\n";
    out << "EIP = 0x" << std::hex << std::setw(8) << std::setfill('0')
        << u32(curr_state.EIP) << std::dec << std::setfill(' ') << "\n\n";
    out << "------------------------------ GPRs --------------------------------\n";
    out << "REG      32-bit              16-bit              8-bit low   8-bit high\n";
    out << "---------------------------------------------------------------------\n";
    for (int i = 0; i < 8; ++i) printRegLine(i);
    out << "\n";
    out << "--------------------------- SEGMENTS -------------------------------\n";
    out << std::hex << std::setfill('0');
    for (int i = 0; i < 6; ++i) {
        out << std::left << std::setw(2) << SEGRN[i]
            << " = 0x" << std::right << std::setw(4) << (static_cast<uint16_t>(curr_state.SEGR[i]) & 0xFFFFu)
            << ((i % 3 == 2) ? "\n" : "   ");
    }
    if (6 % 3 != 0) out << "\n";
    out << std::dec << std::setfill(' ') << "\n";
    out << "------------------------------ MMX ---------------------------------\n";
    out << std::hex << std::setfill('0');
    for (int i = 0; i < 8; ++i) {
        uint64_t v = static_cast<uint64_t>(curr_state.MMX[i]);
        out << "MMX" << i << " = 0x" << std::setw(16) << v << "\n";
    }
    out << std::dec << std::setfill(' ') << "\n";
    out << "----------------------------- FLAGS -------------------------------\n";
    for (int i = 0; i < 7; ++i) {
        out << FLGN[i] << '=' << flag01(curr_state.FLAGS[i]) << ((i == 6) ? '\n' : ' ');
    }
    out << "\n";
}

void mem_dump(string path) {
    ofstream out(path, std::ios::out | std::ios::app);
    out << std::hex << std::setfill('0');
    out << "====================== x86 MACHINE MEMORY DUMP ======================\n\n";
    out << "CYCLE COUNT: " << cycles << "\n\n";
    for (auto it = mem.begin(); it != mem.end(); ++it) {
        out << "0x"
            << std::setw(8) << it->first
            << ": 0x"
            << std::setw(2) << static_cast<unsigned>(it->second)
            << '\n';
    }
    out << std::dec << std::setfill(' ');
}

void clear_dump_file(string path) {
    std::ofstream out(path, std::ios::out | std::ios::trunc);
}

void cycle(string filename){
    if(cycles == 0){init_state(); init_mem(filename);}
    cout << "Machine Initialized" << endl;
    while(run){
        fetch_and_execute();
        cycles++;
        curr_state = next_state;
        dump_state("run.dump");
        mem_dump("mem.dump");
    }
}

int main(int argc, char* argv[]){
    if(argc < 2) cout << "Error: List a source assembly file" << endl;
    string filename = argv[1];
    clear_dump_file("run.dump");
    clear_dump_file("mem.dump");
    cycle(filename);
}


//TODO:
/*
1.sign-extended modes for imm8 Adds: Done
2.Sgement registers for fetching and memory operations : Done
3.dump to file all diagnostics 
*/


