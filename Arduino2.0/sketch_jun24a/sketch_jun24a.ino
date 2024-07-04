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
    byte state;
    int pc;
    int sp;
    byte stack[STACKSIZE];
} processType;

fileType FAT[MAX_FILES];
EERef noOfFiles = EEPROM[160];

MemoryTableEntry memoryTable[MAX_VARS];
int noOfVars = 0;
byte memory[MEMORYSIZE];

byte stack[STACKSIZE];
int stackPointer = 0;

processType processes[MAXPROCESSES];
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
    Serial.println(noOfFiles);
    Serial.println("ArduinOS 1.0 ready");
    bufferIndex = 0;
    noOfVars = 0;
    initializeFAT();    
}


// Push and Pop functions
void pushByte(byte b) {
  //Serial.printnl("TEST1");
    if (stackPointer < STACKSIZE -1) {
      //Serial.printnl("TEST2");
        stack[stackPointer++] = b;
        Serial.print("Pushed byte: ");
        Serial.println(b, HEX);
        Serial.print("Stack pointer is now: ");
        Serial.println(stackPointer);
    } else {
        //Serial.println("Error: Stack overflow");
    }
}

byte popByte() {
    if (stackPointer > 0) {
        byte b = stack[--stackPointer];
        Serial.print("Popped byte: ");
        Serial.println(b, HEX);
        Serial.print("Stack pointer is now: ");
        Serial.println(stackPointer);
        return b;
    } else {
        //Serial.println("Error: Stack underflow");
        return 0;
    }
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
    byte low = popByte();
    byte high = popByte();
    return word(high, low);
}

void pushFloat(float f) {
    byte *b = (byte *)&f;
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
    return *((float *)b);
}

void pushString(const char *s) {
    int length = strlen(s);
    for (int i = 0; i < length; i++) {
        pushByte(s[i]);
    }
    pushByte(length); // Push the length of the string
    pushByte(STRING); // Push the type identifier
}



// void popString() {
//     int length = popByte();
//     char *s = new char[length];
//     for (int i = length - 1; i >= 0; --i) {
//         s[i] = popByte();
//     }
//     return s;
// }
char* popString() {

    int length = popByte(); // Read the length of the string
    //Serial.println(length);
    char *s = (char*)malloc(length + 1); // Allocate memory for the string (+1 for null terminator)
    if (s == NULL) {
        Serial.println("Error: Memory allocation failed");
        return NULL;
    }
    for (int i = 0; i < length; i++) {
        Serial.println(i);
        s[i] = popByte(); // Read each character from the stack
    }
    s[length] = '\0'; // Null-terminate the string
    return s;
}
// Set Variable
void setVar(const char* name, int processID) {
    Serial.print("Setting variable: ");
    Serial.print(name);
    Serial.print(", Process ID: ");
    Serial.println(processID);

    if (stackPointer < 2) {
        Serial.println("Error: Not enough data on stack");
        return;
    }

    byte type = popByte();
    int size = 0;
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
            size = popByte();
            break;
        default:
            Serial.println("Error: Unknown variable type");
            return;
    }

    // Allocate memory for the variable
    int address = -1;
    for (int i = 0; i <= MEMORYSIZE - size; i++) {
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
    if (address == -1) {
        Serial.println("Error: No free memory");
        return;
    }

    // Add the variable to the memory table
    strcpy(memoryTable[noOfVars].name, name);
    memoryTable[noOfVars].type = type;
    memoryTable[noOfVars].address = address;
    memoryTable[noOfVars].size = size;
    memoryTable[noOfVars].processID = processID;
    noOfVars++;

    // Write the variable data from the stack to memory
    for (int i = size - 1; i >= 0; i--) {
        memory[address + i] = popByte();
    }

    Serial.print("Variable ");
    Serial.print(name);
    Serial.println(" set successfully.");
}

// Get Variable
void getVar(const char* name, int processID) {
    Serial.print("Getting variable: ");
    Serial.print(name);
    Serial.print(", Process ID: ");
    Serial.println(processID);

    for (int i = 0; i < noOfVars; i++) {
        if (strcmp(memoryTable[i].name, name) == 0 && memoryTable[i].processID == processID) {
            int size = memoryTable[i].size;
            for (int j = 0; j < size; j++) {
                pushByte(memory[memoryTable[i].address + j]);
            }
            pushByte(memoryTable[i].type); // Push the type after the data
            Serial.print("Found variable '");
            Serial.print(name);
            Serial.print("' at address ");
            Serial.print(memoryTable[i].address);
            Serial.print(" with size ");
            Serial.println(size);
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
            i--; 
        }
    }
}

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

    Serial.print("File content will be stored starting at EEPROM address: ");
    Serial.println(freeSpace);

    writeFATEntry(index);

    char content[fileSize];
    //Serial.println(content);
    for (int i = 0; i < fileSize; i++) {
        while (!Serial.available());
        content[i] = Serial.read();
        Serial.print(content[i]);
        delay(1);
        EEPROM.write(freeSpace + i, content[i]);
    }

    noOfFiles++;
    EEPROM.write(160, noOfFiles);
    Serial.println("");
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

    Serial.print("FAT entry ");
    Serial.print(index);
    Serial.print(" written at EEPROM address: ");
    Serial.println(index * sizeof(fileType));
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

// void loop() {
//     if (readToken(inputBuffer)) {
//         processCommand(inputBuffer);
//     }
// }
void loop() {
  // pushChar('a');
  // setVar('x', 0);
  // getVar('x', 0);
  // popByte(); 
  // Serial.println(popChar());

    pushString("Hallo");
    setVar('s', 3);
    getVar('s', 3);
    popByte(); 
    Serial.println(popString());

    // Delay for a while to observe the output
    delay(5000); // 5 seconds delay between loop iterations
}