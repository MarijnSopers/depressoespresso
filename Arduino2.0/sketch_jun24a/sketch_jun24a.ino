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
int stackPointer = 0;  //zet terug naar 0

Process processes[MAXPROCESSES];
int noOfProcesses = 0;


char inputBuffer[BUFSIZE];
int bufferIndex = 0;


void setup() {
  Serial.begin(9600);
  ////Serial.println((noOfFiles);
  Serial.println(F("ArduinOS 1.0 ready"));
  // initProcessTable();
  initializeFAT();
}

void initializeFAT() {
    for (int i = 0; i < MAX_FILES; i++) {
        EEPROM.get(i * sizeof(fileType), FAT[i]);
    }
    noOfFiles = EEPROM.read(160);
}

void initProcessTable() {
  for (int i = 0; i < MAXPROCESSES; i++) {
    processes[i].state = '0';
  }
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
  return -1;  // No free space
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

void store() {
    // Clear buffer
    inputBuffer[0] = 0;
    Serial.println(noOfFiles);

  
    if (noOfFiles >= MAX_FILES) {
        Serial.println(F("ERROR: Too many files! Max 10."));
        return;  // Exit if the file limit is reached
    }

    fileType file;
    // Get file.name
    Serial.println(F("Enter filename:"));
              // get file.name
        while (!readToken(inputBuffer)) {
            strcpy(file.name, inputBuffer);
        }

        if (findFileInFAT(inputBuffer) != -1) {
          Serial.println(F("file already stored"));
          return;
        }
                // Check if the filename is empty
        if (strlen(inputBuffer) == 0) {
            Serial.println(F("ERROR: Filename cannot be empty."));
            return;
        }
        Serial.println("name:");
        Serial.println(file.name);

    // Clear buffer
    inputBuffer[0] = 0;

    // Get file.size
    Serial.println(F("Enter file size:"));
    while (!readToken(inputBuffer)) {
      file.size = atoi(inputBuffer);
    }
    if (file.size <= 0) {
          Serial.println(F("invalid size"));
          return;
        }
    Serial.println(file.size);

    // Calculate empty space to store
    int freeSpace = findFreeSpace(file.size);
    if (freeSpace == -1) {
        Serial.println(F("ERROR: Not enough space to store file!"));
        return;  // Exit if there is not enough space
    }

    int index = findEmptyFATEntry();
    if (index == -1) {
        Serial.println(F("ERROR: No empty FAT entry"));
        return;  // Exit if no empty FAT entry is available
    }

    // Set up the FAT entry for the file
    FAT[index].start = freeSpace;
    FAT[index].size = file.size;
    strncpy(FAT[index].name, file.name, FILENAMESIZE);

    writeFATEntry(index);

    inputBuffer[0] = 0;
    // Write content to EEPROM
    Serial.println(F("Enter file content:"));
    char content[file.size];
    int bytesRead = 0;


   // Read content from Serial until the specified file size is reached
    while (bytesRead < file.size) {
        if (Serial.available()) {
            // Read available data into the input buffer
            int available = Serial.available();
            int toRead = min(available, file.size - bytesRead);
            
            // Read data into the content buffer
            for (int i = 0; i < toRead; i++) {
                content[bytesRead++] = Serial.read();
            }
            delay(10);  // Small delay to handle buffer processing
        }
    }

    // Write content to EEPROM
    for (int byteId = 0; byteId < file.size; byteId++) {
        EEPROM.write(freeSpace + byteId, content[byteId]);
    }

    // Update the number of files
    noOfFiles++;
    EEPROM.write(160, noOfFiles);

    // Print success message
    Serial.print(F("INFO: File "));
    Serial.print(file.name);
    Serial.println(F(" has been stored successfully."));
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

void retrieve() {
  Serial.println(F("Executing retrieve command"));
  inputBuffer[0] = 0;    
  char* filename;

  while (!readToken(inputBuffer)) {
    // Serial.println(F("Error: Unknown filename"));
    // return;
    filename = inputBuffer;
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
  inputBuffer[0] = 0;    
  char* filename;

  while (!readToken(inputBuffer)) {
    // Serial.println(F("Error: Unknown filename"));
    // return;
    filename = inputBuffer;
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
  Serial.print(F("current spaces in use: "));
  Serial.println(noOfFiles);
}

void freespace() {
  int maxFreeSpace = EEPROM_SIZE - 161;

  for (int i = 0; i < MAX_FILES; i++) {
    if (FAT[i].size > 0) {
      maxFreeSpace -= FAT[i].size;
    }
  }

  Serial.print(F("Max available space: "));
  Serial.println(maxFreeSpace);
}

void run() {
  Serial.println(F("Executing run command"));
  inputBuffer[0] = 0;    
  char* filename;
  while (!readToken(inputBuffer)) {
    // Serial.println(F("Error: Unknown filename"));
    // return;
    filename = inputBuffer;
  }
  if (noOfProcesses >= MAXPROCESSES) {
    Serial.println(F("Error: Process table is full."));
    return;
  }
  int index = findFileInFAT(filename);
  if (index == -1) {
    Serial.println(F("Error: File not found"));
    return;
  }

  // Find the first available slot
  int processId = -1;
  for (int i = 0; i < MAXPROCESSES; i++) {
    Serial.println(processes[i].state);
    if (processes[i].state == '0') {  // Check if the process slot is available
      processId = i;
      break;
    }
  }

  if (processId == -1) {
    processId = noOfProcesses;
  }

  fileType fileEntry = FAT[index];

  strcpy(processes[processId].name, filename);
  processes[processId].processId = processId;
  processes[processId].state = 'r';           // RUNNING state constant
  processes[processId].pc = fileEntry.start;  // Set Program Counter to the file start address
  processes[processId].fp = 0;                // Frame Pointer initially set to 0
  processes[processId].sp = 0;                // Stack Pointer initially set to 0
  processes[processId].loop_start = -1;       // Loop start not set

  // Only increment process count if a new slot is used
  if (processId == noOfProcesses) {
    noOfProcesses++;
  }

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
    Serial.println(F(""));

    // Serial.print(F(" PC: "));
    // Serial.print(processes[i].pc);
    // Serial.print(F(" FP: "));
    // Serial.print(processes[i].fp);
    // Serial.print(F(" SP: "));
    // Serial.println(processes[i].sp);
  }
}


void suspend() {
  inputBuffer[0] = 0;    
  char* processid;
  while (!readToken(inputBuffer)) {
    // Serial.println(F("Error: Unknown filename"));
    // return;
    processid = inputBuffer;
  }

  int processID = atoi(processid);
  Serial.print(F("process id:"));
  Serial.println(processID);
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
  inputBuffer[0] = 0;    
  char* processid;
  while (!readToken(inputBuffer)) {
    // Serial.println(F("Error: Unknown filename"));
    // return;
    processid = inputBuffer;
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
    inputBuffer[0] = 0;
    char* processid;

    // Read the process ID token from the input buffer
    while (!readToken(inputBuffer)) {
        processid = inputBuffer;
    }

    int processID = atoi(processid);
    int processIndex = -1;

    // Search for the process with the given process ID
    for (int i = 0; i < noOfProcesses; i++) {
        if (processes[i].processId == processID && processes[i].state != '0') {
            processIndex = i;
            break;
        }
    }

    // If the process wasn't found, print an error message
    if (processIndex == -1) {
        Serial.println(F("Error: Process could not be found."));
        return;
    }

    // Delete any variables associated with this process
    deleteVariables(processID);

    // Shift the processes array to remove the terminated process
    for (int i = processIndex; i < noOfProcesses - 1; i++) {
        processes[i] = processes[i + 1];
        processes[i].processId = i;
    }

    noOfProcesses--;
    processes[noOfProcesses].state = '0';

    // Output success message
    Serial.print(F("Process "));
    Serial.print(processID);
    Serial.println(F(" terminated successfully."));
}



// void show() {
//   // Serial.println(F("Show command");
//   // Serial.println((noOfFiles);
// }

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



static commandType command[] = {
   //{ "help", &help },
   { "store", &store },
   { "retrieve", &retrieve },
   { "erase", &erase },
   { "files", &files },
   { "freespace", &freespace },
   { "run", &run },
   { "list", &list },
   { "suspend", &suspend },
   { "resume", &resume },
   //{ "kill", &kill },
  { "deleteall", &deleteAllFiles },
  //{ "show", &show },
};
static int nCommands = sizeof(command) / sizeof(commandType);

void help() {
  Serial.println(F("Available commands:"));
  for (int i = 0; i < nCommands; i++) {
    Serial.println(command[i].name);
  }
}


// void processCommand(char* input) {
//   for (int i = 0; i < nCommands; i++) {
//     if (strcmp(input, command[i].name) == 0) {
//       command[i].func();
//       return;
//     }
//   }
//   Serial.println(F("Unknown command. Type 'help' for a list of commands."));
// }


void pushByte(Process &p, byte b) {
    if (p.sp >= STACKSIZE) {
        Serial.println(F("Error: Stack overflow"));
        return;
    }
    p.stack[p.sp++] = b;
}

byte popByte(Process &p) {
    if (p.sp <= 0) {
        Serial.println(F("Error: Stack underflow"));
        return 0;
    }
    return p.stack[--p.sp];
}

void pushChar(Process &p, char c) {
    pushByte(p, c);
    pushByte(p, CHAR);
}

char popChar(Process &p) {
    return popByte(p);
}

void pushInt(Process &p, int i) {
    pushByte(p, highByte(i));
    pushByte(p, lowByte(i));
    pushByte(p, INT);
}

int popInt(Process &p) {
    if (p.sp < 2) {
        Serial.println(F("Error: Not enough data on stack"));
        return 0;
    }
    byte lowByte = popByte(p);
    byte highByte = popByte(p);
    int value = word(lowByte, highByte);
    return value;
}

void pushFloat(Process &p, float f) {
    byte* b = (byte*)&f;
    for (int i = sizeof(float) - 1; i >= 0; i--) {
        pushByte(p, b[i]);
    }
    pushByte(p, FLOAT);   
}

float popFloat(Process &p) {
    byte b[sizeof(float)];
    for (int i = sizeof(float) - 1; i >= 0; i--) {
        b[i] = popByte(p);
    }
    float* f = (float*)b;
    return *f;
}

void pushString(Process &p, const char* string) {
    int length = strlen(string);
    for (int i = 0; i < length; i++) {
        pushByte(p, string[i]);
    }
    pushByte(p, '\0');           
    pushByte(p, length + 1);     
    pushByte(p, STRING);          
}

char* popString(Process &p) {
    int length = popByte(p);
    if (length <= 0) {
        return NULL;  
    }

    char* result = (char*)malloc((length + 1) * sizeof(char));
    if (result == NULL) {
        return NULL;  // Memory allocation failed
    }

    for (int i = 0; i < length; i++) {
        result[i] = popByte(p);
    }
    result[length] = '\0';  // Null-terminate the string

    return result;
}

            
void setVar(const char* name, int processID) {
    // Check if processID is valid
    if (processID < 0 || processID >= MAXPROCESSES) {
        Serial.println(F("Error: Invalid process ID"));
        return;
    }

    // Retrieve the process based on processID
    Process* processPointer = &processes[processID]; 

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

    // Check stack pointer
    if (processPointer->sp < 1) {
        Serial.println(F("Error: Not enough data on stack"));
        return;
    }

    byte type = popByte(*processPointer);
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
            size = popByte(*processPointer);
            break;
        default:
            Serial.println(F("Error: Unknown variable type"));
            return;
    }

    // Add the new variable to memoryTable
    if (noOfVars < MAX_VARS) {
        strncpy(memoryTable[noOfVars].name, name, sizeof(memoryTable[noOfVars].name) - 1);
        memoryTable[noOfVars].name[sizeof(memoryTable[noOfVars].name) - 1] = '\0'; // Ensure null termination
        memoryTable[noOfVars].processID = processID;
        memoryTable[noOfVars].size = size;
        memoryTable[noOfVars].type = type;
        noOfVars++;
    } else {
        Serial.println(F("Error: No space in memory table"));
    }
}

void getVar(const char* name, int processID) {
    // Validate the process ID
    if (processID < 0 || processID >= MAXPROCESSES) {
        Serial.println(F("Error: Invalid process ID"));
        return;
    }

    // Retrieve the process based on processID
    Process* processPointer = &processes[processID]; 

    // Search the memoryTable for the variable
    for (int i = 0; i < noOfVars; i++) {
        if (strcmp(memoryTable[i].name, name) == 0 && memoryTable[i].processID == processID) {
            // Variable found; determine its type and retrieve its value
            int size = memoryTable[i].size;
            byte type = memoryTable[i].type;

            // Serial.print(F("Variable '"));
            // Serial.print(name);
            // Serial.print(F("' Value: "));

            switch (type) {
                case CHAR:
                    if (size == sizeof(char)) {
                        //Serial.println(popChar(*processPointer));
                    }
                    break;
                case INT:
                    if (size == sizeof(int)) {
                        //Serial.println(popInt(*processPointer));
                    }
                    break;
                case FLOAT:
                    if (size == sizeof(float)) {
                        //Serial.println(popFloat(*processPointer), 6); // Print float with 6 decimal places
                    }
                    break;
                case STRING:
                    // Pop the length of the string
                    if (size > 0) {
                        char* str = popString(*processPointer);
                        if (str != NULL) {
                            // Serial.print(str);
                            // free(str); // Free the allocated memory
                        } else {
                            Serial.println(F("Error: Failed to allocate memory for string"));
                        }
                    }
                    break;
                default:
                    Serial.println(F("Error: Unknown variable type"));
                    return;
            }
            return;
        }
    }

    // Variable not found
    Serial.println(F("Error: Variable not found"));
}


void runProcesses() {
  for (int i = 0; i < noOfProcesses; i++) {
    if (processes[i].state == 'r') {  // Check if the process is RUNNING
      execute(i);                     // Execute the next instruction for this process
      // delay(200);                     // Delay to allow other operations and avoid overwhelming the CPU
    }
  }
}

void execute(int processIndex) {
    Process& p = processes[processIndex];  // Direct reference to the Process struct
    fileType* fileEntry = nullptr;
    for (int i = 0; i < noOfFiles; i++) {
        if (strcmp(FAT[i].name, p.name) == 0) {
            fileEntry = &FAT[i];
            break;
        }
    }

    // // If no corresponding file entry is found, print an error and exit
    // if (fileEntry == nullptr) {
    //     Serial.println(F("Error: Corresponding file entry not found."));
    //     return;
    // }

    int endAddress = fileEntry->start + fileEntry->size;

    // Debugging: Print the current PC and file details
    // Serial.print(F("Process ID: "));
    // Serial.println(p.processId);
    // Serial.print(F("File Start Address: "));
    // Serial.println(fileEntry->start);
    // Serial.print(F("File Size: "));
    // Serial.println(fileEntry->size);
    // Serial.print(F("File End Address: "));
    // Serial.println(endAddress);
    // Serial.print(F("Current PC: "));
    // Serial.println(p.pc);

    // Check if the PC is within the bounds of the file
    if (p.pc >= endAddress) {
        Serial.println(F("End of file reached. No more instructions to execute."));
        p.state = 't';  // Mark the process as terminated
        return;
    }

    // Read the instruction from EEPROM and execute it
    byte instruction = EEPROM.read(p.pc);
    p.pc += 1;  // Increment the Program Counter

    // Debugging: Print the instruction read
    Serial.print(F("Executing Instruction Code: "));
    Serial.println(instruction);


    switch (instruction) {
          case CHAR: {
            char value = (char)EEPROM.read(p.pc++);
            pushChar(p, value);
            break;
        }
        case INT: {
            int value = EEPROM.read(p.pc) | (EEPROM.read(p.pc + 1) << 8);
            p.pc += 2;
            pushInt(p, value);
            break;
        }
        case FLOAT: {
            float value;
            byte* bytePointer = (byte*)&value;
            for (int i = 0; i < sizeof(float); i++) {
                bytePointer[i] = EEPROM.read(p.pc++);
            }
            pushFloat(p, value);
            break;
        }
        case STRING: {
            char ch;
            int length = 0;
            while ((ch = EEPROM.read(p.pc++)) != '\0') {
                pushByte(p, ch);
                length++;
            }
            pushByte(p, '\0');  // Null-terminate the string
            pushByte(p, length + 1);  // Store the length of the string (including the null character)
            pushByte(p, STRING);
            break;
        }
        case PRINT: {
            byte type = popByte(p);
            switch (type) {
                case CHAR:
                    Serial.print((char)popChar(p));
                    break;
                case INT:
                    Serial.print(popInt(p));
                    break;
                case FLOAT:
                    Serial.print(popFloat(p), 6);  // Print float with 6 decimal places
                    break;
                case STRING: {
                    char* str = popString(p);
                    if (str != NULL) {
                        Serial.print(str);
                        free(str);
                    }
                    break;
                }
                default:
                    Serial.println(F("Error: Unknown type in PRINT"));
                    break;
            }
            break;
        }
        case PRINTLN: {
            execute(processIndex);  // Reuse PRINT logic
            Serial.println();
            break;
        }
        case SET: {
            byte varName = EEPROM.read(p.pc++);
            setVar((char*)&varName, processIndex);
            break;
        }
        case GET: {
            byte varName = EEPROM.read(p.pc++);
            getVar((char*)&varName, processIndex);
            break;
        }
        // Add more cases for arithmetic, logic, control, and other operations
        default: {
            Serial.println(F("Error: Unknown instruction"));
            break;
        }
    }
 }


bool readToken(char Buffer[]) {
  int i = strlen(Buffer);
  char c;
  while (Serial.available()) {
    c = Serial.read();
    if ((c == ' ') || c == '\r' || c == '\n') {
      Buffer[i] = '\0'; 
      return true;
    } 
    Buffer[i] = c;
    i++;
  }
  Buffer[i + 1] = '\0';
  return false;
}

void clearSerialBuffer() {
    delayMicroseconds(1042);
    while (Serial.available()) {
    Serial.read();
    delayMicroseconds(1042);
    }
}



void loop() {
    if (readToken(inputBuffer)) {
        bool oneCalled = false;
        for (int commandId = 0; commandId < nCommands; commandId++) {
            if (!strcmp(inputBuffer, command[commandId].name)) {
                void (*func) (char inputBuffer[]) = command[commandId].func;
                func(inputBuffer);
                oneCalled = true;
            }
        }

        if (!oneCalled) {
            Serial.println(F("ERROR: command not known"));
            Serial.print(inputBuffer);
            Serial.println(F("Enter 'help' for help."));
        }
    
        Serial.print(F("> "));
        clearSerialBuffer();
        inputBuffer[0] = 0;
    }
    runProcesses();
}
