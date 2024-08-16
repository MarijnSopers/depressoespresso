#include <EEPROM.h>

#define BUFSIZE 12
#define FILENAMESIZE 12
#define MAX_FILES 10
#define EEPROM_SIZE 1024 

typedef struct {
    char name[FILENAMESIZE];
    int start;
    int size;
} fileType;

fileType FAT[MAX_FILES];
byte noOfFiles = 0;

typedef struct {
    char name[BUFSIZE];
    void (*func)();
} commandType;

void help() {
    Serial.println(F("Here is a list with all available commands:"));
    Serial.println(F("    help                        prints a list with all commands and their syntax."));
    Serial.println(F("    store [file] [size] [data]  creates file with specified size and puts data in file."));
    Serial.println(F("    retrieve [file]             prints the contents of file."));
    Serial.println(F("    erase [file]                deletes file."));
    Serial.println(F("    files                       prints a list with all files."));
    Serial.println(F("    freespace                   prints the free space in the filesystem."));
    Serial.println(F("    run [file]                  starts the process that is defined in file."));
    Serial.println(F("    list                        prints a list with all processes."));
    Serial.println(F("    suspend [id]                pauzes the process with processId id."));
    Serial.println(F("    resume [id]                 continues the process with processId id."));
    Serial.println(F("    kill [id]                   stops the process with processId id."));
}

// Array of available commands
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
};

static int commandLength = sizeof(command) / sizeof(commandType);

void writeFAT(int index, fileType file) {
    int start = 1 + sizeof(fileType) * index;
    EEPROM.put(start, file);
}

fileType readFATEntry(int index) {
    fileType entry;
    int addr = sizeof(fileType) * index + 1;
    EEPROM.get(addr, entry);
    return entry;
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

    int fileSize = atoi(sizeBuffer); // Convert size string to integer

    if (fileSize <= 0) {
        Serial.println("Error: Invalid file size");
        return;
    }

    char content[BUFSIZE] = "";
    int contentIndex = 0;

    // Read content until the specified size is reached or until buffer is full
    while (contentIndex < fileSize) {
        if (!Serial.available()) {
            continue; // Wait for data to be available
        }
        char c = Serial.read();
        content[contentIndex++] = c;
    }

    content[fileSize] = '\0'; // Null-terminate the content

    Serial.print("Content: ");
    Serial.println(content);

    // Check if the content length is greater than the specified size
    // if (strlen(content) != fileSize) {
    //     Serial.println("Error: Content size does not match specified file size");
    //     return;
    // }
    
    // Check if there is enough space in the FAT
    if (noOfFiles >= MAX_FILES) {
        Serial.println("Error: Maximum number of files reached");
        return;
    }

    // Check if the file already exists
    if (findFile(filename) != -1) {
        Serial.println("Error: File already exists");
        return;
    }

    // Allocate a new file entry in the FAT
    fileType newFile;
    strncpy(newFile.name, filename, FILENAMESIZE - 1); // Ensure filename doesn't exceed FILENAMESIZE
    newFile.size = fileSize;
    writeFAT(noOfFiles, newFile);
    noOfFiles++;

    // Write file content to EEPROM
    int dataAddress = newFile.start;
    for (int i = 0; i < fileSize; i++) {
        EEPROM.write(dataAddress++, content[i]);
    }

    Serial.println("File stored successfully");
}


void retrieve() {
    Serial.println("Executing retrieve command");

    char filename[FILENAMESIZE] = "";
    if (!readToken(filename)) {
        Serial.println("Error: Unknown filename");
        return;
    }

    Serial.print("Filename: ");
    Serial.println(filename);

    int fileIndex = findFile(filename);
    if (fileIndex == -1) {
        Serial.println("Error: File not found");
        return;
    }

    fileType file = readFATEntry(fileIndex);
    Serial.print("File size: ");
    Serial.println(file.size);

    Serial.print("Content: ");
    for (int i = 0; i < file.size; i++) {
        char c = EEPROM.read(file.start + i);
        Serial.print(c);
    }
    Serial.println();
}


void erase() {
    Serial.println("Executing erase command");

    char filename[FILENAMESIZE] = "";
    if (!readToken(filename)) {
        Serial.println("Error: Unknown filename");
        return;
    }    
    Serial.print("Filename: ");
    Serial.println(filename);

    // Find the index of the file in the FAT
    int fileIndex = findFile(filename);

    // Check if the file exists
    if (fileIndex == -1) {
        Serial.println("Error: File not found");
        return;
    }

    // Clear the content of the file from EEPROM
    int startAddress = FAT[fileIndex].start;
    int endAddress = startAddress + FAT[fileIndex].size;
    for (int i = startAddress; i < endAddress; ++i) {
        EEPROM.write(i, 0); // Clear each byte of the content
    }

    // Shift the remaining files in the FAT to fill the gap
    for (int i = fileIndex; i < noOfFiles - 1; ++i) {
        FAT[i] = FAT[i + 1];
        // Update start address of the shifted file
        FAT[i].start -= FAT[fileIndex].size;
    }

    // Decrement the file count
    noOfFiles--;

    Serial.println("File erased successfully");
}




// Find a file in the FAT by name
int findFile(const char* filename) {
    for (int i = 0; i < noOfFiles; ++i) {
        fileType file = readFATEntry(i);
        if (strcmp(filename, file.name) == 0) {
            return i; // Return index if found
        }
    }
    return -1; // Return -1 if not found
}

// Find space in the EEPROM
int findSpace(int size) {
    // Sort FAT by start position
    qsort(FAT, noOfFiles, sizeof(fileType), [](const void* a, const void* b) {
        return ((fileType*)a)->start - ((fileType*)b)->start;
    });

    int prevEnd = 0;
    for (int i = 0; i < noOfFiles; i++) {
        int gapStart = prevEnd;
        int gapEnd = FAT[i].start;
        if ((gapEnd - gapStart) >= size) {
            return gapStart;
        }
        prevEnd = FAT[i].start + FAT[i].size;
    }

    if ((EEPROM_SIZE - prevEnd) >= size) {
        return prevEnd;
    }

    return -1; // No space found
}

void files() {
    Serial.println(F("This is a list with all files:"));
    Serial.println(F("    name                        size"));
    for (int FATEntryId = 0; FATEntryId < noOfFiles; FATEntryId++) {
        Serial.print(F("    "));
        Serial.print(readFATEntry(FATEntryId).name);
        Serial.print(F("                        "));
        Serial.println(readFATEntry(FATEntryId).size);
    }
    Serial.print(noOfFiles);
    Serial.println(F(" result(s)"));
}

void freespace() {
    // Free space calculation
}

void run() {
    Serial.println("Executing run command");
}

void list() {
    Serial.println("List of files:");
    for (int i = 0; i < noOfFiles; ++i) {
        fileType file = readFATEntry(i);
        Serial.print("Name: ");
        Serial.print(file.name);
        Serial.print("\tSize: ");
        Serial.println(file.size);
    }
}

void suspend() {
    Serial.println("Executing suspend command");
}

void resume() {
    Serial.println("Executing resume command");
}

void kill() {
    Serial.println("Executing kill command");
}

void unknownCommand() {
    Serial.println("Unknown command. Available commands:");
    for (int i = 0; i < commandLength; i++) {
        Serial.println(command[i].name);
    }
}

bool readToken(char* buffer) {
    static int pos = 0;
    bool tokenRead = false;
    
    while (Serial.available() > 0) {
        char c = Serial.read();
        if (c == ' ' || c == '\n' || c == '\r') {
            if (pos > 0) {
                buffer[pos] = '\0';
                pos = 0;
                tokenRead = true;
                break;
            }
        } else {
            buffer[pos++] = c;
            if (pos >= BUFSIZE - 1) {
                buffer[pos] = '\0';
                pos = 0;
                tokenRead = true;
                break;
            }
        }
    }
    delay(10);
    
    return tokenRead;
}

void clearSerialBuffer() {
    delayMicroseconds(1042);
    while (Serial.available()) {
        Serial.read();
        delayMicroseconds(1042);
    }
}

void setup() {
    Serial.begin(9600);
    Serial.println("ArduinOS 1.0 ready");
}

void loop() {
    static char buffer[BUFSIZE];

    if (readToken(buffer)) {
        char *commandToken = strtok(buffer, " ");
        if (commandToken != NULL) {
            for (int i = 0; i < commandLength; i++) {
                if (strcmp(commandToken, command[i].name) == 0) {
                    command[i].func();
                    return;
                }
            }
            help();
        }
    }
}
