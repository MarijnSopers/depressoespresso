/*
• kan commando’s inlezen vanaf de command line via de seriele terminal. ¨
• kan programma’s in de voorgeschreven bytecode (opgeslagen als bestanden in het bestandssysteem)
uitvoeren op de Arduino Uno of Nano.
• beheert een geheugen van tenminste 256 bytes.
• kan tenminste 25 variabelen van het type CHAR (1 byte), INT (2 bytes), FLOAT (4 bytes) of STRING (zeroterminated, variabel aantal bytes) in het geheugen houden, waarvan de waarde gezet, gelezen en gemuteerd kan worden.
• beheert een bestandssysteem ter grootte van het beschikbare EEPROM-geheugen.
• kan hierin tenminste 10 bestanden opslaan, teruglezen en wissen met bestandsnamen van maximaal 12
tekens (inclusief terminating zero).
• kan de nog beschikbare hoeveelheid opslagruimte weergeven.
• kan tot 10 verschillende processen bijhouden die gestart, gepauzeerd, hervat en beeindigd kunnen wor- ¨
den.
• houdt bij van alle variabelen bij welk proces ze horen, en geeft het geheugen dat de variabelen innemen
vrij als het proces stopt.
• kan per proces 1 bestand tegelijk lezen of schrijven.
• houdt per proces een stack bij van tenminste 32 bytes
*/

#include <EEPROM.h>

#define BUFSIZE 12
#define FILENAMESIZE 12
#define MAX_FILES 10

typedef struct {
    char name[BUFSIZE];
    void (*func)();
} commandType;

typedef struct {
    char name[FILENAMESIZE];
    int start;
    int size;
} fileType;

fileType FAT[MAX_FILES];

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

//array of available table command
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


void store() {
    Serial.println("Executing store command");

    char filename[BUFSIZE] = "";
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

    // Now you have filename and fileSize, you can proceed to store the file
    // Implement the file storage logic here
}

void retrieve(char* filename){
}

void erase(const char* filename) {
}

int findFile(const char* filename) {
}

void files() {
    Serial.println("List of files:");
}

void freespace() {
}

void run (){
    Serial.println("Executing run command");

}

void list (){
    Serial.println("Executing list command");

}

void suspend (){
    Serial.println("Executing suspend command");

}

void resume (){
    Serial.println("Executing resume command");

}

void kill (){
    Serial.println("Executing kill command");

}

void unknownCommand() {
    Serial.println("Unknown command. Available commands:");
    for (int i = 0; i < commandLength; i++) {
        Serial.println(command[i].name);
    }
}

void processCommand(char* buffer) {
    for (int i = 0; i < sizeof(command) / sizeof(commandType); ++i) {
        if (strcmp(buffer, command[i].name) == 0) {
            command[i].func();
            return;
        }
    }
    Serial.println("Unknown command. Available commands:");
    for (int i = 0; i < sizeof(command) / sizeof(commandType); ++i) {
        Serial.println(command[i].name);
    }
}

bool readToken(char* buffer) {
    static int index = 0;
    static bool commandMode = true; // Flag to indicate if we're reading a command
    bool tokenComplete = false;
    
    while (Serial.available() && !tokenComplete) {
        char c = Serial.read();
        
        // Stop reading if we encounter whitespace or newline
        if (c == ' ' || c == '\n' || c == '\r') {
            if (index == 0) {
                // Skip leading whitespace
                continue;
            } else {
                buffer[index] = '\0';
                index = 0;
                if (commandMode) {
                    commandMode = false; // Switch to argument mode after reading the command
                } else {
                    tokenComplete = true;
                }
            }
        } else {
            buffer[index++] = c;
            if (index >= BUFSIZE - 1) {
                buffer[index] = '\0';
                index = 0;
                tokenComplete = true;
            }
        }
    }
    
    return tokenComplete;
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
        processCommand(buffer);
    }
}

