#include <EEPROM.h>
#include <IEEE754tools.h>
#include "instruction_set.h"

#define BUFSIZE 12
#define FILENAMESIZE 12
#define MAX_FILES 10
#define EEPROM_SIZE 1024

#define MAXPROCESSES 10
#define STACKSIZE 32
#define MEMORYSIZE 256

#define MAX_VARS 25

typedef struct {
    char name[FILENAMESIZE];
    int start;
    int size;
} fileType;

typedef struct {
    char name[BUFSIZE];
    void (*func)();
} commandType;

typedef struct {
    char name[BUFSIZE]; 
    int type;                   
    int address;                  
    int size;                   
    int processID;              
} MemoryTableEntry; 

typedef struct {
    char name[FILENAMESIZE];
    int processId;
    char state;  
    int pc;  
    int fp; 
    int sp;  
    int loop_start; 
    int stack[STACKSIZE];  
} Process;

fileType FAT[MAX_FILES];
EERef noOfFiles = EEPROM[160];

MemoryTableEntry memoryTable[MAX_VARS];
int noOfVars = 0;
byte memory[MEMORYSIZE];

byte stack[STACKSIZE];
int stackPointer = 0; //zet terug naar 0 

Process processes[MAXPROCESSES];
int noOfProcesses = 0;

void help();
void store();
void retrieve();
void erase();
void files();
void freespace();
void run();
void list();
void suspend();
void resume();
void kill();
void deleteAllFiles(); 
void show();

static commandType command[] = {
    {"help", &help},
    {"store", &store},
    {"retrieve", &retrieve},
    {"erase", &erase},
    {"files", &files},
    {"freespace", &freespace},
    {"run", &run},
    {"list", &list},
    {"suspend", &suspend},
    {"resume", &resume},
    {"kill", &kill},
    {"deleteall", &deleteAllFiles},
    {"show", &show},
};

static int nCommands = sizeof(command) / sizeof(commandType);

char inputBuffer[BUFSIZE];
int bufferIndex = 0;

void setup() {
    Serial.begin(9600); 
    ////Serial.println((noOfFiles);
    Serial.println(F("ArduinOS 1.0 ready"));
    // bufferIndex = 0;
    // noOfVars = 0;
    initProcessTable();
    initializeFAT();      
}

void initProcessTable() {
    for (int i = 0; i < MAXPROCESSES; i++) {
        processes[i].state = '0';  
    }
}

void pushByte(byte b) {
    if (stackPointer >= sizeof(stack)) {
        Serial.println(F("Error: Stack overflow"));
        return;
    }
    stack[stackPointer++] = b;
    //Serial.print("Pushed byte: ");
    //Serial.println((b);
}


byte popByte() {
    if (stackPointer <= 0) {
        Serial.println(F("Error: Stack underflow"));
        return 0;
    }
    byte b = stack[--stackPointer];
    // Serial.print("Popped byte: ");
    // Serial.print(b);
    // Serial.print(" / ");
    //Serial.println(((char)b);
    return b;
}

void pushChar(char c) {
    pushByte(c);
    pushByte(CHAR);
}

char popChar() {
    return popByte();
}

void pushInt(int i) {
    pushByte(highByte(i));
    pushByte(lowByte(i));
    pushByte(INT);
}

int popInt() {
    if (stackPointer < 2) {
        //Serial.println(("Error: Not enough data on stack");
        return 0; // Handle the error appropriately
    }
    byte lowByte = popByte();   
    byte highByte = popByte();   
    int value = word(lowByte, highByte);  

    return value;
}

void pushFloat(float f) {
    byte *b = (byte *)&f;
    //Serial.println(("Pushing float bytes: ");
    //Serial.print("sizeof the value: ");
    //Serial.println((sizeof(float));
    for (int i = sizeof(float) - 1; i >= 0; i--) {
        pushByte(b[i]);
        // Serial.print(b[i], HEX);
        // Serial.print(" ");
    }
    //Serial.println(();  // Newline for clarity
    pushByte(FLOAT);  // Push the type identifier last
}

float popFloat() {
    byte b[sizeof(float)];
    for (int i = sizeof(float) - 1; i >= 0; i--) {
        b[i] = popByte();
        //Serial.print("Popped byte: ");
        //Serial.println(b[i], HEX);
    }
    float *f = (float *)b;
    return *f;
}

void pushString(const char *string) {
    int length = strlen(string);
    //Serial.println(("stringlength = " + String(length));

    for (int i = 0; i < length; i++) {
        pushByte(string[i]);
        //Serial.println((string[i]);
    }
    
    pushByte('\0');              
    pushByte(length + 1);         
    pushByte(STRING);              
}

char* popString() {
    int length = popByte();
    if (length <= 0) {
        return NULL; // Invalid length
    }
    // Serial.print(F("Found length of: "));
    // Serial.println(length);

    char* result = (char*)malloc((length + 1) * sizeof(char));
    if (result == NULL) {
        return NULL;  
    }

    for (int i = 0; i < length; i++) {
        result[i] = popByte();
    }
    result[length] = '\0';

    return result;
}

void setVar(const char* name, int processID) {
    // Check for space in the memory table
    if (noOfVars >= MAX_VARS) {
        Serial.println(F("Error: No space in memory table"));
        return;
    }

    // Check if the variable with the same name and processID already exists
    for (int i = 0; i < noOfVars; i++) {
        if (strcmp(memoryTable[i].name, name) == 0 && memoryTable[i].processID == processID) {
            // Remove the existing variable by shifting entries
            for (int j = i; j < noOfVars - 1; j++) {
                memoryTable[j] = memoryTable[j + 1];
            }
            noOfVars--;
            break;
        }
    }

    if (stackPointer < 1) {
        Serial.println(F("Error: Not enough data on stack"));
        return;
    }

    byte type = popByte();
    int size = 0;

    // Determine the size based on the type
    switch (type) {
        case CHAR:
            size = sizeof(char);
            break;
        case INT:
            size = sizeof(int);
            break;
        case FLOAT:
            size = sizeof(float);
            break;
        case STRING:
            if (stackPointer < 1) {
                Serial.println(F("Error: Not enough data on stack for string length"));
                return;
            }
            size = popByte();
            break;
        default:
            Serial.println(F("Error: Unknown variable type"));
            return;
    }

    // Find free memory space
    int address = findFreeSpaceInMemoryTable(size);
    Serial.print(F("avalible space = "));
    Serial.println(address);
    if (address == -1) {
        Serial.println(F("Error: No free memory"));
        return;
    }

    // Add new variable entry
    strcpy(memoryTable[noOfVars].name, name);
    memoryTable[noOfVars].type = type;
    memoryTable[noOfVars].address = address;
    memoryTable[noOfVars].size = size;
    memoryTable[noOfVars].processID = processID;
    noOfVars++;

    // Write data to memory
    Serial.println(F("Writing to memory:"));
    for (int i = 0; i < size; i++) {
        if (stackPointer < 1) {
            Serial.println(F("Error: Not enough data on stack to write to memory"));
            return;
        }
        byte data = popByte();
        memory[address + i] = data;
        // Serial.print(F("Address "));
        // Serial.print(address + i);
        // Serial.print(F(": "));
        // Serial.println(data);
    }

    // Write type and size information to memory
    if (type == STRING) {
        memory[address + size] = STRING; // Store type
        memory[address + size + 1] = size; // Store length after the string
    } else {
        memory[address + size] = type; // Store type for other data types
    }

    Serial.print(F("Variable "));
    Serial.print(name);
    Serial.println(F(" set successfully."));
}

void getVar(const char* name, int processID) {
    Serial.print(F("Getting variable: "));
    Serial.print(name);
    Serial.print(F(", Process ID: "));
    Serial.println(processID);
    Serial.println(F("Number of variables: "));
    Serial.println(noOfVars);

    // Search for the variable in the memoryTable
    for (int i = 0; i < noOfVars; i++) {
        if (strcmp(memoryTable[i].name, name) == 0 && memoryTable[i].processID == processID) {
            int size = memoryTable[i].size;
            int address = memoryTable[i].address;
            byte type = memoryTable[i].type;

            for (int j = 0; j < size; j++) {
                pushByte(memory[address + j]);
            }

            if (type == STRING) {
                // Push the length of the string (stored after the data in memory)
                Serial.print(F("Pushing length: "));
                pushByte(memory[address + size + 1]);
            }

            // Push the type of the variable after the data
            pushByte(type);

            Serial.print(F("Found variable '"));
            Serial.print(name);
            Serial.print(F("' at address "));
            Serial.print(address);
            Serial.print(F(" with size "));
            Serial.println(size);
            return;
        }
    }

    // Error if variable not found
    Serial.println(F("Error: Variable not found"));
}

int findFreeSpaceInMemoryTable(int size) {
    for (int i = 0; i <= MEMORYSIZE - size; i++) {
        bool available = true;
        for (int j = 0; j < noOfVars; j++) {
            if (i >= memoryTable[j].address && i < (memoryTable[j].address + memoryTable[j].size)) {
                available = false;
                break;
            }
        }
        if (available) {
            return i;
        }
    }
    return -1; // No free space
}

void deleteVariables(int processID) {
    for (int i = 0; i < noOfVars; i++) {
        if (memoryTable[i].processID == processID) {
            for (int j = i; j < noOfVars - 1; j++) {
                memoryTable[j] = memoryTable[j + 1];
            }
            noOfVars--;
            i--; 
        }
    }
}

void help() {
    Serial.println(F("Available commands:"));
    for (int i = 0; i < nCommands; i++) {
        Serial.println(command[i].name);
    }
}

void store() {
    Serial.println(F("Executing store command"));

    char filename[FILENAMESIZE] = "";
    if (!readToken(filename)) { 
        Serial.println(F("Error: Unknown filename"));
        return;
    }

    Serial.print(F("Filename: "));
    Serial.println(filename);

    char sizeBuffer[BUFSIZE] = "";
    if (!readToken(sizeBuffer)) { 
        Serial.println(F("Error: Unknown size"));
        return;
    }

    Serial.print(F("Size: "));
    Serial.println(sizeBuffer);

    int fileSize = atoi(sizeBuffer);

    if (fileSize <= 0) {
        Serial.println(F("Error: Invalid file size"));
        return;
    }

    if (findFileInFAT(filename) != -1) {
        Serial.println(F("Error: File already exists"));
        return;
    }

    int freeSpace = findFreeSpace(fileSize);
    if (freeSpace == -1) {
        Serial.println(F("Error: Not enough space to store the file"));
        return;
    }

    int index = findEmptyFATEntry();
    if (index == -1) {
        Serial.println(F("Error: No empty FAT entry"));
        return;
    }

    FAT[index].start = freeSpace;
    FAT[index].size = fileSize;
    strncpy(FAT[index].name, filename, FILENAMESIZE);

    // Serial.print(F("File content will be stored starting at EEPROM address: "));
    // Serial.println(freeSpace);

    writeFATEntry(index);

    char content[fileSize];
    ////Serial.println((content);
    for (int i = 0; i < fileSize; i++) {
        while (!Serial.available());
        content[i] = Serial.read();
        //retrieve kaaSerial.print(content[i]);
        delay(1);
        EEPROM.write(freeSpace + i, content[i]);
    }

    noOfFiles++;
    EEPROM.write(160, noOfFiles);
    //Serial.println(("");
    Serial.println(F("File stored successfully"));
}


int findEmptyFATEntry() {
    for (int i = 0; i < MAX_FILES; i++) {
        if (FAT[i].size == 0) {
            return i;
        }
    }
    return -1;
}

int findFreeSpace(int fileSize) {
    int startAddress = 161;
    for (int i = 0; i < MAX_FILES; i++) {
        if (FAT[i].size > 0) {
            int endAddress = FAT[i].start + FAT[i].size;
            if (endAddress > startAddress) {
                startAddress = endAddress;
            }
        }
    }

    if (startAddress + fileSize <= EEPROM_SIZE) {
        return startAddress;
    } else {
        return -1;
    }
}

void writeFATEntry(int index) {
    EEPROM.put(index * sizeof(fileType), FAT[index]);

    // Serial.print("FAT entry ");
    // Serial.print(index);
    // Serial.print(" written at EEPROM address: ");
    //Serial.println((index * sizeof(fileType));
}


void initializeFAT() {
    for (int i = 0; i < MAX_FILES; i++) {
        EEPROM.get(i * sizeof(fileType), FAT[i]);
    }
    noOfFiles = EEPROM.read(160);
}

void retrieve() {
    Serial.println(F("Executing retrieve command"));

    char filename[FILENAMESIZE] = "";
    if (!readToken(filename)) {
        Serial.println(F("Error: Unknown filename"));
        return;
    }

    int index = findFileInFAT(filename);
    if (index == -1) {
        Serial.println(F("Error: File not found"));
        return;
    }

    char data[FAT[index].size];
    for (int i = 0; i < FAT[index].size; i++) {
        data[i] = EEPROM.read(FAT[index].start + i);
    }

    Serial.println(F("File content:"));
    for (int i = 0; i < FAT[index].size; i++) {
        Serial.print(data[i]);
    }
    Serial.println();
}

int findFileInFAT(char* filename) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (strcmp(FAT[i].name, filename) == 0) {
            return i;
        }
    }
    return -1;
}

void erase() {
    Serial.println(F("Executing erase command"));

    char filename[FILENAMESIZE] = "";
    if (!readToken(filename)) {
        Serial.println(F("Error: Unknown filename"));
        return;
    }

    int index = findFileInFAT(filename);
    if (index == -1) {
        Serial.println(F("Error: File not found"));
        return;
    }

    int fileSize = FAT[index].size;
    int startAddress = FAT[index].start;

    // Shift all files that come after the erased file
    for (int i = index + 1; i < MAX_FILES; i++) {
        if (FAT[i].size > 0) {
            // Calculate the new start address
            int newStartAddress = FAT[i].start - fileSize;

            // Move the file content in EEPROM
            for (int j = 0; j < FAT[i].size; j++) {
                EEPROM.write(newStartAddress + j, EEPROM.read(FAT[i].start + j));
            }

            // Update the FAT entry
            FAT[i - 1] = FAT[i];  // Shift FAT entry up
            FAT[i - 1].start = newStartAddress;
            writeFATEntry(i - 1);
        }
    }

    // Clear the last FAT entry (now duplicated after shifting)
    FAT[noOfFiles - 1].size = 0;
    FAT[noOfFiles - 1].start = 0;
    memset(FAT[noOfFiles - 1].name, 0, FILENAMESIZE);
    writeFATEntry(noOfFiles - 1);

    noOfFiles--;
    EEPROM.write(160, noOfFiles);

    Serial.println(F("File erased and subsequent files shifted successfully"));
}


void files() {

    Serial.println("Listing files:");
    for (int i = 0; i < noOfFiles; i++) {
        Serial.print(FAT[i].name);
        Serial.print(F(" - "));
        Serial.print(FAT[i].size);
        Serial.print(F(" bytes at address "));
        Serial.println(FAT[i].start);
    }
}

void freespace() {
    int maxFreeSpace = EEPROM_SIZE - 161;

    for (int i = 0; i < MAX_FILES; i++) {
        if (FAT[i].size > 0) {
            maxFreeSpace -= FAT[i].size;
        }
    }

    // Serial.print("Max available space: ");
    // Serial.println(maxFreeSpace);
}

void run() {
    Serial.println(F("Executing run command"));

    char filename[FILENAMESIZE] = "";
    if (!readToken(filename)) {
        Serial.println(F("Error: Unknown filename"));
        return;
    }

    // Check if file exists in the FAT
    int index = findFileInFAT(filename);
    if (index == -1) {
        Serial.println(F("Error: File not found"));
        return;
    }

    // Check if there is space in the process table
    if (noOfProcesses >= MAXPROCESSES) {
        Serial.println(F("Error: Process table is full."));
        return;
    }

    // Initialize the new process
    int processId = noOfProcesses;
    fileType fileEntry = FAT[index];

    strcpy(processes[processId].name, filename);
    processes[processId].processId = processId;
    processes[processId].state = 'r'; // RUNNING state constant
    processes[processId].pc = fileEntry.start; // Set Program Counter to the file start address
    processes[processId].fp = 0; // Frame Pointer initially set to 0
    processes[processId].sp = 0; // Stack Pointer initially set to 0
    processes[processId].loop_start = -1; // Loop start not set

    // Increment process count
    noOfProcesses++; 

    // Print the success message
    Serial.print(F("Process "));
    Serial.print(filename);
    Serial.print(F(" with ID "));
    Serial.print(processId);
    Serial.println(F(" started successfully."));
}

void list() {
    if (noOfProcesses == 0) {
        Serial.println(F("No active processes."));
        return;
    }

    //Serial.println(("Active processes:");
    for (int i = 0; i < noOfProcesses; i++) {
        Serial.print(F("Process "));
        Serial.print(processes[i].name);
        Serial.print(F(" (ID: "));
        Serial.print(processes[i].processId);
        Serial.print(F(") - "));

        if (processes[i].state == 'r') {
            Serial.print(F("RUNNING"));
        } else if (processes[i].state == 'p') {
            Serial.print(F("PAUSED"));
        } else {
            Serial.print(F("UNKNOWN STATE"));
        }

        // Serial.print(F(" PC: "));
        // Serial.print(processes[i].pc);
        // Serial.print(F(" FP: "));
        // Serial.print(processes[i].fp);
        // Serial.print(F(" SP: "));
        // Serial.println(processes[i].sp);
    }
}


void suspend() {
    char processid[BUFSIZE] = "";
    if (!readToken(processid)) { 
        Serial.println(F("Error: Unknown size"));
        return;
    }

    Serial.print(F("id: "));
    Serial.println(processid);

    int processID = atoi(processid);
    suspendProcess(processID);
}

void suspendProcess(int processID) {
    int processIndex = -1;
    for (int i = 0; i < noOfProcesses; i++) {
        if (processes[i].processId == processID) {
            processIndex = i;
            break;
        }
    }

    // Check if the process ID was found
    if (processIndex == -1) {
        Serial.println(F("Error: Invalid process ID"));
        return;
    }

    // Check if the process is already suspended
    if (processes[processIndex].state == 'p') {
        Serial.println(F("Error: Process is already suspended"));
        return;
    }

    // Suspend the process
    processes[processIndex].state = 'p';
    Serial.print(F("Process suspended: "));
    Serial.println(processes[processIndex].name);
}

void resume() {
    char processid[BUFSIZE] = "";
    if (!readToken(processid)) {
        Serial.println(F("Error: Unknown size"));
        return;
    }

    int processID = atoi(processid);
    resumeProcess(processID);
}

void resumeProcess(int processID) {
    int processIndex = -1;
    for (int i = 0; i < noOfProcesses; i++) {
        if (processes[i].processId == processID) {
            processIndex = i;
            break;
        }
    }
    if (processIndex == -1) {
        Serial.println(F("Error: Invalid process ID"));
        return;
    }

    // Check if the process is already running
    if (processes[processIndex].state == 'r') {
        Serial.println(F("Error: Process is already running"));
        return;
    }

    // Resume the process
    processes[processIndex].state = 'r';
    Serial.print(F("Process resumed: "));
    Serial.println(processes[processIndex].name);
}


void kill() {
    char processid[BUFSIZE] = "";
    if (!readToken(processid)) {
        Serial.println(F("Error: Unknown size"));
        return;
    }

    int processID = atoi(processid);
    killProcess(processID);
}

void killProcess(int processID) {
    int processIndex = -1;
    for (int i = 0; i < noOfProcesses; i++) {
        if (processes[i].processId == processID) {
            processIndex = i;
            break;
        }
    }

    // Check if the process ID was found
    if (processIndex == -1) {
        Serial.println(F("Error: Invalid process ID"));
        return;
    }

    // Clear all variables associated with this process
    deleteVariables(processID);

    // Option 1: Mark the process as TERMINATED
    processes[processIndex].state = '0';

    // Option 2: Remove the process from the process table by shifting the remaining processes
    for (int i = processIndex; i < noOfProcesses - 1; i++) {
        processes[i] = processes[i + 1];
    }
    noOfProcesses--;

    // Clear the last process entry
    processes[noOfProcesses].state = '0';

    Serial.print(F("Process "));
    Serial.print(processID);
    Serial.println(F(" terminated successfully."));
}


void show() {
    // Serial.println(F("Show command");
    // Serial.println((noOfFiles);
}

void deleteAllFiles() {
    //Serial.println(("Deleting all files...");
    for (int i = 0; i < MAX_FILES; i++) {
        FAT[i].name[0] = '\0';  
        FAT[i].start = 0;
        FAT[i].size = 0;
        writeFATEntry(i);  
    }
    noOfFiles = 0;
    EEPROM.write(160, noOfFiles);
    Serial.println(F("All files deleted successfully."));
}

bool readToken(char* buffer) {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == ' ') {
            buffer[bufferIndex] = '\0';
            bufferIndex = 0;
            return true;
        } else {
            buffer[bufferIndex++] = c;
            if (bufferIndex >= BUFSIZE) bufferIndex = BUFSIZE - 1;
        }
    }
    delay(100);
    return false;
}



void processCommand(char* input) {
    for (int i = 0; i < nCommands; i++) {
        if (strcmp(input, command[i].name) == 0) {
            command[i].func();
            return;
        }
    } 
    Serial.println(F("Unknown command. Type 'help' for a list of commands."));
}

void runProcesses() {
    for (int i = 0; i < noOfProcesses; i++) {
        if (processes[i].state == 'r') { // Check if the process is RUNNING
            execute(i); // Execute the next instruction for this process
            delay(200); // Delay to allow other operations and avoid overwhelming the CPU
        }
    }
}

void execute(int processIndex) {
    int address = processes[processIndex].fp; // Frame Pointer
    int pc = processes[processIndex].pc; // Program Counter

    byte instruction = EEPROM.read(address + pc);
    processes[processIndex].pc += 1; // Move PC to the next instruction

    switch (instruction) {
        case CHAR: {
            char charValue = EEPROM.read(address + processes[processIndex].pc);
            processes[processIndex].pc += 1; // Move PC to the next instruction
            pushChar(charValue);
            break;
        }
        case INT: {
            int intValue = EEPROM.read(address + processes[processIndex].pc);
            processes[processIndex].pc += 1; // Move PC to the next instruction
            pushInt(intValue);
            break;
        }
        case STRING: {
            String strValue;
            byte charValue;
            while ((charValue = EEPROM.read(address + processes[processIndex].pc)) != '\0') {
                strValue += (char)charValue;
                processes[processIndex].pc += 1; // Move PC to the next character
            }
            processes[processIndex].pc += 1; // Move PC past the null-terminator
            pushString(strValue.c_str()); // Convert String to const char* and push
            break;
        }
        case FLOAT: {
            byte b[4];
            for (int i = 0; i < 4; i++) {
                b[i] = EEPROM.read(address + processes[processIndex].pc);
                processes[processIndex].pc += 1; // Move PC to the next byte
            }
            float *pf = (float *)b;
            pushFloat(*pf);
            break;
        }
        // case PRINT: {
        //     Serial.print(F("PRINT: "));
        //     printVar(processes[processIndex].processId);
        //     break;
        // }
        // case PRINTLN: {
        //     Serial.print(F("PRINTLN: "));
        //     printVar(processes[processIndex].processId);
        //     Serial.println();
        //     break;
        // }
        default: {
            Serial.print(F("Error: Unknown instruction "));
            Serial.println(instruction);
            break;
        }
    }
}

void loop() {
    if (readToken(inputBuffer)) {
        processCommand(inputBuffer);
    }
    //runProcesses();
}

