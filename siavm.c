#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

/*
    --------------------------------------------------------------------------------------------------------------------------------------------------------------
    gcc -o siavm siavm.c
    ./siavm testFile.bin
    --------------------------------------------------------------------------------------------------------------------------------------------------------------
    load(char * fileName) - Deals with reading all instructions into memory
    fetch() - Fetches the memory bytes into a buffer which we use for every instruction (also increments PC)
    decode() - Sets the OP1 and OP2 Registers where applicable
    execute() - Calculates results, and only executes if there is no memory, stack or offset calculation necessary
    store() - Deals with almost all calculations regarding memory, setting values to registers, stack operations of any kind, and offsets
    main(int argc, char ** argv) - If we don't have 2 arguments, program stops (VM Exits) otherwise, run an infinite loop with a flag if a HALT instruction is hit
    --------------------------------------------------------------------------------------------------------------------------------------------------------------
*/

unsigned char mem[1000] = {0};  // Memory, 1000 Bytes, initialized to 0 so I don't get weird stuff when I access
int Register[16] = {0}; // Registers, initialized to 0 so I don't get weird stuff when I access
int PC = 0; // Program Counter
int totalByteCount = 0; // Total number of occupied bytes

// The state locks
bool canFetch1 = true;
bool canFetch2 = false;
bool canDecode1 = false;
bool canDecode2 = false;
bool canExecute1 = false;
bool canExecute2 = false;
bool canStore1 = false;
bool canStore2 = false;

int status = -1; // Current status register
int ReserveRegisterNumber; // used for Register Forwarding
int finishedBuffers = 0; // If buffers are finished, this turns to 1

// Operands 1, 2, and Result Variable
int OP1;
int OP2;
int OP3;
int OP4;
int result1;
int result2;
int fetchCheck = 0;
int decodeCheck = 0;
unsigned char buffer1[4]; // My buffer1 will hold up to 4 bytes of my current opCode
unsigned char buffer2[4]; // My buffer2 will hold up to 4 bytes of my current opCode
char currInstruction1 = -1; //OPCODE INSTRUCTION 1
char currInstruction2 = -1; //OPCODE INSTRUCTION 2
int flag = 1; // My flag for my infinite while loop in main
size_t memSize = sizeof(mem)/sizeof(mem[0]); // The size of my memory, so I don't have to change my interrupt 1 opCode below if I allocate more than 1K bytes to memory

// Zero Out the buffer, OP1, OP2 and the result variables so they're ready for the next instruction
void clearFields1(){
    buffer1[0] = 0;
    buffer1[1] = 0;
    buffer1[2] = 0;
    buffer1[3] = 0;
    OP1 = 0;
    OP2 = 0;
    currInstruction1 = -1;
    result1 = 0;
}

// Zero Out the buffer, OP3, OP4 and the result variables so they're ready for the next instruction
void clearFields2(){
        
    buffer2[0] = 0;
    buffer2[1] = 0;
    buffer2[2] = 0;
    buffer2[3] = 0;
    OP3 = 0;
    OP4 = 0;
    currInstruction2 = -1;
    result2 = 0;
}

/*
    Load simply reads the bin file we pass, and writes instructions into memory
*/
void load (char * fileName){

    // rb is like read, but for binary
    FILE * file = fopen(fileName, "rb");

    int character;
    if (file == NULL){
        printf("Cannot read file! File Doesn't exist! \n");
    } else {
        int i;
        for (i = 0; (character = fgetc(file)) != EOF; i++){
            mem[i] = (unsigned char) character;
        }
        totalByteCount = i + 1;
    }
}

/*
    Fetch looks at the current opCode OPCODE, and if it's a branch, call or jump opCode (opCode 7), it sends the next 4 bytes to the buffer while simoultaneously increasing the Program Counter
    For all other opCodes, it sends only the next 2 bytes
*/
void fetch (){
    if (finishedBuffers == 1){
        finishedBuffers = 0;
    }
    int i = 0;
    currInstruction1 = (mem[PC + i] >> 4);
    if (canFetch1 == true && currInstruction1 != -1){
        if (currInstruction1 == 7){
            buffer1[0] = mem[PC + i++];
            buffer1[1] = mem[PC + i++];
            buffer1[2] = mem[PC + i++];
            buffer1[3] = mem[PC + i++];
        } else {
            buffer1[0] = mem[PC + i++];
            buffer1[1] = mem[PC + i++];
        }
        if (currInstruction1 == 7 && (0x0F & buffer1[0]) > 5){
            status = 5;
        } else {
            canFetch2 = true;
        }
    }

    currInstruction2 = (mem[PC + i] >> 4);

    if (canFetch2 == true && currInstruction2 != -1){
        if (currInstruction2 == 7){
            buffer2[0] = mem[PC + i++];
            buffer2[1] = mem[PC + i++];
            buffer2[2] = mem[PC + i++];
            buffer2[3] = mem[PC + i++];
        } else {
            buffer2[0] = mem[PC + i++];
            buffer2[1] = mem[PC + i++];
        }
        fetchCheck = -1;
    }

    // Walmart tier State Machine to determine next locks
    if (fetchCheck == -1){
        canFetch1 = false;
        canFetch2 = false;
        canDecode1 = true;
        canDecode2 = true;
        fetchCheck = 0;
    }

    if (status == 5){
        canDecode1 = true;
    } else if (status == 6){
        canDecode2 = true;
    }
}


/*
    3R Instructions - Sets OP1 to the first Register, OP2 to the second Register
    BR1 Instructions - Sets OP1 to the first Register, OP2 to the second Register
    BR2 Instructions - Nothing (Not necessary here)
    Load and Store Instructions - Sets OP1 to Register to load/store, OP2 to the Address Register
    Stack Instructions - Return| Sets OP1 to 15 (used later for register 15) ---- Pop and Push| Sets OP1 to the register we are popping/pushing
    Move Instruction - Move Sets OP1 to the Register number we are moving a value to, OP2 to the actual value we are moving
    Interrupt Instruction - Not Necessary
*/
void decode (){
    currInstruction1 = (buffer1[0] >> 4); // Gets the opcode
    int temp; // Used for anything we might need a temporary variable for

    if (canDecode1 == true && currInstruction1 != -1){
        if(currInstruction1 > 0 && currInstruction1 < 7){  // 3R Instructions                    
            OP1 = 0x0F & buffer1[0];
            OP2 = (buffer1[1]) >> 4;
            ReserveRegisterNumber = 0x0F & buffer1[1];
        } else if(currInstruction1 == 7) {                // BRANCH
            if ((0x0F & buffer1[0]) <= 5){       // Branch Type 5 or less
                OP1 = (buffer1[1] >> 4);
                OP2 = 0x0F & (buffer1[1]);
            }
        } else if(currInstruction1 == 8){                 // LOAD
            OP1 = 0x0F & (buffer1[0]);
            OP2 = (buffer1[1] >> 4);
        } else if(currInstruction1 == 9){                 // STORE
            OP1 = 0x0F & (buffer1[0]);
            OP2 = (buffer1[1] >> 4);
        } else if(currInstruction1 == 10){                // STACK INSTRUCTION
        
            // Checks top 2 bits of 2nd byte to see if its a pop, return or push instruction
            temp = (buffer1[1] >> 6);
            if (temp == 0){                     // Return
                OP1 = 15;
            } else if (temp == 1){              // Push
                OP1 = 0x0F & (buffer1[0]); 
            } else if (temp == 2){              // Pop
                OP1 = 0x0F & (buffer1[0]);
            }

        } else if(currInstruction1 == 11){                // MOVE
            OP1 = 0x0F & (buffer1[0]);
            OP2 = buffer1[1];
        } else {
            OP1 = OP2 = 0;
        }
        canDecode1 = false;
    }


    currInstruction2 = (buffer2[0] >> 4); // Gets the opcode

    if (canDecode2 == true && currInstruction2 != -1){
        if(currInstruction2 > 0 && currInstruction2 < 7){  // 3R Instructions                    
            OP3 = 0x0F & buffer2[0];
            OP4 = (buffer2[1]) >> 4;
        } else if(currInstruction2 == 7) {                // BRANCH
            if ((0x0F & buffer2[0]) <= 5){       // Branch Type 5 or less
                OP3 = (buffer2[1] >> 4);
                OP4 = 0x0F & (buffer2[1]);
            }
        } else if(currInstruction2 == 8){                 // LOAD
            OP3 = 0x0F & (buffer2[0]);
            OP4 = (buffer2[1] >> 4);
        } else if(currInstruction2 == 9){                 // STORE
            OP3 = 0x0F & (buffer2[0]);
            OP4 = (buffer2[1] >> 4);
        } else if(currInstruction2 == 10){                // STACK INSTRUCTION
        
            // Checks top 2 bits of 2nd byte to see if its a pop, return or push instruction
            temp = (buffer2[1] >> 6);
            if (temp == 0){                     // Return
                OP3 = 15;
            } else if (temp == 1){              // Push
                OP3 = 0x0F & (buffer2[0]); 
            } else if (temp == 2){              // Pop
                OP3 = 0x0F & (buffer2[0]);
            }

        } else if(currInstruction2 == 11){                // MOVE
            OP3 = 0x0F & (buffer2[0]);
            OP4 = buffer2[1];
        } else {
            OP3 = OP4 = 0;
        }
        status = 1;
        canDecode2 = false;
    }


    // Walmart tier State Machine to determine next locks
    if (status == 1){
        canExecute1 = true;
    } else if (status == 2){
        canExecute2 = 2;
    } else if (status == 5) {
        canDecode1 = false;
        canExecute1 = true;
    } else if (status == 6){
        canDecode2 = false;
        canExecute2 = true;
    }

    

}


/*
    3R Instructions - Performs operation on the Operants OP1 and OP2, and stores them in the result, Halt sets the flag to 0, which means the program won't run to the next instruction
    BR1 Instructions - Checks conditions and sets Result as 1 for true and 0 for false
    BR2 Instruction - Not Necessary
    Load and Store Instructions - Not Necessary
    Stack Instructions - Not Necessary
    Move Instruction - Not Necessary
    Interrupt Instruction - Either prints registers, memory, or an error message for an unsupported interrupt code
*/
void execute (){
    currInstruction1 = (buffer1[0] >> 4); // Gets the opcode
    int temp; // Used for anything we might need a temporary variable for
    
    if (canExecute1 == true && currInstruction1 != -1){
        if(currInstruction1 == 0){            // HALT
            flag = 0;
        } else if(currInstruction1 == 1){     // ADD
            result1 = Register[OP1] + Register[OP2];     
        } else if(currInstruction1 == 2){     // AND
            result1 = Register[OP1] & Register[OP2];
        } else if(currInstruction1 == 3){     // Divide
            result1 = Register[OP1] / Register[OP2];
        } else if(currInstruction1 == 4){     // Multiply
            result1 = Register[OP1] * Register[OP2];
        } else if(currInstruction1 == 5){     // Subtract
            result1 = Register[OP1] - Register[OP2];
        } else if(currInstruction1 == 6){     // Or
            result1 = Register[OP1] | Register[OP2];
        } else if(currInstruction1 == 7){     // Branch

            // Checks if conditions for any branch types if they are true, if they are, they set result as 1, and if false, they set result as 0
            if (0x0F & buffer1[0] <= 5){
                if (0x0F & buffer1[0] == 0){                         // Branch if Less than
                    if(Register[OP1] < Register[OP2]){
                        result1 = 1;
                    } else {
                        result1 = 0;
                    }
                } else if (0x0F & buffer1[0] == 1){                  // Branch if Less than or Equal to
                    if(Register[OP1] <= Register[OP2]){
                        result1 = 1;
                    } else {
                        result1 = 0;
                    }
                } else if (0x0F & buffer1[0] == 2){                  // Branch if Equal
                    if(Register[OP1] == Register[OP2]){
                        result1 = 1;
                    } else {
                        result1 = 0;
                    }
                } else if (0x0F & buffer1[0] == 3){                  // Branch if Not Equal
                    if(Register[OP1] != Register[OP2]){
                        result1 = 1;
                    } else {
                        result1 = 0;
                    }
                } else if (0x0F & buffer1[0] == 4){                  // Branch if Greater than
                    if(Register[OP1] > Register[OP2]){
                        result1 = 1;
                    } else {
                        result1 = 0;
                    }
                } else if (0x0F & buffer1[0] == 5){                  // Branch if Greater than or Equal
                    if(Register[OP1] >= Register[OP2]){
                        result1 = 1;
                    } else {
                        result1 = 0;
                    }
                }

                if (result1 == 0){
                    status = 3;
                } else {
                    status = 1;
                }
            }  
            
        }
        
    }

     currInstruction2 = (buffer2[0] >> 4); // Gets the opcode

    if (canExecute2 == true && currInstruction1 != -1){
        if(currInstruction2 == 0){            // HALT
            flag = 0;
        } else if(currInstruction2 == 1){     // ADD
            result2 = Register[OP3] + Register[OP4];     
        } else if(currInstruction2 == 2){     // AND
            result2 = Register[OP3] & Register[OP4];
        } else if(currInstruction2 == 3){     // Divide
            result2 = Register[OP3] / Register[OP4];
        } else if(currInstruction2 == 4){     // Multiply
            result2 = Register[OP3] * Register[OP4];
        } else if(currInstruction2 == 5){     // Subtract
            result2 = Register[OP3] - Register[OP4];
        } else if(currInstruction2 == 6){     // Or
            result2 = Register[OP3] | Register[OP4];
        } else if(currInstruction2 == 7){     // Branch

            // Checks if conditions for any branch types if they are true, if they are, they set result as 1, and if false, they set result as 0
            if (0x0F & buffer2[0] <= 5){

                if (0x0F & buffer2[0] == 0){                         // Branch if Less than
                    if(Register[OP3] < Register[OP4]){
                        result2 = 1;
                    } else {
                        result2 = 0;
                    }
                } else if (0x0F & buffer2[0] == 1){                  // Branch if Less than or Equal to
                    if(Register[OP3] <= Register[OP4]){
                        result2 = 1;
                    } else {
                        result2 = 0;
                    }
                } else if (0x0F & buffer2[0] == 2){                  // Branch if Equal
                    if(Register[OP3] == Register[OP4]){
                        result2 = 1;
                    } else {
                        result2 = 0;
                    }
                } else if (0x0F & buffer2[0] == 3){                  // Branch if Not Equal
                    if(Register[OP3] != Register[OP4]){
                        result2 = 1;
                    } else {
                        result2 = 0;
                    }
                } else if (0x0F & buffer2[0] == 4){                  // Branch if Greater than
                    if(Register[OP3] > Register[OP4]){
                        result2 = 1;
                    } else {
                        result2 = 0;
                    }
                } else if (0x0F & buffer2[0] == 5){                  // Branch if Greater than or Equal
                    if(Register[OP3] >= Register[OP4]){
                        result2 = 1;
                    } else {
                        result2 = 0;
                    }
                }
                if (result2 == 0){
                    status = 4;
                } else {
                    status = 1;
                }
                
            }  
            
        }
        
    }


     // Walmart tier State Machine to determine next locks
    if (status == 1){
        canExecute1 = false;
        canStore1 = true;
    } else if (status == 2){
        canExecute2 = false;
        canStore2 = true;
    } else if (status == 3){
        canExecute1 = false;
        canStore1 = false;
        canExecute2 = true;
        canStore2 = true;
    } else if (status == 4){
        canExecute1 = false;
        canExecute2 = false;
        canStore2 = false;
        canStore1 = true;
    } else if (status == 5) {
        canExecute1 = false;
        canStore1 = true;
    } else if (status == 6){
        canExecute2 = false;
        canStore2 = true;
    }

    
}



/*
    3R Instructions - Halt simply ends the function (ends program also). The Rest simply assign the result value to the Result Register.
    BR1 and BR2 Instructions - Perform all necessary offset, memory and stack calculations to function properly
    Load and Store Instructions - Perform all necessary offset calculations and either Store to memory or Load from Memory
    Stack Instructions - Push, Pop or Return(pops the top value) values into the stack with the appropriate calculations
    Move Instruction - Sets Register at OP1 address to the result
    Interrupt Instruction - Not Necessary
*/
void store (){
    currInstruction1 = (buffer1[0] >> 4); // Gets the opcode
    int temp; // Used for anything we might need a temporary variable for
    int offset; // Used for any offset calculations we might need
    currInstruction2 = (buffer2[0] >> 4);

    if (canStore1 == true && currInstruction1 != -1){
        if (currInstruction1 == 0){                                                           // HALT
            PC += 2;
            status = -1;
        } else if (currInstruction1 == 1){                                                    // Add
            PC += 2;
            temp = 0x0F & (buffer1[1]);
            Register[temp] = result1;
        } else if (currInstruction1 == 2){                                                    // And
            PC += 2;
            temp = 0x0F & (buffer1[1]);
            Register[temp] = result1;
        } else if (currInstruction1 == 3){                                                    // Divide
            PC += 2;
            temp = 0x0F & (buffer1[1]);
            Register[temp] = result1;
        } else if (currInstruction1 == 4){                                                    // Multiply
            PC += 2;
            temp = 0x0F & (buffer1[1]);
            Register[temp] = result1;
        } else if (currInstruction1 == 5){                                                    // Subtract
            PC += 2;
            temp = 0x0F & (buffer1[1]);
            Register[temp] = result1;
        } else if (currInstruction1 == 6){                                                    // OR
            PC += 2;
            temp = 0x0F & (buffer1[1]);
            Register[temp] = result1;
        } else if (currInstruction1 == 7){                                                    // Branches
            PC += 4;
            if ((0x0F & buffer1[0]) <= 5){                                           // Branch Types 0 to 5
                offset = (buffer1[2] << 8 | buffer1[3]); // Setting the offset

                if (result1 == 1){ // If the result from execute is 1, the operation is true, so we set the PC to PC + 2 times the offset (minus 4 because I had incremented PC in fetch by + 4, I had to make up the difference here instead now)
                    PC = PC + (2 * offset) - 4;
                }

            } else {                                                                // CALL AND JUMP
                offset = (buffer1[1] << 16 | buffer1[2] << 8 | buffer1[3]); // The offset for Call and Jump 

                if ((0x0F & buffer1[0]) == 6){                                                // Call
                    // We call a function (hence the call operation) by shifting the program counter right, and pop 4 bytes in the stack (the other side of memory, where Register 15 is used as an index in the stack)
                    for (int i = 0; i < 4; i++){
                        mem[Register[15]--] = (PC >> (8 * i));
                    }
                    PC = 2 * offset; // Just move the PC to 2 time the offset
                    
                } else {                                                                      // Jump   
                    PC = 2 * offset; // Just move the PC to 2 time the offset
                }
            }
        } else if (currInstruction1 == 8){                                                    // LOAD
            offset = 0x0F & buffer1[1]; // We set the offset
            PC += 2;
            // Load has a limit of 0 to 30 for its offset, if its more than 30 or less than 0, pop an error message and return;
            if (offset > 30 || offset < 0){
                printf("Invalid offset, please enter offset 0 to 30 for Load\n");
                return;
            }
            
            // For the store instruction we are doing something similar to push for stack operations while also taking into account an offset
            int temp = (Register[OP2] + offset) * 2;

            // Load kind of works like a pop from memory, we literally load the value from memory, and then zero out that index in memory (temp is derived from the offset, look above)
            for (int i = 0; i < 4; i++){
                Register[OP1] = (Register[OP1] << (8 * i)) | mem[++temp];
                mem[temp] = 0;
            } 

        } else if (currInstruction1 == 9){                                                    // STORE
            offset = 0x0F & buffer1[1]; // We set the offset
            PC += 2;
            // Store has a limit of 0 to 30 for its offset, if its more than 30 or less than 0, pop an error message and return;
            if (offset > 30 || offset < 0){
                printf("Invalid offset, please enter offset 0 to 30 for Store\n");
                return;
            }

            // For the store instruction we are doing something similar to push for stack operations while also taking into account an offset
            int temp = 4 + ((Register[OP2] + offset) * 2);

            // work our way through the stack from the position of temp (inside memory), and set it to the shift right operation times i, of the first Register
            for (int i = 0; i < 4; i++){
                mem[temp--] = (Register[OP1] >> (8 * i));
            }  

        } else if (currInstruction1 == 10){                                                   // STACK
            temp = (buffer1[1] >> 6); // Getting the top 2 bits of the 2nd byte
            PC += 2;
            if (temp == 0){                                                         // Return
                
                // Popping the top value from the stack and jumping to that PC address
                for (int i = 0; i < 4; i++){
                    PC = (PC << (8 * i)) | mem[++Register[15]];
                    mem[Register[15]] = 0;
                } 
                
            } else if (temp == 1){                                                  // Push
                
                // Popping the specified register value from the stack
                for (int i = 0; i < 4; i++){
                    mem[Register[15]--] = (Register[OP1] >> (8 * i));
                }
                
            } else if (temp == 2){                                                  // Pop

                // Popping the specified register value from the stack
                for (int i = 0; i < 4; i++){
                    Register[OP1] = (Register[OP1] << (8 * i)) | mem[++Register[15]];
                    mem[Register[15]] = 0;
                } 
                
            }
        } else if (currInstruction1 == 11){                                                   // Move
            PC += 2;
            Register[OP1] = OP2;
        } else if(currInstruction1 == 12){        // Interrupt
            PC += 2;
            if (buffer1[1] == 0) {       // If interrupt code is 0, it prints registers
                for (temp = 0; temp < 16; temp++){
                    printf("Register %d: %d\n", temp, Register[temp]);
                }
            } else if (buffer1[1] == 1){ // If interrupt code is 1, it prints all of memory USED (Does not print unused memory)
                for (temp = 0; temp < totalByteCount; temp++){
                    printf("Byte %d:   %02x\n", temp, mem[temp]);
                }
            } else {                    // If interrupt code is anything else, it prints out an error message saying the interrupt code is invalid (they are not supported in this version of SIA)
                printf("Invalid Interrupt Code: %d\n", buffer1[1]);
            }
        }
    }




    if (canStore2 == true && currInstruction2 != -1){

        if (currInstruction2 == 0){                                                           // HALT
            PC += 2;
            status = -1;
        } else if (currInstruction2 == 1){                                                    // Add
            PC += 2;
            temp = 0x0F & (buffer2[1]);
            Register[temp] = result2;
        } else if (currInstruction2 == 2){                                                    // And
            PC += 2;
            temp = 0x0F & (buffer2[1]);
            Register[temp] = result2;
        } else if (currInstruction2 == 3){                                                    // Divide
            PC += 2;
            temp = 0x0F & (buffer2[1]);
            Register[temp] = result2;
        } else if (currInstruction2 == 4){                                                    // Multiply
            PC += 2;
            temp = 0x0F & (buffer2[1]);
            Register[temp] = result2;
        } else if (currInstruction2 == 5){                                                    // Subtract
            PC += 2;
            temp = 0x0F & (buffer2[1]);
            Register[temp] = result2;
        } else if (currInstruction2 == 6){                                                    // OR
            PC += 2;
            temp = 0x0F & (buffer2[1]);
            Register[temp] = result2;
        } else if (currInstruction2 == 7){                                                    // Branches
            PC += 4;
            if ((0x0F & buffer2[0]) <= 5){                                           // Branch Types 0 to 5
                offset = (buffer2[2] << 8 | buffer2[3]); // Setting the offset

                if (result2 == 1){ // If the result from execute is 1, the operation is true, so we set the PC to PC + 2 times the offset (minus 4 because I had incremented PC in fetch by + 4, I had to make up the difference here instead now)
                    PC = PC + (2 * offset) - 4;
                }

            } else {                                                                // CALL AND JUMP
                offset = (buffer2[1] << 16 | buffer2[2] << 8 | buffer2[3]); // The offset for Call and Jump 

                if ((0x0F & buffer2[0]) == 6){                                                // Call
                    // We call a function (hence the call operation) by shifting the program counter right, and pop 4 bytes in the stack (the other side of memory, where Register 15 is used as an index in the stack)
                    for (int i = 0; i < 4; i++){
                        mem[Register[15]--] = (PC >> (8 * i));
                    }
                    PC = 2 * offset; // Just move the PC to 2 time the offset
                    
                } else {                                                                      // Jump   
                    PC = 2 * offset; // Just move the PC to 2 time the offset
                }
            }
        } else if (currInstruction2 == 8){                                                    // LOAD
            offset = 0x0F & buffer2[1]; // We set the offset
            PC += 2;
            // Load has a limit of 0 to 30 for its offset, if its more than 30 or less than 0, pop an error message and return;
            if (offset > 30 || offset < 0){
                printf("Invalid offset, please enter offset 0 to 30 for Load\n");
                return;
            }
            
            // For the store instruction we are doing something similar to push for stack operations while also taking into account an offset
            int temp = (Register[OP4] + offset) * 2;

            // Load kind of works like a pop from memory, we literally load the value from memory, and then zero out that index in memory (temp is derived from the offset, look above)
            for (int i = 0; i < 4; i++){
                Register[OP3] = (Register[OP3] << (8 * i)) | mem[++temp];
                mem[temp] = 0;
            } 

        } else if (currInstruction2 == 9){                                                    // STORE
            offset = 0x0F & buffer2[1]; // We set the offset
            PC += 2;
            // Store has a limit of 0 to 30 for its offset, if its more than 30 or less than 0, pop an error message and return;
            if (offset > 30 || offset < 0){
                printf("Invalid offset, please enter offset 0 to 30 for Store\n");
                return;
            }

            // For the store instruction we are doing something similar to push for stack operations while also taking into account an offset
            int temp = 4 + ((Register[OP4] + offset) * 2);

            // work our way through the stack from the position of temp (inside memory), and set it to the shift right operation times i, of the first Register
            for (int i = 0; i < 4; i++){
                mem[temp--] = (Register[OP3] >> (8 * i));
            }  

        } else if (currInstruction2 == 10){                                                   // STACK
            temp = (buffer2[1] >> 6); // Getting the top 2 bits of the 2nd byte
            PC += 2;
            if (temp == 0){                                                         // Return
                
                // Popping the top value from the stack and jumping to that PC address
                for (int i = 0; i < 4; i++){
                    PC = (PC << (8 * i)) | mem[++Register[15]];
                    mem[Register[15]] = 0;
                } 
                
            } else if (temp == 1){                                                  // Push
                
                // Popping the specified register value from the stack
                for (int i = 0; i < 4; i++){
                    mem[Register[15]--] = (Register[OP3] >> (8 * i));
                }
                
            } else if (temp == 2){                                                  // Pop

                // Popping the specified register value from the stack
                for (int i = 0; i < 4; i++){
                    Register[OP3] = (Register[OP3] << (8 * i)) | mem[++Register[15]];
                    mem[Register[15]] = 0;
                } 
                
            }
        } else if (currInstruction2 == 11){                                                   // Move
            PC += 2;
            Register[OP3] = OP4;
        } else if(currInstruction2 == 12){        // Interrupt
            PC += 2;
            if (buffer2[1] == 0) {       // If interrupt code is 0, it prints registers
                for (temp = 0; temp < 16; temp++){
                    printf("Register %d: %d\n", temp, Register[temp]);
                }
            } else if (buffer2[1] == 1){ // If interrupt code is 1, it prints all of memory USED (Does not print unused memory)
                for (temp = 0; temp < totalByteCount; temp++){
                    printf("Byte %d:   %02x\n", temp, mem[temp]);
                }
            } else {                    // If interrupt code is anything else, it prints out an error message saying the interrupt code is invalid (they are not supported in this version of SIA)
                printf("Invalid Interrupt Code: %d\n", buffer2[1]);
            }
        }
        finishedBuffers = 1;
    }


     // Walmart tier State Machine to determine next locks
    if (status == 1){
        canStore1 = false;
        canExecute2 = true;
        status = 2;
        clearFields1();
    } else if (status == 2){
        canStore2 = false;
        clearFields2();
    } else if (status == 3){
        canExecute2 = false;
        canStore2 = false;
    } else if (status == 4){
        canStore1 = false;
    } else if (status == 5) {
        canStore1 = false;
        canFetch2 = true;
        status = 6;
    } else if (status == 6){
        canStore2 = false;
    }

     // If the buffers are completed, we reset everything back to normal
    if (canStore2 == false && canStore2 == false && finishedBuffers == 1){
        canFetch1 = true;
        canFetch2 = false;
        canDecode1 = false;
        canDecode2 = false;
        finishedBuffers = 0;
        currInstruction1 = -1;
        currInstruction2 = -1;
        ReserveRegisterNumber = -1;
        status = -1;
        clearFields1();
        clearFields2();
    }

}


/*
    Main takes command line arguments and we compile and run like this
    gcc -o siavm siavm.c
    ./siavm testFile.bin

    we are required to have an argv[1] which is a binary file for this assignment

    If the arguments are not 2, we print an error message
    We load the file into our program passing in argv[1] (the .bin file)
    We set the running flag to 1, this flag will turn 0 inside our decode function if it encounters the program, which will stop the program at that instruction
    In this "infinite" while loop we run fetch(), decode(), execute() and store() in order
    When the VM finished, we print the message "VM Exiting..." which signals that the VM has terminated
*/
int main (int argc, char ** argv){
    Register[15] = memSize - 1;   // We set the 16th Register to the memory size - 1, which means the index of the last byte in memory, which is where our stack starts
    if (argc != 2){
        printf("Not Enough Arguments!\n");
    } else {
        load(argv[1]);
        // Submitting it out of order because it all works perfectly
        while (flag == 1){
            fetch();
            decode();
            execute();
            store();
        }
        printf("VM Exiting...\n");
    }


}
