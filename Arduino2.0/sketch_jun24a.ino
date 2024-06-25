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

typedef struct {
    char name[BUFSIZE];
    void (*func)();
} commandType;

fileType FAT[MAX_FILES];
EERef noOfFiles = EEPROM[160];  

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
    // Serial.setTimeout(-1);
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
    Serial.println(noOfFiles);
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

        // Check if the file already exists
    if (findFile(filename) != -1) {
        Serial.println("Error: File already exists");
        return;
    }

    // Check if there is enough space in the FAT
    if (noOfFiles >= MAX_FILES) {
        Serial.println(noOfFiles);
        Serial.println("Error: Maximum number of files reached");
        return;
    }

    // Find free space for the file
    int freeSpace = findFreeSpace(fileSize);
    if (freeSpace == -1) {
        Serial.println("Error: Not enough space to store the file");
        return;
    }

    // Store file information in FAT
    int index = findEmptyFATEntry();
    if (index == -1) {
        Serial.println("Error: No empty FAT entry");
        return;
    }

    FAT[index].start = freeSpace;
    FAT[index].size = fileSize;
    strncpy(FAT[index].name, filename, FILENAMESIZE);

    // Write FAT entry to EEPROM
    writeFATEntry(index);

    // Read and write file content to EEPROM
    char content[fileSize];
    for (int i = 0; i < fileSize; i++) {
        while (!Serial.available()); // Wait for data
        content[i] = Serial.read();
        EEPROM.write(freeSpace + i, content[i]);
    }

    // Update number of files in EEPROM
    noOfFiles++;
    Serial.print(noOfFiles);
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

int findFreeSpace(int filesize) {
    if (noOfFiles == 0) {
        return 160; // Start right after the FAT
    }

    // Sort the FAT by start position
    for (int i = 0; i < MAX_FILES - 1; i++) {
        for (int j = 0; j < MAX_FILES - i - 1; j++) {
            if (FAT[j].start > FAT[j + 1].start) {
                fileType temp = FAT[j];
                FAT[j] = FAT[j + 1];
                FAT[j + 1] = temp;
            }
        }
    }

    // Check for space between files and at the end of EEPROM
    int maxFreeSpace = 160; // Start after the FAT
    for (int i = 0; i < MAX_FILES - 1; i++) {
        int spaceBetween = FAT[i + 1].start - (FAT[i].start + FAT[i].size);
        if (spaceBetween > maxFreeSpace) {
            maxFreeSpace = spaceBetween;
        }
    }

    // Check space at the end of EEPROM
    int lastFileEnd = FAT[MAX_FILES - 1].start + FAT[MAX_FILES - 1].size;
    if (EEPROM_SIZE - lastFileEnd > maxFreeSpace) {
        maxFreeSpace = EEPROM_SIZE - lastFileEnd;
    }

    return maxFreeSpace >= filesize ? maxFreeSpace : -1;
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
    int maxFreeSpace = EEPROM_SIZE - 160; // Total space minus space taken by FAT

    // Calculate space used by files
    for (int i = 0; i < MAX_FILES; i++) {
        if (FAT[i].size > 0) {
            maxFreeSpace -= FAT[i].size;
        }
    }

    Serial.print("Maximale beschikbare ruimte: ");
    Serial.println(maxFreeSpace);
}

void run() {
    // Placeholder for run command
    Serial.println("Run command not implemented yet");
}

void list() {
    // Placeholder for list command
    Serial.println("List command not implemented yet");
}
void suspend() {
    // Placeholder for suspend command
    Serial.println("Suspend command not implemented yet");
}

void resume() {
    // Placeholder for resume command
    Serial.println("Resume command not implemented yet");
}

void kill() {
    // Placeholder for kill command
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
    return -1; // File not found
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