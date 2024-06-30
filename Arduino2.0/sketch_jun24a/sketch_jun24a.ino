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

fileType FAT[MAX_FILES];
EERef noOfFiles = EEPROM[160];

MemoryTableEntry memoryTable[MAX_VARS];
int noOfVars = 0;
byte memory[MEMORYSIZE];

byte stack[STACKSIZE];
byte sp = 0;

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
    Serial.println(noOfFiles);
    Serial.println("ArduinOS 1.0 ready");
    bufferIndex = 0;
    initializeFAT();
}

// Stack operations
void pushByte(byte b) {
    stack[sp++] = b;
}

byte popByte() {
    return stack[--sp];
}

void pushInt(int val) {
    pushByte(lowByte(val));
    pushByte(highByte(val));
    pushByte(INT);
}

int popInt() {
    byte high = popByte();
    byte low = popByte();
    return word(low, high);
}

void pushFloat(float val) {
    byte *b = (byte *)&val;
    for (int i = 3; i >= 0; i--) {
        pushByte(b[i]);
    }
    pushByte(FLOAT);
}

float popFloat() {
    byte b[4];
    for (int i = 0; i < 4; i++) {
        b[i] = popByte();
    }
    float *pf = (float *)b;
    return *pf;
}

void pushString(const char *str) {
    int len = strlen(str) + 1;
    for (int i = len - 1; i >= 0; i--) {
        pushByte(str[i]);
    }
    pushByte(len);
    pushByte(STRING);
}

char *popString() {
    byte type = popByte();
    int len = popByte();
    sp -= len; 
    return (char *)&stack[sp];
}

// Memory management
void storeVariable(char name, int processID) {
    if (noOfVars >= MAX_VARS) {
        Serial.println("Error: Memory table full");
        return;
    }
    
    int idx = -1;
    for (int i = 0; i < noOfVars; i++) {
        if (memoryTable[i].name == name && memoryTable[i].processID == processID) {
            idx = i;
            break;
        }
    }
    
    if (idx != -1) { // Variable exists, remove it
        for (int i = idx; i < noOfVars - 1; i++) {
            memoryTable[i] = memoryTable[i + 1];
        }
        noOfVars--;
    }
    
    byte type = popByte();
    int size;
    switch (type) {
        case CHAR:
            size = 1;
            break;
        case INT:
            size = 2;
            break;
        case FLOAT:
            size = 4;
            break;
        case STRING:
            size = popByte();
            sp++; // Skip the type byte
            break;
    }
    
    int address = -1;
    for (int i = 0; i < MEMORYSIZE; i++) {
        bool available = true;
        for (int j = 0; j < noOfVars; j++) {
            if (i >= memoryTable[j].address && i < (memoryTable[j].address + memoryTable[j].size)) {
                available = false;
                break;
            }
        }
        if (available) {
            address = i;
            break;
        }
    }
    
    if (address == -1 || (address + size) > MEMORYSIZE) {
        Serial.println("Error: Not enough memory");
        return;
    }
    
    memoryTable[noOfVars] = {name, type, address, size, processID};
    noOfVars++;
    
    for (int i = 0; i < size; i++) {
        memory[address + i] = popByte();
    }
}

void retrieveVariable(char name, int processID) {
    for (int i = 0; i < noOfVars; i++) {
        if (memoryTable[i].name == name && memoryTable[i].processID == processID) {
            int address = memoryTable[i].address;
            int size = memoryTable[i].size;
            for (int j = 0; j < size; j++) {
                pushByte(memory[address + j]);
            }
            pushByte(memoryTable[i].type);
            return;
        }
    }
    Serial.println("Error: Variable not found");
}

void deleteVariables(int processID) {
    for (int i = 0; i < noOfVars; i++) {
        if (memoryTable[i].processID == processID) {
            for (int j = i; j < noOfVars - 1; j++) {
                memoryTable[j] = memoryTable[j + 1];
            }
            noOfVars--;
            i--; // Check the new entry at this position
        }
    }
}

// Process management
void createProcess(const char* name) {
    if (noOfProcesses >= MAXPROCESSES) {
        Serial.println("Error: Maximum number of processes reached");
        return;
    }

    int processID = noOfProcesses;
    strncpy(processes[processID].name, name, FILENAMESIZE);
    processes[processID].processId = processID;
    processes[processID].state = 0; // 0 = ready, 1 = running, 2 = suspended, etc.
    processes[processID].pc = 0;
    processes[processID].sp = 0;

    noOfProcesses++;
    Serial.print("Process created: ");
    Serial.println(name);
}

void listProcesses() {
    Serial.println("Listing processes:");
    for (int i = 0; i < noOfProcesses; i++) {
        Serial.print("Process ID: ");
        Serial.print(processes[i].processId);
        Serial.print(", Name: ");
        Serial.print(processes[i].name);
        Serial.print(", State: ");
        Serial.print(processes[i].state);
        Serial.print(", PC: ");
        Serial.print(processes[i].pc);
        Serial.print(", SP: ");
        Serial.println(processes[i].sp);
    }
}

void suspendProcess(int processID) {
    if (processID < 0 || processID >= noOfProcesses) {
        Serial.println("Error: Invalid process ID");
        return;
    }
    processes[processID].state = 2; // Suspended
    Serial.print("Process suspended: ");
    Serial.println(processes[processID].name);
}

void resumeProcess(int processID) {
    if (processID < 0 || processID >= noOfProcesses) {
        Serial.println("Error: Invalid process ID");
        return;
    }
    processes[processID].state = 0; // Ready
    Serial.print("Process resumed: ");
    Serial.println(processes[processID].name);
}

void killProcess(int processID) {
    if (processID < 0 || processID >= noOfProcesses) {
        Serial.println("Error: Invalid process ID");
        return;
    }
    for (int i = processID; i < noOfProcesses - 1; i++) {
        processes[i] = processes[i + 1];
    }
    noOfProcesses--;
    Serial.println("Process killed");
}

void help() {
    Serial.println("Available commands:");
    for (int i = 0; i < nCommands; i++) {
        Serial.println(command[i].name);
    }
}

void store() {
    Serial.println("Executing store command");

    char filename[FILENAMESIZE] = "";
    if (!readToken(filename)) { 
        Serial.println("Error: Unknown filename");
        return;
    }

    Serial.print("Filename: ");
    Serial.println(filename);

    char sizeBuffer[BUFSIZE] = "";
    if (!readToken(sizeBuffer)) { 
        Serial.println("Error: Unknown size");
        return;
    }

    Serial.print("Size: ");
    Serial.println(sizeBuffer);

    int fileSize = atoi(sizeBuffer);

    if (fileSize <= 0) {
        Serial.println("Error: Invalid file size");
        return;
    }

    if (findFile(filename) != -1) {
        Serial.println("Error: File already exists");
        return;
    }

    int freeSpace = findFreeSpace(fileSize);
    if (freeSpace == -1) {
        Serial.println("Error: Not enough space to store the file");
        return;
    }

    int index = findEmptyFATEntry();
    if (index == -1) {
        Serial.println("Error: No empty FAT entry");
        return;
    }

    FAT[index].start = freeSpace;
    FAT[index].size = fileSize;
    strncpy(FAT[index].name, filename, FILENAMESIZE);

    writeFATEntry(index);

    char content[fileSize];
    for (int i = 0; i < fileSize; i++) {
        while (!Serial.available());
        content[i] = Serial.read();
        EEPROM.write(freeSpace + i, content[i]);
    }

    noOfFiles++;
    EEPROM.write(160, noOfFiles);

    Serial.println("File stored successfully");
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
}

void initializeFAT() {
    for (int i = 0; i < MAX_FILES; i++) {
        EEPROM.get(i * sizeof(fileType), FAT[i]);
    }
    noOfFiles = EEPROM.read(160);
}

void retrieve() {
    Serial.println("Executing retrieve command");

    char filename[FILENAMESIZE] = "";
    if (!readToken(filename)) {
        Serial.println("Error: Unknown filename");
        return;
    }

    int index = findFileInFAT(filename);
    if (index == -1) {
        Serial.println("Error: File not found");
        return;
    }

    char data[FAT[index].size];
    for (int i = 0; i < FAT[index].size; i++) {
        data[i] = EEPROM.read(FAT[index].start + i);
    }

    Serial.println("File content:");
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
    Serial.println("Executing erase command");

    char filename[FILENAMESIZE] = "";
    if (!readToken(filename)) {
        Serial.println("Error: Unknown filename");
        return;
    }

    int index = findFileInFAT(filename);
    if (index == -1) {
        Serial.println("Error: File not found");
        return;
    }

    FAT[index].size = 0;
    writeFATEntry(index);

    noOfFiles--;
    EEPROM.write(160, noOfFiles);
    Serial.println("File erased successfully");
}

void files() {
    Serial.println("Listing files:");
    for (int i = 0; i < MAX_FILES; i++) {
        if (FAT[i].size > 0) {
            Serial.print("Name: ");
            Serial.print(FAT[i].name);
            Serial.print(", Size: ");
            Serial.println(FAT[i].size);
        }
    }
}

void freespace() {
    int maxFreeSpace = EEPROM_SIZE - 161;

    for (int i = 0; i < MAX_FILES; i++) {
        if (FAT[i].size > 0) {
            maxFreeSpace -= FAT[i].size;
        }
    }

    Serial.print("Max available space: ");
    Serial.println(maxFreeSpace);
}

void run() {
    Serial.println("Run command not implemented yet");
}

void list() {
    Serial.println("List command not implemented yet");
}

void suspend() {
    Serial.println("Suspend command not implemented yet");
}

void resume() {
    Serial.println("Resume command not implemented yet");
}

void kill() {
    Serial.println("Kill command not implemented yet");
}

void show() {
    Serial.println("Show command");
    Serial.println(noOfFiles);
}

void deleteAllFiles() {
    Serial.println("Deleting all files...");
    for (int i = 0; i < MAX_FILES; i++) {
        FAT[i].name[0] = '\0';  
        FAT[i].start = 0;
        FAT[i].size = 0;
        writeFATEntry(i);  
    }
    noOfFiles = 0;
    EEPROM.write(160, noOfFiles);
    Serial.println("All files deleted successfully.");
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

int findFile(char* filename) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (strcmp(FAT[i].name, filename) == 0) {
            return i;
        }
    }
    return -1;
}

void processCommand(char* input) {
    for (int i = 0; i < nCommands; i++) {
        if (strcmp(input, command[i].name) == 0) {
            command[i].func();
            return;
        }
    }
    Serial.println("Unknown command. Type 'help' for a list of commands.");
}

void loop() {
    if (readToken(inputBuffer)) {
        processCommand(inputBuffer);
    }
}
