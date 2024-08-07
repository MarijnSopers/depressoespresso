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
    int process_id;
    char state; // 'r' for RUNNING, 'p' for PAUSED, '0' for TERMINATED
    int PC; // Program Counter
    int FP; // File Pointer
    int SP; // Stack Pointer
    int loop_start; // Address of the start of the loop
    int stack[STACKSIZE]; // Separate stack for each process
} Process;

fileType FAT[MAX_FILES];
EERef noOfFiles = EEPROM[160];

MemoryTableEntry memoryTable[MAX_VARS];
int noOfVars = 0;
byte memory[MEMORYSIZE];

byte stack[STACKSIZE];
int stackPointer = 0; //zet terug naar 0 

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

void pushByte(byte b) {
    if (stackPointer >= sizeof(stack)) {
        Serial.println("Error: Stack overflow");
        return;
    }
    stack[stackPointer++] = b;
    Serial.print("Pushed byte: ");
    Serial.println(b);
}


byte popByte() {
    if (stackPointer <= 0) {
        Serial.println("Error: Stack underflow");
        return 0;
    }
    byte b = stack[--stackPointer];
    Serial.print("Popped byte: ");
    Serial.print(b);
    Serial.print(" / ");
    Serial.println((char)b);

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
        Serial.println("Error: Not enough data on stack");
        return 0; // Handle the error appropriately
    }
    byte lowByte = popByte();   
    byte highByte = popByte();   
    int value = word(lowByte, highByte);  

    return value;
}

void pushFloat(float f) {
    byte *b = (byte *)&f;
    Serial.println("Pushing float bytes: ");
    Serial.print("sizeof the value: ");
    Serial.println(sizeof(float));
    for (int i = sizeof(float) - 1; i >= 0; i--) {
        pushByte(b[i]);
        Serial.print(b[i], HEX);
        Serial.print(" ");
    }
    Serial.println();  // Newline for clarity
    pushByte(FLOAT);  // Push the type identifier last
}


float popFloat() {
    byte b[sizeof(float)];
    for (int i = sizeof(float) - 1; i >= 0; i--) {
        b[i] = popByte();
        Serial.print("Popped byte: ");
        Serial.println(b[i], HEX);
    }
    float *f = (float *)b;
    return *f;
}



void pushString(const char *string) {
    
    int length = strlen(string);
    Serial.println("stringlength = " + String(length));

    
    for (int i = 0; i < length; i++) {
        pushByte(string[i]);
        Serial.println(string[i]);
    }
    
    pushByte('\0');                // Push terminating zero
    pushByte(length + 1);          // Push length of the string including the terminating zero
    pushByte(STRING);              // Push the type identifier

    // Serial.print(F("String pushed: "));
    // Serial.println(string);
} 

char* popString() {
    // Pop the length byte
    int length = popByte();
    if (length <= 0) {
        return NULL; // Invalid length
    }
    Serial.print("Found length of: ");
    Serial.println(length);

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
    // Serial.print("Setting variable: ");
    // Serial.print(name);
    // Serial.print(", Process ID: ");
    // Serial.println(processID);

    if (stackPointer < 2) {
        Serial.println("Error: Not enough data on stack");
        return;
    }

    byte type = popByte(); // Read the type of the variable
    Serial.print("type = ");
    Serial.println(type);
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
            size = popByte(); // Read the length of the string
            Serial.print("String length = ");
            Serial.println(size);
            break;
        default:
            Serial.println("Error: Unknown variable type");
            return;
    }

    // Find free memory space
    int address = findFreeSpaceInMemoryTable(size);
    if (address == -1) {
        Serial.println("Error: No free memory");
        return;
    }

    // Check for existing variable with the same name and processID
    for (int i = 0; i < noOfVars; i++) {
        if (strcmp(memoryTable[i].name, name) == 0 && memoryTable[i].processID == processID) {
            // Shift variables to remove the existing one
            for (int j = i; j < noOfVars - 1; j++) {
                memoryTable[j] = memoryTable[j + 1];
            }
            noOfVars--;
            break;
        }
    }

    // Add new variable entry
    strcpy(memoryTable[noOfVars].name, name);
    memoryTable[noOfVars].type = type;
    memoryTable[noOfVars].address = address;
    memoryTable[noOfVars].size = size;
    memoryTable[noOfVars].processID = processID;
    noOfVars++;

    // Write data to memory
    Serial.println("Writing to memory:");
    for (int i = 0; i < size; i++) {
        byte data = popByte();
        memory[address + i] = data;
        // Serial.print("Address ");
        // Serial.print(address + i);
        // Serial.print(": ");
        // Serial.println(data);
    }

    // Write type and size information to memory
    if (type == STRING) {
        memory[address + size] = STRING; // Store type
        // Serial.print("Address ");
        // Serial.print(address + size);
        // Serial.print(": ");
        // Serial.println(STRING);
        memory[address + size + 1] = size; // Store length after the string
        // Serial.print("Address ");
        // Serial.print(address + size + 1);
        // Serial.print(": ");
        // Serial.println(size);
    } else {
        memory[address + size] = type; // Store type for other data types
        // Serial.print("Address ");
        // Serial.print(address + size);
        // Serial.print(": ");
        // Serial.println(type);
    }

    Serial.print("Variable ");
    Serial.print(name);
    Serial.println(" set successfully.");
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
void getVar(const char* name, int processID) {
    Serial.print("Getting variable: ");
    Serial.print(name);
    Serial.print(", Process ID: ");
    Serial.println(processID);

    // Find the variable in the memory table
    for (int i = 0; i < noOfVars; i++) {
        if (strcmp(memoryTable[i].name, name) == 0 && memoryTable[i].processID == processID) {
            int size = memoryTable[i].size;
            int address = memoryTable[i].address;
            byte type = memoryTable[i].type;

            // Push the variable data onto the stack
            Serial.print("type = ");
            Serial.println(type);

            for (int j = 0; j < size; j++) {
                pushByte(memory[address + j]);
            }
                if (type == STRING) {
                Serial.print("Pushing length: ");
                pushByte(memory[address + size + 1]); // Push the length of the string
                Serial.println(memory[address + size + 1]);
            }

            pushByte(type); // Push the type after the data



            Serial.print("Found variable '"); 
            Serial.print(name);
            Serial.print("' at address ");
            Serial.print(address);
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
  delay(5000);
  Serial.println("char");
  pushChar('a');
  setVar('x', 0);
  getVar('x', 0);
  popByte(); // gets type
  Serial.println(popChar());

  delay(5000);
  Serial.println("int");
  pushInt(12);
  setVar('y', 1);
  getVar('y', 1);
  popByte(); // gets type
  Serial.println(popInt());

  delay(5000);
  Serial.println("float");
  pushFloat(1.234);
  setVar('z', 1);
  getVar('z', 1);
  for (int i = 0; i<= 5; i++) {
    Serial.println("hi");
  }
  popByte(); // gets type
  Serial.println(popFloat(),6);

  delay(5000);
  Serial.println("string");
  pushString("Hallo");
  setVar('s', 2);
  getVar('s', 2);
  popByte(); // gets type
  Serial.println(popString());

  delay(5000);
  Serial.println("string");
  pushString("Hallobamipangpang");
  setVar('u', 2);
  getVar('u', 2);
  popByte(); // gets type
  Serial.println(popString());

  delay(5000);
  Serial.println("string");
  pushString("Hallobamipangpang");
  setVar('u', 2);
  getVar('u', 2);
  popByte(); // gets type
  Serial.println(popString());


    // Delay for a while to observe the output
  delay(5000000); // 5 seconds delay between loop iterations
}