#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdbool.h>
#include <time.h>

#define PORT 8000
#define BUFFER_SIZE 1024
#define SIZE 26

typedef struct Quote
{
    char* phrase;
    char* author;
    struct Quote* next;
} Quote;

typedef struct
{
    int sock;
    char *base_path;
} handler_params;

char encryptionKey[SIZE];
char playerKey[SIZE];
char* encryptedString = NULL;
char globalVariable[512];
char userInput[256];
Quote* head = NULL;
int quoteCount = 0;
time_t startTime, endTime;

bool updateState(char* input);
void shuffle(char* array, int n);
void initialization();
void tearDown();
void loadPuzzles();
void freeQuotes();
Quote* createQuote();
char* getPuzzle();
bool isGameOver();
void handleGame(int sock, char *path);
void send_response(int sock, char *header, char *content, char *mime_type);
void not_found(int sock);
void *request_handler(void *params);
int start_server(char *base_path);

int main(int argc, char *argv[]) // Main function
{
    if (argc < 2)
    {
        printf("Usage: %s <path>\n", argv[0]);
        return 1;
    }
    loadPuzzles();
    start_server(argv[1]);
    return 0;
}

void shuffle(char* array, int n) // Fisher-Yates shuffle algorithm
{
    for (int i = n - 1; i > 0; i--) // Loop through array starting from last element to second
    {
        int j = rand() % (i + 1);
        char temp = array[i];
        array[i] = array[j];
        array[j] = temp;
    }
}

void initialization() // Initialize the encryption settings
{
    strcpy(globalVariable, getPuzzle());
    srand(time(NULL));

    for (int i = 0; i < SIZE; i++) // Initialize encryptionKey array with consecutive letters from A based on the size defined by SIZE
    {
        encryptionKey[i] = 'A' + i;
    }
    shuffle(encryptionKey, SIZE); // Randomly shuffles encryptionKey array to create mapping for encryption
    memset(playerKey, '\0', sizeof(playerKey)); // Make sure playerKey array starts empty
    encryptedString = malloc(strlen(globalVariable) + 1); // Allocates memory for encryptedString

    for (size_t i = 0; i < strlen(globalVariable); i++) // Loop through each character in globalVariable
    {
        if (isalpha(globalVariable[i])) // Check if character is alphabetical
        {
            char upper = toupper(globalVariable[i]); // Convert character to UPPERCASE
            encryptedString[i] = encryptionKey[upper - 'A'];
        }
        else // Non alphabetical characters are copied as is
        {
            encryptedString[i] = globalVariable[i];
        }
    }
    encryptedString[strlen(globalVariable)] = '\0'; // Add null terminator to end of encrypted string
    time(&startTime);
}

char* getPuzzle()
{
    if (quoteCount == 0) return "No puzzles loaded.";
    int randomIndex = rand() % quoteCount;
    Quote* current = head;

    for (int i = 0; i < randomIndex; i++)
    {
        current = current->next;
    }
    return current->phrase;
}

void handleGame(int sock, char *path) // Handle game logic
{
    if (strstr(path, "?move=") != NULL)
    {
        char *move = strstr(path, "=") + 1;

        if (strlen(move) == 2)
        {
            updateState(move);
        }
    }
    else // If no move is provided
    {
        initialization();
    }

    if (!isGameOver())
    {
        char decryptedString[512] = {0};
        for (size_t i = 0; i < strlen(encryptedString); i++)
        {
            if (isalpha(encryptedString[i]))
            {
                char decrypted = playerKey[encryptedString[i] - 'A'];
                decryptedString[i] = decrypted ? decrypted : '_';
            }
            else
            {
                decryptedString[i] = encryptedString[i];
            }
        }

        // HTML content showing encrypted and decrypted strings and form of user input
        char htmlOutput[1024];
        sprintf(htmlOutput, "<html><body>Encrypted: %s<br/><p>Decrypted: %s</p><form action=\"crypt\" method=\"GET\"><input type=\"text\" name=\"move\" autofocus maxlength=\"2\"></form></body></html>", encryptedString, decryptedString);
        send_response(sock, "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\n\r\n", htmlOutput, "text/html"); // HTML content is sent to the client
    }
    else
    {
        char *congratsPage = "<html><body>Congratulations! You solved it! <a href=\"crypt\">Start another?</a></body></html>";
        send_response(sock, "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\n\r\n", congratsPage, "text/html");
    }
}

void send_response(int sock, char *header, char *content, char *mime_type)
{
    char buffer[BUFFER_SIZE];

    // Format HTTP response with header, content-length, mime type and content of file
    sprintf(buffer, "%s\r\nContent-Length: %ld\r\nContent-Type: %s\r\n\r\n%s", header, strlen(content), mime_type, content);
    write(sock, buffer, strlen(buffer));
}

void not_found(int sock) // 404 Not Found error response for client
{
    char *response = "<html><body><h1>404 Not Found</h1></body></html>";
    send_response(sock, "HTTP/1.0 404 NOT FOUND", response, "text/html");
}

int start_server(char *base_path) // Initialize and runs a server on port 8000 to handle incoming connections
{
    int sockfd, new_sockfd; // Socket file descriptors for server and new connections
    struct sockaddr_in addr; // IPv4
    struct addrinfo hints, *res;
    pthread_t thread; // Handle each client connections

    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM; // Socket type for TCP
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, "8000", &hints, &res) != 0)
    {
        perror("getaddrinfo failed");
        return 1;
    }

    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol); // Create a socket using address information

    if (sockfd < 0)
    {
        perror("Cannot open socket");
        return 1;
    }

    if (bind(sockfd, res->ai_addr, res->ai_addrlen) < 0) // Bind socket to port from address information
    {
        perror("Cannot bind port");
        return 1;
    }

    listen(sockfd, 5); // Max of 5 pending connections

    while (1)
    {
        new_sockfd = accept(sockfd, NULL, NULL);
        if (new_sockfd < 0)
        {
            perror("Error on accept");
            continue;
        }
        handler_params *p = malloc(sizeof(handler_params));
        p->sock = new_sockfd;
        p->base_path = base_path;

        if (pthread_create(&thread, NULL, request_handler, p) != 0) // Creates a new thread to handle request
        {
            perror("Thread creation failed");
            close(new_sockfd);
        }
    }
    freeaddrinfo(res);
    close(sockfd);
    return 0;
}

void loadPuzzles()
{
    FILE* file = fopen("quotes.txt", "r"); // Hardcoded file location
    if (!file)
    {
        printf("Error opening quotes file.\n");
        return;
    }
    char line[512];
    Quote* current = createQuote(); // Initialize the head of the list
    head = current;
    quoteCount = 0;
    bool isFirstLineOfQuote = true;

    while (fgets(line, sizeof(line), file))
    {
        line[strcspn(line, "\r\n")] = 0;// Remove the newline character at the end of the line

        if (strlen(line) < 3) // Blank line indicates the end of a quote
        {
            if(current->phrase) // Only increment quote count if a quote has been added
            {
                quoteCount++;
                isFirstLineOfQuote = true;
                current->next = createQuote(); // Create a new quote and link it
                current = current->next; // Move to the new quote
            }
        }
        else if (line[0] == '-' && line[1] == '-') // Line starts with "--", indicating an author
        {
            current->author = strdup(line);
        }
        else // This is part of the phrase
        {
            if (isFirstLineOfQuote) // If it is the first line of the quote
            {
                current->phrase = strdup(line);
                isFirstLineOfQuote = false;
            }
            else // If it is a subsequent line of the quote
            {
                char* newPhrase = (char*)malloc(strlen(current->phrase) + strlen(line) + 2); // Allocate space for the new combined string
                sprintf(newPhrase, "%s\n%s", current->phrase, line); // Concatenation using sprintf
                free(current->phrase);
                current->phrase = newPhrase;
            }
        }
    }
    if (current && current->phrase && !current->author) // Handle the last quote
    {
        quoteCount++;
    }
    fclose(file);
}
void freeQuotes()
{
    Quote* current = head;
    while (current != NULL)
    {
        Quote* next = current->next;
        free(current->phrase);
        free(current->author);
        free(current);
        current = next;
    }
    head = NULL;
    quoteCount = 0;
}

Quote* createQuote()
{
    Quote* newQuote = (Quote*)malloc(sizeof(Quote)); // Allocate memory for a new quote structure
    if (newQuote)
    {
        newQuote->phrase = NULL;
        newQuote->author = NULL;
        newQuote->next = NULL; // End of the list, so initialize next pointer to NULL
    }
    return newQuote;
}

bool updateState(char* input)
{
    if (strlen(input) != 2) return false;
    char original = toupper(input[0]);
    char replacement = toupper(input[1]);
    if (isalpha(original) && isalpha(replacement))
    {
        playerKey[original - 'A'] = replacement;
        return true;
    }
    return false;
}

bool isGameOver()
{
    for (size_t i = 0; i < strlen(encryptedString); i++)
    {
        if (isalpha(encryptedString[i]))
        {
            char decrypted = playerKey[encryptedString[i] - 'A'];
            if (decrypted == '\0')
                return false;
        }
    }
    return true;
}

void *request_handler(void *params) // Handle client requests
{
    handler_params *p = (handler_params *)params;
    char buffer[BUFFER_SIZE], *request_line, *method, *path;
    int file_fd, n;

    n = read(p->sock, buffer, BUFFER_SIZE - 1);

    if (n > 0)
    {
        buffer[n] = '\0';
        request_line = strtok(buffer, "\r\n");
        method = strtok(request_line, " ");
        path = strtok(NULL, " ");

        if (strcmp(method, "GET") == 0) // Handle GET requests
        {
            if (strncmp(path, "/crypt", 6) == 0)
            {
                handleGame(p->sock, path);
            }
            else
            {
                char full_path[256];
                sprintf(full_path, "%s%s", p->base_path, path);
                file_fd = open(full_path, O_RDONLY); // Open file in read only

                if (file_fd == -1)
                {
                    not_found(p->sock); // 404 Not Found
                }
                else
                {
                    struct stat stat_buf;
                    fstat(file_fd, &stat_buf);
                    char *file_contents = malloc(stat_buf.st_size); // Allocate memory for file contents
                    read(file_fd, file_contents, stat_buf.st_size); // Read file content
                    send_response(p->sock, "HTTP/1.0 200 OK", file_contents, "text/html");
                    free(file_contents); // Free allocated memory
                    close(file_fd);
                }
            }
        }
    }
    close(p->sock);
    free(p);
}