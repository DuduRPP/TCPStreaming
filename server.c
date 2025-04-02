/*
** server.c -- a stream socket server demo
*/

// Include base C libraries
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

// Include base C socket programming libraries
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

// External libraries for SQLite Database and JSON parser
#include <sqlite3.h>
#include "vendor/cJSON/cJSON.h"

#define PORT "7777"  // the port users will be connecting to
#define MAXDATASIZE 2048 // max number of bytes we can get at once 

#define BACKLOG 10   // how many pending connections queue will hold

#define MAX_GENRES 10 // Max number of genres in a single movie

// JSON Request Struct
typedef struct {
    char method[16];  // "GET"
    char resource[64]; // "Ex: /movies"
    // Only for GET
    char query[64];    // Query param for genre filtering
    // Only for POST and PUT
    char title[128];
    char genre[MAX_GENRES][64];
    int num_genres;
    char director[128];
    int release_year;
} JsonRequest;

static int callback(void *NotUsed, int argc, char **argv, char **azColName) {
   int i;
   for(i = 0; i<argc; i++) {
      printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
   }
   printf("\n");
   return 0;
}

int initialize_db(sqlite3* db)
{
    char *zErrMsg = 0;
    int rc;
    char *sql;

    /* Open database */
    rc = sqlite3_open("test.db", &db);

    if( rc ) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        return(0);
    } else {
        fprintf(stdout, "Opened database successfully\n");
        fprintf(stdout, "Initializing database...\n");
    }

    /*
    sqlite> SELECT * FROM Movie m
   ...> JOIN Movie_Genre mg on m.ID = mg.MovieID
   ...> JOIN Genre g ON mg.GenreID = g.id;
    */

    /* Create SQL statement */
    sql =
    "DROP TABLE IF EXISTS Genre;"\
    "DROP TABLE IF EXISTS Movie;"\
    "DROP TABLE IF EXISTS Movie_Genre;"\
    "CREATE TABLE Genre(" \
        "ID   INTEGER    PRIMARY KEY AUTOINCREMENT,"
        "Name TEXT                 NOT NULL);"
    "CREATE TABLE Movie("  \
        "ID INTEGER PRIMARY KEY AUTOINCREMENT," \
        "Title          TEXT    NOT NULL," \
        "Director       TEXT    NOT NULL, " \
        "ReleaseYear    INT     NOT NULL);"
    "CREATE TABLE Movie_Genre("\
        "ID      INT  PRIMARY KEY,"
        "MovieID INT,"\
        "GenreID INT,"\
        "FOREIGN KEY(MovieID) REFERENCES Movie(ID),"\
        "FOREIGN KEY(GenreID) REFERENCES Genre(ID));"\
    "INSERT INTO Genre (Name) VALUES ('Action');"\
    "INSERT INTO Genre (Name) VALUES ('Sci-fi');"\
    "INSERT INTO Genre (Name) VALUES ('Fantasy');"\
    "INSERT INTO Genre (Name) VALUES ('Suspense');"\
    "INSERT INTO Genre (Name) VALUES ('Comedy');"\
    "INSERT INTO Genre (Name) VALUES ('Drama');"\
    "INSERT INTO Genre (Name) VALUES ('Historical Drama');"\
    "INSERT INTO Genre (Name) VALUES ('Horror');";

    /* Execute SQL statement */
    rc = sqlite3_exec(db, sql, callback, 0, &zErrMsg);

    if( rc != SQLITE_OK ){
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
        sqlite3_free(zErrMsg);
    } else {
        fprintf(stdout, "Table created successfully\n");
    }
    sqlite3_close(db);
    return 0;

}

void sigchld_handler(int s)
{
    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;

    while(waitpid(-1, NULL, WNOHANG) > 0);

    errno = saved_errno;
}


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void invalid_request(int new_fd, char loc_err[]){
    char buffer[100];
    sprintf(buffer, "Bad Request: Invalid %s", loc_err);

    // create a cJSON object 
    cJSON *res = cJSON_CreateObject();
    cJSON_AddNumberToObject(res, "status", 400);
    cJSON_AddStringToObject(res, "message", buffer);
    
    // convert the cJSON object to a JSON string 
   char *res_str = cJSON_Print(res); 

    if (send(new_fd, res_str, MAXDATASIZE, 0) == -1)
        perror("send");
    return ;
}

// POST
void post_movie(JsonRequest req, sqlite3* db){
    char *zErrMsg = 0;
   int rc;
   char *sql;
   const char* data = "Callback function called";

   /* Open database */
   rc = sqlite3_open("test.db", &db);
   
   if( rc ) {
      fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
      return ;
   } else {
      fprintf(stderr, "Opened database successfully\n");
   }

   /* Create SQL statement */
   sql = "SELECT * from Genre";

   /* Execute SQL statement */
   rc = sqlite3_exec(db, sql, callback, (void*)data, &zErrMsg);
   
   if( rc != SQLITE_OK ) {
      fprintf(stderr, "SQL error: %s\n", zErrMsg);
      sqlite3_free(zErrMsg);
   } else {
      fprintf(stdout, "Operation done successfully\n");
   }
   sqlite3_close(db);

    return ;
}


void handle_request(int new_fd, sqlite3* db){
    char res_string[MAXDATASIZE];
    char req_string[MAXDATASIZE];

    memset(req_string, 0, MAXDATASIZE);
    recv(new_fd, req_string, MAXDATASIZE, 0);
    // Debug request string:
    // printf("Server received JSON:\n%s\n", req_string);

    JsonRequest req;
    memset(&req, 0, sizeof(JsonRequest));

    // Testing Parsing
    {
        cJSON *json = cJSON_Parse(req_string);
        if (json == NULL) { 
            const char *error_ptr = cJSON_GetErrorPtr(); 
            if (error_ptr != NULL) { 
                printf("Error: %s\n", error_ptr); 
            } 
            cJSON_Delete(json); 
        }

        cJSON *method = cJSON_GetObjectItemCaseSensitive(json, "method");
        cJSON *resource = cJSON_GetObjectItemCaseSensitive(json, "resource");
        cJSON *body = cJSON_GetObjectItemCaseSensitive(json, "body");
        if (cJSON_IsString(method) && (method->valuestring != NULL)) { 
            strncpy(req.method, method->valuestring, sizeof(req.method) - 1);
        } else return invalid_request(new_fd, "method");
        if (cJSON_IsString(resource) && (resource->valuestring != NULL)) { 
            strncpy(req.resource, resource->valuestring, sizeof(req.resource) - 1);
        } else return invalid_request(new_fd, "resource");
        
        // DELETE
        if(strcmp(req.method, "DELETE") == 0){

        }

        // POST & PUT
        if(strcmp(req.method,"POST") || strcmp(req.method,"PUT")){
            cJSON *title = cJSON_GetObjectItem(body, "title");
            cJSON *release_year = cJSON_GetObjectItem(body, "release_year");
            cJSON *genres = cJSON_GetObjectItem(body, "genre");

            if (cJSON_IsString(title) && (title->valuestring != NULL)) { 
                strncpy(req.title, title->valuestring, sizeof(req.title) - 1);
            } else return invalid_request(new_fd, "body.title");
            if (cJSON_IsNumber(release_year)) { 
                req.release_year = release_year->valueint;
            } else return invalid_request(new_fd, "body.release_year");

            
            // Process genres array
            if (cJSON_IsArray(genres)) {
                int count = 0;
                cJSON *genre;
                cJSON_ArrayForEach(genre, genres) {
                    if (cJSON_IsString(genre) && genre->valuestring != NULL) {
                        strncpy(req.genre[count], genre->valuestring, sizeof(req.genre[count]) - 1);
                        count++;
                        if (count >= MAX_GENRES) break;
                    }
                }
                req.num_genres = count;
                printf("Num genres: %d\n", req.num_genres);
            } else return invalid_request(new_fd, "body.genre");

            return post_movie(req, db);
        }

        // GET
        if(strcmp(req.method, "GET") == 0 && strcmp(req.resource, "movies/genre") == 0){
            cJSON *query = cJSON_GetObjectItem(body, "query");
            if (cJSON_IsString(query) && (query->valuestring != NULL)) { 
                strncpy(req.query, query->valuestring, sizeof(req.query) - 1);
            } else return invalid_request(new_fd, "body.query");
        }

        
      
        cJSON_Delete(json);
    }
    

    if (send(new_fd, "Hello, world!", 13, 0) == -1)
        perror("send");
}

int main(void)
{
    int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    struct sigaction sa;
    int yes=1;
    char s[INET6_ADDRSTRLEN];
    int rv;

    sqlite3* db;

    initialize_db(db);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo); // all done with this structure

    if (p == NULL)  {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    printf("server: waiting for connections...\n");

    while(1) {  // main accept() loop
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(their_addr.ss_family,
            get_in_addr((struct sockaddr *)&their_addr),
            s, sizeof s);
        printf("server: got connection from %s\n", s);

        if (!fork()) { // this is the child process
            close(sockfd); // child doesn't need the listener
            handle_request(new_fd, db);
            close(new_fd);
            exit(0);
        }
        close(new_fd);  // parent doesn't need this
    }

    return 0;
}
