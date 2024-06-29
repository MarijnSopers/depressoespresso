#include <EEPROM.h>
#include <IEEE754tools.h>

#define BUFSIZE 12
#define FILENAMESIZE 12
#define MAX_FILES 10
#define EEPROM_SIZE 1024

#define MAXPROCESSES 10
#define STACKSIZE 32
#define MEMORYSIZE 256

#define MAX_VARS 25

#define CHAR_TYPE 0
#define INT_TYPE 1
#define FLOAT_TYPE 2
#define STRING_TYPE 3

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
