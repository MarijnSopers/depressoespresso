//include libraries
#include <EEPROM.h>
#include <IEEE754tools.h>
#include "instruction_set.h"

//define some variables
#define BUFSIZE 12
#define FILENAMESIZE 12
#define MAX_FILES 10
#define EEPROM_SIZE 1024

#define MAXPROCESSES 5
#define STACKSIZE 32
#define MEMORYSIZE 256

#define MAX_VARS 25
//define structs
//struct for fat
typedef struct {
  char name[FILENAMESIZE];
  int start;
  int size;
} fileType;

//structs for commands
typedef struct {
  char name[BUFSIZE];
  void (*func)();
} commandType;

//struct for memory table
typedef struct {
  char name[BUFSIZE];
  int type;
  int address;
  int size;
  int processID;
} MemoryTableEntry;

//structs for the Processes
typedef struct {
  char name[FILENAMESIZE];
  int processId;
  byte state;
  int pc;
  int fp;
  int sp;
  int loop_start;
  int stack[STACKSIZE];
} Process;

//define start of where files are stored
fileType FAT[MAX_FILES];
EERef noOfFiles = EEPROM[160];

//define maximum variables
MemoryTableEntry memoryTable[MAX_VARS];
int noOfVars = 0;
byte memory[MEMORYSIZE];

byte stack[STACKSIZE];
int stackPointer = 0;  //zet terug naar 0

Process processes[MAXPROCESSES];
int noOfProcesses = 0;


char inputBuffer[BUFSIZE];
int bufferIndex = 0;

//setup
void setup() {
  Serial.begin(9600);
  Serial.println(F("ArduinOS 1.0 ready"));
  //setup fat so files can be recovered after restart of arduino
  initializeFAT();
}
//check if there are already files on the arduino
void initializeFAT() {
    for (int i = 0; i < MAX_FILES; i++) {
        EEPROM.get(i * sizeof(fileType), FAT[i]);
    }
    noOfFiles = EEPROM.read(160);
}
//checks if there is a place for the file with size and than return what space
int findFreeSpaceInMemoryTable(int size) {
    int freeAddress = 0;  
    bool foundSpace;

    do {
        foundSpace = true;
        for (int i = 0; i < noOfVars; i++) {
            int startAddress = memoryTable[i].address;
            int endAddress = startAddress + memoryTable[i].size;
            if ((freeAddress >= startAddress && freeAddress < endAddress) ||
                (freeAddress + size > startAddress && freeAddress + size <= endAddress)) {
                freeAddress = endAddress + 1;
                foundSpace = false;
                break;
            }
        }
    } while (!foundSpace);

    return freeAddress;
}

void store() {
    inputBuffer[0] = 0;
    Serial.println(noOfFiles);

    if (noOfFiles >= MAX_FILES) {
        Serial.println(F("ERROR: Too many files! Max 10."));
        return;  
    }

    fileType file;
    Serial.println(F("Enter filename:"));
        while (!readToken(inputBuffer)) {
            strcpy(file.name, inputBuffer);
        }

        if (findFileInFAT(inputBuffer) != -1) {
          Serial.println(F("file already stored"));
          return;
        }
        if (strlen(inputBuffer) == 0) {
            Serial.println(F("ERROR: Filename cannot be empty."));
            return;
        }
        Serial.println("name:");
        Serial.println(file.name);

    inputBuffer[0] = 0;

    Serial.println(F("Enter file size:"));
    while (!readToken(inputBuffer)) {
      file.size = atoi(inputBuffer);
    }
    if (file.size <= 0) {
          Serial.println(F("invalid size"));
          return;
        }
    Serial.println(file.size);

    int freeSpace = findFreeSpace(file.size);
    if (freeSpace == -1) {
        Serial.println(F("ERROR: Not enough space to store file!"));
        return;
    }

    int index = findEmptyFATEntry();
    if (index == -1) {
        Serial.println(F("ERROR: No empty FAT entry"));
        return;
    }

    FAT[index].start = freeSpace;
    FAT[index].size = file.size;
    strncpy(FAT[index].name, file.name, FILENAMESIZE);

    writeFATEntry(index);

    inputBuffer[0] = 0;
    Serial.println(F("Enter file content:"));
    char content[file.size];
    int bytesRead = 0;

    while (bytesRead < file.size) {
        if (Serial.available()) {
            int available = Serial.available();
            int toRead = min(available, file.size - bytesRead);
            
            for (int i = 0; i < toRead; i++) {
                content[bytesRead++] = Serial.read();
            }
            //delay(10);   
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

  int processId = -1;
  for (int i = 0; i < MAXPROCESSES; i++) {
    //Serial.println(processes[i].state);
    if (processes[i].state == '0') {   
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
  processes[processId].state = 'r';            
  processes[processId].pc = fileEntry.start;   
  processes[processId].fp = 0;                 
  processes[processId].sp = 0;                 
  processes[processId].loop_start = -1;        

  if (processId == noOfProcesses) {
    noOfProcesses++;
  }
}


void list() {
  if (noOfProcesses == 0) {
    Serial.println(F("No active processes."));
    return;
  }

  Serial.println(("Active processes:");
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

  if (processIndex == -1) {
    Serial.println(F("Error: Invalid process ID"));
    return;
  }

  if (processes[processIndex].state == 'p') {
    Serial.println(F("Error: Process is already suspended"));
    return;
  }

  processes[processIndex].state = 'p';
  Serial.print(F("Process suspended: "));
  Serial.println(processes[processIndex].name);
}

void resume() {
  inputBuffer[0] = 0;    
  char* processid;
  while (!readToken(inputBuffer)) {
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

  if (processes[processIndex].state == 'r') {
    Serial.println(F("Error: Process is already running"));
    return;
  }

  processes[processIndex].state = 'r';
  Serial.print(F("Process resumed: "));
  Serial.println(processes[processIndex].name);
}

void kill() {
  inputBuffer[0] = 0;    
  char* processid;
  while (!readToken(inputBuffer)) {
    processid = inputBuffer;
  }

  int processID = atoi(processid);
  killProcess(processID);
}


void killProcess(int processID) {
    int processIndex = -1;
    for (int i = 0; i < noOfProcesses; i++) {
        if (processes[i].processId == processID && processes[i].state != '0') {
            processIndex = i;
            break;
        }
    }

    if (processIndex == -1) {
        Serial.println(F("Error: Process could not be found."));
        return;
    }

    for (int i = 0; i < noOfVars; i++) {
        if (memoryTable[i].processID == processID) {
            for (int j = i; j < noOfVars - 1; j++) {
                memoryTable[j] = memoryTable[j + 1];
            }
            noOfVars--;
            i--;
        }
    }
    
    for (int i = processIndex; i < noOfProcesses - 1; i++) {
        processes[i] = processes[i + 1];
        processes[i].processId = i;
    }

    noOfProcesses--;
    processes[noOfProcesses].state = '0';

    Serial.print(F("Process "));
    Serial.print(processID);
    Serial.println(F(" terminated successfully."));
}



void show() {
  Serial.println(F("Show command"));
  Serial.println(noOfFiles);
}

void deleteAllFiles() {
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
   { "help", &help },
   { "store", &store },
   { "retrieve", &retrieve },
   { "erase", &erase },
   { "files", &files },
   { "freespace", &freespace },
   { "run", &run },
   { "list", &list },
   { "suspend", &suspend },
   { "resume", &resume },
  { "kill", &kill },
   { "deleteall", &deleteAllFiles },
  { "show", &show },
};
static int nCommands = sizeof(command) / sizeof(commandType);

void help() {
  Serial.println(F("Available commands:"));
  for (int i = 0; i < nCommands; i++) {
    Serial.println(command[i].name);
  }
}

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
    
    float result;
    byte* pFloat = reinterpret_cast<byte*>(&result);

    for (int i = 0; i < sizeof(float); i++) {
        pFloat[i] = b[sizeof(float) - 1 - i];
    }

    return result;
}


void pushString(Process &p, const char* string) {
    int length = strlen(string );
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
    char* result = (char*)malloc(length * sizeof(char));

    if (result == NULL) {
        return NULL;  
    }
    for (int i = length - 1; i >= 0; --i) {
        result[i] = popByte(p);
    }

    result[length] = '\0';   

    return result;
}

byte peekByte(Process& p) {
    if (p.sp <= 0) {
        Serial.println(F("Error: Stack underflow"));
        return 0;
    }
    return p.stack[p.sp - 1];
}


void setVar(const char* name, int processID) {
    Process* processPointer = &processes[processID]; 

    if (noOfVars >= MAX_VARS) {
        Serial.println(F("Error: No space in memory table"));
        return;
    }

    bool found = false;
    for (int i = 0; i < noOfVars; i++) {
        if (strcmp(memoryTable[i].name, name) == 0 && memoryTable[i].processID == processID) {
            for (int j = i; j < noOfVars - 1; j++) {
                memoryTable[j] = memoryTable[j + 1];
            }
            noOfVars--;
            found = true;
            break;
        }
    }

    byte type = popByte(*processPointer);
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
            size = popByte(*processPointer);
            break;
        default:
            Serial.println(F("Error: Unknown variable type"));
            return;
    }

    int address = findFreeSpaceInMemoryTable(size);

    if (noOfVars < MAX_VARS) {
        strncpy(memoryTable[noOfVars].name, name, sizeof(memoryTable[noOfVars].name) - 1);
        memoryTable[noOfVars].name[sizeof(memoryTable[noOfVars].name) - 1] = '\0';
        memoryTable[noOfVars].processID = processID;
        memoryTable[noOfVars].size = size;
        memoryTable[noOfVars].type = type;
        memoryTable[noOfVars].address = address;

        for (int i = size - 1; i >= 0; i--) {
            byte value = popByte(*processPointer);
            memory[address + i] = value;
        }
        noOfVars++;
 
    } else {
        Serial.println(F("Error: No space in memory table"));
    }
}

void getVar(const char* name, int processID) {
    int address = -1;
    if (processID < 0 || processID >= MAXPROCESSES) {
        Serial.println(F("Error: Invalid process ID"));
        return;
    }

    Process* processPointer = &processes[processID];

    bool found = false;
    for (int i = 0; i < noOfVars; i++) {
        if (strcmp(memoryTable[i].name, name) == 0 && memoryTable[i].processID == processID) {

            address = memoryTable[i].address;
            int size = memoryTable[i].size;
            byte type = memoryTable[i].type;

            for (int j = 0; j < size; j++) {
                byte value = memory[address + j];
                pushByte(*processPointer, value);
            }

            if (type == STRING) {
                pushByte(*processPointer, size);
            }

            pushByte(*processPointer, type);

            //printStack(*processPointer); //debug whats in the stack
            found = true;
            break;
        }
    }

    if (!found) {
        Serial.println(F("Error: Variable not found"));
    }
}

void runProcesses() {
  for (int i = 0; i < noOfProcesses; i++) {
    if (processes[i].state == 'r') {   
      execute(i);                      
      // delay(200);                      
    }
  }
}

void execute(int processIndex) {
    Process& p = processes[processIndex];  
    int address = p.fp;   
    int pc = p.pc;       

    byte instruction = EEPROM.read(address + pc);
    p.pc += 1;   

    switch (instruction) {
        case CHAR: {
            char value = EEPROM.read(address + p.pc);
            p.pc += 1;
            pushChar(p, value);
            break;
        }
        case INT: {
            int value = EEPROM.read(address + p.pc) | (EEPROM.read(address + p.pc + 1) << 8);
            p.pc += 2;
            pushInt(p, value);
            break;
        }
        case FLOAT: {
            float value;
            byte* bytePointer = (byte*)&value;

            for (int i = 3; i >= 0; i--) {
                bytePointer[i] = EEPROM.read(address + p.pc++);
            }
            pushFloat(p, value);
            break;
        }
        case STRING: {
            String str = "";
            char ch;
            while ((ch = EEPROM.read(address + p.pc++)) != '\0') {
                str += ch;
            }
            pushString(p, str.c_str());
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
                    Serial.print(popFloat(p));   
                    break;
                case STRING: 
                    Serial.print(popString(p));
                    break;
                default:
                    Serial.println(F("Error: Unknown type in PRINT"));
                    break;
            }
            break;
        }
        case PRINTLN: {
            byte type = popByte(p);
            switch (type) {
                case CHAR:
                    Serial.println((char)popChar(p));
                    break;
                case INT:
                    Serial.println(popInt(p));
                    break;
                case FLOAT:
                    Serial.println(popFloat(p));
                    break;
                case STRING: 
                    Serial.println(popString(p));
                    break;
                default:
                    Serial.println(F("Error: Unknown type in PRINTLN"));
                    break;
            }
            break;
        }
        case SET: {
            byte varName = EEPROM.read(address + p.pc++);
            char varNameStr[2];
            varNameStr[0] = varName;
            varNameStr[1] = '\0';
            setVar(varNameStr, processIndex);
            break;
        }
        case GET: {
            byte varName = EEPROM.read(address + p.pc++);
            getVar((char*)&varName, processIndex);
            break;
        }
            case EQUALS: {
            byte type2 = popByte(p); 
            float value2 = popValueByType(type2, p);
            
            byte type1 = popByte(p);
            float value1 = popValueByType(type1, p);
          
            // Serial.println(value2);
            // Serial.println(value1);
            
             byte result = (value1 == value2) ? 1 : 0;  
            Serial.println(result);
 
            pushInt(p, result);  
            break;
        }
        case PLUS: {
            Serial.println(F("running plus"));
            binaryOperator(PLUS, processIndex);
            break;
        }
        case MINUS:
        {
            Serial.println(F("running minus"));
            binaryOperator(MINUS, processIndex);
            break;
        }
        case INCREMENT: {
           unaireOperator(INCREMENT, processIndex);
            break;
        }
        case DECREMENT: {
           unaireOperator(DECREMENT, processIndex);
            break;
        }
        case LOOP: {
          p.loop_start = p.pc;   
          break;
        }
        case ENDLOOP: {
            p.pc = p.loop_start;   
            break;
        }
        case MILLIS: {
            unsigned long currentMillis = millis();
            pushFloat(p, currentMillis);  
            break;
        }
        case DELAYUNTIL: {
          byte type = popByte(p);
          float targetMillis = popValueByType(type, p);
          unsigned long currentMillis = millis();

          if (currentMillis < targetMillis) {
              pushFloat(p, targetMillis);  
              p.pc--;  
  
          }
          break;
        }
        case IF: {
            byte condition = popChar(p);

            if (condition == 1) {
                p.pc += popByte(p);
            } else {
                byte skipBytes = popByte(p);
                p.pc += skipBytes;
            }
            break;
        }
        case ELSE: {
            byte skipBytes = popByte(p);
            p.pc += skipBytes;
            break;
        }
        case ENDIF: {
            popChar(p);
            break;
        }

        case STOP: {   
             killProcess(p.processId);  
             break;
        }
        default: {
            Serial.println(F("Error: Unknown instruction"));
            break;
        }
    }
}

void delays(unsigned long ms) {
    unsigned long start = millis();
    while (millis() - start < ms) {
    }
}

void binaryOperator(byte operation, int processIndex) {
    Process& p = processes[processIndex];

    byte type1 = popByte(p); 
    float value1 = popValueByType(type1, p);  

    byte type2 = popByte(p); 
    float value2 = popValueByType(type2, p); 

    float result;
    switch (operation) {
        case PLUS:
            result = value2 + value1;  
            break;
        case MINUS:
            result = value2 - value1;
            break;
        default:
            Serial.println(F("Operator not recognized."));
            return;
    }

    pushFloat(p, result);  
}
float popValueByType(byte type, Process& p) {
    switch (type) {
        case CHAR:
            return (float)popChar(p);   
        case INT:
            return (float)popInt(p);    
        case FLOAT:
            return popFloat(p);        
        case STRING:
            Serial.println(F("Error: Cannot perform arithmetic on STRING type."));
            return 0.0;
        default:
            Serial.println(F("Error: Unknown type."));
            return 0.0;
    }
}

void unaireOperator(byte typeOperator, int index) {
    Process& p = processes[index]; 

    byte type = popByte(p);       
    float var;   
            
    switch (type) {
        case CHAR:
            var = (int)popChar(p);
            break;
        case INT:
            var = (int)popInt(p);
            break;
        case FLOAT:
            var = (float)popFloat(p);  
            // Serial.println(var);
            break;
        default:
            Serial.println(F("Error: Unknown type for unary operation"));
            return;
    }

    switch (typeOperator) {
        case INCREMENT:
            var += 1;
            break;
        case DECREMENT:
            var -= 1;
            break;
        default:
            Serial.println(F("Error: Unknown unary operation"));
            return;
    }

    switch (type) {
        case CHAR:
            pushChar(p, (char)var);
            break;
        case INT:
            pushByte(p, (byte)((int)var & 0xFF));        
            pushByte(p, (byte)(((int)var >> 8) & 0xFF));   
            pushByte(p, INT);    
            break;
        case FLOAT:
            pushFloat(p, (float)var);
            break;
        default:
            Serial.println(F("Error: Unknown type for pushing back onto stack"));
            return;
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

void printStack(Process &p) {
    Serial.print(F("Stack contents: "));
    for (int i = 0; i < p.sp; i++) {
        Serial.print(p.stack[i]);
        Serial.print(' ');
    }
    Serial.println();
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
