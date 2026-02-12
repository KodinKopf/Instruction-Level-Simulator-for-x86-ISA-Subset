# Instruction-Level Simulator for x86 Subset
## Created by Peter DiSanto (pkd442)

### Overview:
The Instruction-Level Simulator for x86 Subset (ISLx86) is a C++ program that takes the 
inputs of an assembled text-file of x86 instructions or data, and fetches/executes each instruction sequentially.
After each instructions execution, the state of the machine is dumped to a temporary file **run.dump** and the memory contents are
dumped to a temporary file **mem.dump**. The list of compatible prefixes and instructions are below.

**Instructions:**
```
ADD AL, imm8
ADD AX, imm16
ADD EAX, imm32
ADD r/m8, imm8
ADD r/m16, imm16
ADD r/m32, imm32
ADD r/m16, imm8
ADD r/m32, imm8
ADD r/m8, r8
ADD r/m16, r16
ADD r/m32, r32
ADD r8, r/m8
ADD r16, r/m16
ADD r32, r/m32
MOVQ mm, mm/m64
MOV Sreg, r/m16
XCHG r/m8, r8
CMPXCHG r/m16, r16
JMP ptr16:32
JNE rel32
HLT 
```

**Prefixes** 
```
Operand Size Override (0x66)
```

### How to use:
First: Create a fresh, new directory!!!! Then cd into that directory
```
mkdir new_directory
cd new_directory
```

**Also: make sure you have C++ and g++ compiler downloaded and usable on your machine** 

List of files in new_directory needed for use:
- **main.cpp** : master program, what actually does simulation
- **mem.txt** : arbitrary **ASSEMBLED** x86 program and data memory which the program will execute. Please note: this program needs to start at address 0x00000000 and that the mem.txt file must be **formatted a certain way** (shown below).

**Once all of these modules are in your desired directory** the following commands can be run to execute ISLx86
```
g++ -std=c++17 -Wall -Wextra -o main main.cpp
./main mem.txt
```

After the second command is run, two temporary files **run.dump** and **mem.dump** will be in new_directory.
These files will give you a cycle-by-cycle break down of the State of the x86 Machine (EIP, GPRs, MMXs, SEGRs, FLAGS, etc...) and the contents
of the entire memory system in the form of **0xADDRESS: BYTE**. These will be very useful for debugging and tracing the machine as it runs.

### What to expect:
Once the above command is run with a correct directory setup, if you are using a linux-based machine with a viewable terminal, there should be two outputs: 
1. **Machine Initialized** to indicate the current_state was set to all 0's and memory was loaded from input file mem.txt
2. **x86 Program Executed from file mem.txt** to indicate that the program was executed to completion and machine halted.

### How to Format Mem.Txt
First you will need a x86 Assembly Program to assembler with an online assembler (I recommend **Defuse.ca**)

Assembler Input Example
```
add     al, 0x12
add     eax, 0x12345678
mov     ds, word ptr [0x00000420]
add     byte ptr [0x00000400], 0x7F
add     byte ptr [0x00000400], 0x01
add     dword ptr [0x00000404], 0x12345678
add     dword ptr [0x00000404], -0x10
xchg    byte ptr [0x00000408], al
add     al, byte ptr [0x00000400]
add     ecx, dword ptr [0x00000404]
movq    mm0, qword ptr [0x00000410]
cmpxchg word ptr [0x0000040C], cx
jne     06
add     al, 0x01
hlt
```

Assembler Output Example
```
0:  04 12                   add    al,0x12
2:  05 78 56 34 12          add    eax,0x12345678
7:  8e 1d 20 04 00 00       mov    ds,WORD PTR ds:0x420
d:  80 05 00 04 00 00 7f    add    BYTE PTR ds:0x400,0x7f
14: 80 05 00 04 00 00 01    add    BYTE PTR ds:0x400,0x1
1b: 81 05 04 04 00 00 78    add    DWORD PTR ds:0x404,0x12345678
22: 56 34 12
25: 83 05 04 04 00 00 f0    add    DWORD PTR ds:0x404,0xfffffff0
2c: 86 05 08 04 00 00       xchg   BYTE PTR ds:0x408,al
32: 02 05 00 04 00 00       add    al,BYTE PTR ds:0x400
38: 03 0d 04 04 00 00       add    ecx,DWORD PTR ds:0x404
3e: 0f 6f 05 10 04 00 00    movq   mm0,QWORD PTR ds:0x410
45: 66 0f b1 0d 0c 04 00    cmpxchg WORD PTR ds:0x40c,cx
4c: 00
4d: 0f 85 02 00 00 00       jne    55 <_main+0x55>
53: 04 01                   add    al,0x1
55: f4                      hlt
```

**MODIFICATIONS NEEDS:**
1. Add "0x" before every address line
2. Add comment line "//" before every comment

Finalized Mem.txt File:
```
0x0:  04 12                   // add    al,0x12
0x2:  05 78 56 34 12          // add    eax,0x12345678
0x7:  8e 1d 20 04 00 00       // mov    ds,WORD PTR ds:0x420
0xd:  80 05 00 04 00 00 7f    // add    BYTE PTR ds:0x400,0x7f
0x14: 80 05 00 04 00 00 01    // add    BYTE PTR ds:0x400,0x1
0x1b: 81 05 04 04 00 00 78    // add    DWORD PTR ds:0x404,0x12345678
0x22: 56 34 12                // (continuation of previous immediate)
0x25: 83 05 04 04 00 00 f0    // add    DWORD PTR ds:0x404,0xfffffff0
0x2c: 86 05 08 04 00 00       // xchg   BYTE PTR ds:0x408,al
0x32: 02 05 00 04 00 00       // add    al,BYTE PTR ds:0x400
0x38: 03 0d 04 04 00 00       // add    ecx,DWORD PTR ds:0x404
0x3e: 0f 6f 05 10 04 00 00    // movq   mm0,QWORD PTR ds:0x410
0x45: 66 0f b1 0d 0c 04 00    // cmpxchg WORD PTR ds:0x40c,cx
0x4c: 00                      // (padding / continuation byte)
0x4d: 0f 85 02 00 00 00    // jne    52 <_main+0x52>
0x53: 04 01                   // add    al,0x1
0x55: f4                      // hlt
```

### Troubleshooting and Reminders:
If there is a compilation issue, make sure all files are within the same directory.
Make sure you also have C++ and g++ downloaded on your machine

If both of these requirements are met and the simulator still will not run, then check the mem.txt file to make sure it is the correct format

If an unknown opcode exception occurs, make sure your control instructions are branching you to the expected memory location

If you encounter any run-time/generation errors, please contact the SVGS service team @ **+1 617-448-4817**
