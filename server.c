/*
** server.c -- a stream socket server demo
*/

// Include base C libraries
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>

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

static int callback(void *response_ptr, int argc, char **argv, char **azColName) {
   int i;
   cJSON *res = (cJSON*) response_ptr;
   cJSON *movies_array = cJSON_GetObjectItem(res, "movies");

    // Create movies array for response
    if(!movies_array){
        movies_array = cJSON_CreateArray();
        cJSON_AddItemToObject(res, "movies", movies_array);
    }

    // Create a new movie object
    cJSON *movie_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(movie_obj, "id", atoi(argv[0]));
    cJSON_AddStringToObject(movie_obj, "title", argv[1]);

    if(argv[2] != NULL && argv[3] != NULL){
        cJSON_AddStringToObject(movie_obj, "director", argv[2]);
        cJSON_AddNumberToObject(movie_obj, "release_year", atoi(argv[3]));
    }
    

    // Create a Genres array
    if (argv[4] != NULL) {
        cJSON *genres_array = cJSON_CreateArray();
        char *genre_token = strtok(argv[4], ", ");
        while (genre_token != NULL) {
            cJSON_AddItemToArray(genres_array, cJSON_CreateString(genre_token));
            genre_token = strtok(NULL, ",");
        }
        cJSON_AddItemToObject(movie_obj, "genre", genres_array);
    }

    cJSON_AddItemToArray(movies_array, movie_obj);
    
//    Used for debugging SELECT operations
//    for(i = 0; i<argc; i++) {
//       printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
//    }
//   printf("\n");


   return 0;
}

// Initialize Database
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
        "Name TEXT                 NOT NULL UNIQUE);"
    "CREATE TABLE Movie("  \
        "ID INTEGER PRIMARY KEY AUTOINCREMENT," \
        "Title          TEXT    NOT NULL UNIQUE," \
        "Director       TEXT    NOT NULL, " \
        "ReleaseYear    INT     NOT NULL);"
    "CREATE TABLE Movie_Genre("\
        "ID      INT  PRIMARY KEY,"
        "MovieID INT,"\
        "GenreID INT,"\
        "FOREIGN KEY(MovieID) REFERENCES Movie(ID),"\
        "FOREIGN KEY(GenreID) REFERENCES Genre(ID));";

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

// Send server response of error (400) for request format error
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

// Send server response of error (404) for resource Not Found request error
void not_found(int new_fd){
    char buffer[] = "Not Found: Could not find requested resources";

    // create a cJSON object 
    cJSON *res = cJSON_CreateObject();
    cJSON_AddNumberToObject(res, "status", 404);
    cJSON_AddStringToObject(res, "message", buffer);
    
    // convert the cJSON object to a JSON string 
   char *res_str = cJSON_Print(res); 

    if (send(new_fd, res_str, MAXDATASIZE, 0) == -1)
        perror("send");
    return ;
}

// Send server response of error (500) for server internal error
void server_error(int new_fd, const char loc_err[]){
    char buffer[100];
    sprintf(buffer, "Server Internal Error: %s", loc_err);

    // create a cJSON object 
    cJSON *res = cJSON_CreateObject();
    cJSON_AddNumberToObject(res, "status", 500);
    cJSON_AddStringToObject(res, "message", buffer);
    
    // convert the cJSON object to a JSON string 
   char *res_str = cJSON_Print(res); 

    if (send(new_fd, res_str, MAXDATASIZE, 0) == -1)
        perror("send");
    return ;
}

// Send server response of success for creation of a new movie in DB
void successful_movie(int new_fd, const char *title, char director[128], int release_year, int movie_id, char genres[][64], int genre_count){
    char buffer[MAXDATASIZE];

    // create a cJSON object 
    cJSON *res = cJSON_CreateObject();
    cJSON_AddNumberToObject(res, "status", 200);
    cJSON_AddStringToObject(res, "message", "Movie created successfully");

    // Cria um objeto JSON para o filme
    cJSON *movie = cJSON_CreateObject();
    cJSON_AddNumberToObject(movie, "id", movie_id);
    cJSON_AddStringToObject(movie, "title", title);
    cJSON_AddStringToObject(movie, "director", director);
    cJSON_AddNumberToObject(movie, "release_year", release_year);

    // Cria um array JSON para os gêneros
    cJSON *genres_array = cJSON_CreateArray();
    for (int i = 0; i < genre_count; i++) {
        cJSON_AddItemToArray(genres_array, cJSON_CreateString(genres[i]));
    }

    // Adiciona o array de gêneros ao filme
    cJSON_AddItemToObject(movie, "genre", genres_array);

    // Adiciona o objeto filme ao objeto principal
    cJSON_AddItemToObject(res, "movie", movie);
    
    // convert the cJSON object to a JSON string 
    char *res_str = cJSON_Print(res); 

    if (send(new_fd, res_str, MAXDATASIZE, 0) == -1)
        perror("send");
    return ;
}

// Send server response for successful query in DB for movies
void successful_query(int new_fd, cJSON *res){
    cJSON_AddNumberToObject(res, "status", 200);
    cJSON_AddStringToObject(res, "message", "Successfully found movies");
    // convert the cJSON object to a JSON string 
    char *res_str = cJSON_Print(res); 

    if (send(new_fd, res_str, MAXDATASIZE, 0) == -1)
        perror("send");
    return ;
}

// Send server response for successful query in DB for a single movie
void successful_query_one(int new_fd, cJSON *res){
    cJSON_AddNumberToObject(res, "status", 200);
    cJSON_AddStringToObject(res, "message", "Successfully found movie");
    // convert the cJSON object to a JSON string 
    char *res_str = cJSON_Print(res); 

    if (send(new_fd, res_str, MAXDATASIZE, 0) == -1)
        perror("send");
    return ;
}

// Send server response for successful update in DB for a single movie
void successful_update_one(int new_fd, cJSON *res){
    cJSON_AddNumberToObject(res, "status", 200);
    cJSON_AddStringToObject(res, "message", "Successfully updated movie");
    // convert the cJSON object to a JSON string 
    char *res_str = cJSON_Print(res); 

    if (send(new_fd, res_str, MAXDATASIZE, 0) == -1)
        perror("send");
    return ;
}

// Send server response for successful query in DB for a single movie
void successful_delete(int new_fd, cJSON *res){
    cJSON_AddNumberToObject(res, "status", 200);
    cJSON_AddStringToObject(res, "message", "Deleted successfully");
    // convert the cJSON object to a JSON string 
    char *res_str = cJSON_Print(res); 

    if (send(new_fd, res_str, MAXDATASIZE, 0) == -1)
        perror("send");
    return ;
}

// POST
// Add new movie to DB and send server adequate response
void post_movie(int new_fd, JsonRequest req, sqlite3* db){
    char *zErrMsg = 0;
    int rc;
    char *sql;
    const char* data = "Callback function called";
    sqlite3_stmt *stmt;

    // create a cJSON object 
    cJSON *res = cJSON_CreateObject();

    /* Open database */
    rc = sqlite3_open("test.db", &db);

    if( rc ) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        server_error(new_fd, sqlite3_errmsg(db));
        return ;
    }

    /* Create SQL statement */
    sql = "INSERT INTO Movie (Title, Director, ReleaseYear) VALUES (?, ?, ?);";

    /* Prepare statement */
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        server_error(new_fd, sqlite3_errmsg(db));
        return;
    }

    /* Bind values with JSON Request Data */
    sqlite3_bind_text(stmt, 1, req.title, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, req.director, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, req.release_year);

    /* Execute the statement */
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Execution failed: %s\n", sqlite3_errmsg(db));
        server_error(new_fd, sqlite3_errmsg(db));
    } else {
        fprintf(stdout, "Added Movie to DB\n");
    }

    /* Finalize statement to avoid memory leaks */
    sqlite3_finalize(stmt);

    /* Get the last inserted Movie ID */
    int movie_id = (int)sqlite3_last_insert_rowid(db);

    sql = "INSERT INTO Movie_Genre (MovieID, GenreID) VALUES (?, ?);";
    /* Prepare statement */
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        server_error(new_fd, sqlite3_errmsg(db));
        return;
    }

    /* For each genre in movie JSON Request do a statement */
    for (int i = 0; i < req.num_genres; i++) {
        int genre_id = -1;

        /* Check if genre exists */
        sql = "SELECT ID FROM Genre WHERE Name = ?;";
        sqlite3_stmt *genre_stmt;
        rc = sqlite3_prepare_v2(db, sql, -1, &genre_stmt, 0);

        if (rc != SQLITE_OK) {
            fprintf(stderr, "Failed to prepare genre query: %s\n", sqlite3_errmsg(db));
            server_error(new_fd, sqlite3_errmsg(db));
            return;
        }

        sqlite3_bind_text(genre_stmt, 1, req.genre[i], -1, SQLITE_STATIC);

        rc = sqlite3_step(genre_stmt);

        if (rc == SQLITE_ROW) {
            genre_id = sqlite3_column_int(genre_stmt, 0);
        }
        sqlite3_finalize(genre_stmt);

        /* If genre not found, insert it in table with genres */
        if (genre_id == -1) {
            sql = "INSERT INTO Genre (Name) VALUES (?);";
            rc = sqlite3_prepare_v2(db, sql, -1, &genre_stmt, 0);

            if (rc != SQLITE_OK) {
                fprintf(stderr, "Failed to prepare genre insert: %s\n", sqlite3_errmsg(db));
                server_error(new_fd, sqlite3_errmsg(db));
                return;
            }

            sqlite3_bind_text(genre_stmt, 1, req.genre[i], -1, SQLITE_STATIC);

            rc = sqlite3_step(genre_stmt);

            if (rc != SQLITE_DONE) {
                fprintf(stderr, "Failed to insert genre: %s\n", sqlite3_errmsg(db));
                server_error(new_fd, sqlite3_errmsg(db));
                sqlite3_finalize(genre_stmt);
                return;
            }

            genre_id = (int)sqlite3_last_insert_rowid(db);
            sqlite3_finalize(genre_stmt);
        }

        /* Insert into Movie_Genre table */
        sqlite3_bind_int(stmt, 1, movie_id);
        sqlite3_bind_int(stmt, 2, genre_id);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            fprintf(stderr, "Failed to insert into Movie_Genre: %s\n", sqlite3_errmsg(db));
            server_error(new_fd, sqlite3_errmsg(db));
        }
        sqlite3_reset(stmt);
    }

    sqlite3_finalize(stmt);

    return successful_movie(new_fd, req.title, req.director, req.release_year, movie_id,req.genre, req.num_genres);

}

// GET
// Get all movies from DB and the server send to client as response 
void get_all(int new_fd, JsonRequest req, sqlite3* db, bool withDetail){
    char *zErrMsg = 0;
    int rc;
    char *sql;
    cJSON *res = cJSON_CreateObject();

    /* Open database */
    rc = sqlite3_open("test.db", &db);

    if( rc ) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        return server_error(new_fd, sqlite3_errmsg(db));
    }

    sql = "SELECT ID, Title from Movie";
    /* Create SQL statement */
    if(withDetail){
        sql = "SELECT m.ID, Title, Director, ReleaseYear, GROUP_CONCAT(Name, ', ') AS Genre FROM Movie m "\
            "JOIN Movie_Genre mg on m.ID = mg.MovieID "\
            "JOIN Genre g ON mg.GenreID = g.id "\
            "GROUP BY m.ID";
    }
    
    /* Execute SQL statement */
    rc = sqlite3_exec(db, sql, callback, res, &zErrMsg);

    if( rc != SQLITE_OK ) {
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
        sqlite3_free(zErrMsg);
        server_error(new_fd, sqlite3_errmsg(db));
    } else {
        fprintf(stdout, "Operation done successfully\n");
    }
    
    successful_query(new_fd, res);

    return ;
}

// Get all movies that have the same genre requested from DB and the server send to client as response 
void get_by_genre(int new_fd, JsonRequest req, sqlite3* db){
    char *zErrMsg = 0;
    int rc;
    char *sql;
    sqlite3_stmt *stmt;
    cJSON *res = cJSON_CreateObject();
    // Need to add movies array before in this case because of calls to callbacks by rows
    cJSON *movies_array = cJSON_CreateArray();
    cJSON_AddItemToObject(res, "movies", movies_array);

    /* Open database */
    rc = sqlite3_open("test.db", &db);

    if( rc ) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        return server_error(new_fd, sqlite3_errmsg(db));
    }

    sql = "SELECT m.ID, Title, Director, ReleaseYear, GROUP_CONCAT(Name, ', ') AS Genre FROM Movie m "\
        "JOIN Movie_Genre mg on m.ID = mg.MovieID "\
        "JOIN Genre g ON mg.GenreID = g.id "\
        "WHERE g.Name = ? "\
        "GROUP BY m.ID";

    // Prepare the SQL statement
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        return server_error(new_fd, sqlite3_errmsg(db));
    }

    // Bind the genre name to the statement
    rc = sqlite3_bind_text(stmt, 1, req.query, -1, SQLITE_STATIC);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to bind parameter: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return server_error(new_fd, sqlite3_errmsg(db));
    }

    // Execute the prepared statement with the callback function
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        char *row_data[5];
        for (int i = 0; i < 5; i++) {
            row_data[i] = (char *)sqlite3_column_text(stmt, i);
        }
        callback(res, 5, row_data, NULL);
    }

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Query execution error: %s\n", sqlite3_errmsg(db));
        server_error(new_fd, sqlite3_errmsg(db));
    }

    // Cleanup
    sqlite3_finalize(stmt);

    
    successful_query(new_fd, res);

    return ;
}

// Get a movies that have the matching ID from JSON Request Query and send it as JSON Response
void get_one(int new_fd, JsonRequest req, sqlite3* db){
    char *zErrMsg = 0;
    int rc;
    char *sql;
    sqlite3_stmt *stmt;
    cJSON *res = cJSON_CreateObject();

    /* Open database */
    rc = sqlite3_open("test.db", &db);

    if( rc ) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        server_error(new_fd, sqlite3_errmsg(db));
        return ;
    }

    sql = "SELECT m.ID, Title, Director, ReleaseYear, GROUP_CONCAT(Name, ', ') AS Genre FROM Movie m "\
        "JOIN Movie_Genre mg on m.ID = mg.MovieID "\
        "JOIN Genre g ON mg.GenreID = g.id "\
        "WHERE m.ID = ? ";

    // Prepare the SQL statement
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        server_error(new_fd, sqlite3_errmsg(db));
        return;
    }

    
    // Extract the movie ID from the URL
    int movie_id = atoi(req.resource + 8); // Skip "/movies/"

    // Bind the route ID to the statement
    rc = sqlite3_bind_int(stmt, 1, movie_id);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to bind parameter: %s\n", sqlite3_errmsg(db));
        server_error(new_fd, sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return;
    }

    // Only one row, so its better to not use the callback function
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        if(sqlite3_column_int(stmt, 0) == 0){
            sqlite3_finalize(stmt);
            return not_found(new_fd);
        }
        
        // Create JSON response
        cJSON *movie_obj = cJSON_CreateObject();

        cJSON_AddNumberToObject(movie_obj, "id", sqlite3_column_int(stmt, 0));
        cJSON_AddStringToObject(movie_obj, "title", (const char*)sqlite3_column_text(stmt, 1));
        cJSON_AddStringToObject(movie_obj, "director", (const char*)sqlite3_column_text(stmt, 2));
        cJSON_AddNumberToObject(movie_obj, "release_year", sqlite3_column_int(stmt, 3));

        // Parse the concatenated genre string into a JSON array
        cJSON *genres_array = cJSON_CreateArray();
        const char *genres_str = (const char*)sqlite3_column_text(stmt, 4);
        if (genres_str != NULL) {
            char *genres_copy = strdup(genres_str);
            char *token = strtok(genres_copy, ",");
            while (token != NULL) {
                cJSON_AddItemToArray(genres_array, cJSON_CreateString(token));
                token = strtok(NULL, ",");
            }
            free(genres_copy);
        }

        cJSON_AddItemToObject(movie_obj, "genre", genres_array);
        cJSON_AddItemToObject(res, "movie", movie_obj);

    } else if (rc == SQLITE_DONE) {
        return not_found(new_fd);
    } else {
        fprintf(stderr, "Failed to execute statement: %s\n", sqlite3_errmsg(db));
        return server_error(new_fd, sqlite3_errmsg(db));
    }

    // Cleanup
    sqlite3_finalize(stmt);
    
    successful_query_one(new_fd, res);

    return ;
}

// DELETE
void delete_one(int new_fd, JsonRequest req, sqlite3* db){
    char *zErrMsg = 0;
    int rc;
    char *sql;
    sqlite3_stmt *stmt;
    cJSON *res = cJSON_CreateObject();

    // Extract the movie ID from the URL
    int movie_id = atoi(req.resource + 8); // Skip "/movies/"

    /* Open database */
    rc = sqlite3_open("test.db", &db);

    if( rc ) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        server_error(new_fd, sqlite3_errmsg(db));
        return ;
    }

    // DELETE MOVIE GENRES BY MOVIE ID

    sql = "DELETE FROM Movie_Genre WHERE MovieID = ?;";

    // Prepare the SQL statement
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        server_error(new_fd, sqlite3_errmsg(db));
        return;
    }

    sqlite3_bind_int(stmt, 1, movie_id);
    rc = sqlite3_step(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failed to delete movie genres: %s\n", sqlite3_errmsg(db));
    }

    // Cleanup
    sqlite3_finalize(stmt);

    // DELETE MOVIE BY MOVIE ID

    sql = "DELETE FROM Movie WHERE ID = ?;";

    // Prepare the SQL statement
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        server_error(new_fd, sqlite3_errmsg(db));
        return;
    }

    sqlite3_bind_int(stmt, 1, movie_id);
    rc = sqlite3_step(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failed to delete movie genres: %s\n", sqlite3_errmsg(db));
    }

    // Cleanup
    sqlite3_finalize(stmt);

    return successful_delete(new_fd, res);
}

// PUT
void update_one(int new_fd, JsonRequest req, sqlite3* db){
    int rc;
    const char *sql;
    sqlite3_stmt *stmt;
    cJSON *res = cJSON_CreateObject();
    
    /* Open database */
    rc = sqlite3_open("test.db", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        return;
    }

    // Extract the movie ID from the URL
    int movie_id = atoi(req.resource + 8); // Skip "/movies/"

    // Prepare SQL update query for Movie table
    sql = "UPDATE Movie SET Title = ?, Director = ?, ReleaseYear = ? WHERE ID = ?;";
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        return server_error(new_fd, sqlite3_errmsg(db));
    }

    // Bind values to the prepared statement
    sqlite3_bind_text(stmt, 1, req.title, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, req.director, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, req.release_year);
    sqlite3_bind_int(stmt, 4, movie_id);

    // Execute the update statement
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failed to update movie: %s\n", sqlite3_errmsg(db));
        return server_error(new_fd, sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);

    // Delete old genre associations for the movie
    sql = "DELETE FROM Movie_Genre WHERE MovieID = ?;";
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, movie_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    // Insert new genres
    sql = "INSERT INTO Movie_Genre (MovieID, GenreID) VALUES (?, (SELECT ID FROM Genre WHERE Name = ?));";
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

    for (int i = 0; i < req.num_genres; i++) {
        char* genre = req.genre[i];
        sqlite3_bind_int(stmt, 1, movie_id);
        sqlite3_bind_text(stmt, 2, genre, -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            fprintf(stderr, "Failed to insert genre: %s\n", sqlite3_errmsg(db));
            return server_error(new_fd, sqlite3_errmsg(db));
        }
        sqlite3_reset(stmt);
    }

    sqlite3_finalize(stmt);

    printf("Movie updated successfully.\n");

    // Retrieve the updated movie
    sql = "SELECT m.ID, Title, Director, ReleaseYear, GROUP_CONCAT(g.Name, ',') AS Genres "
          "FROM Movie m "
          "LEFT JOIN Movie_Genre mg ON m.ID = mg.MovieID "
          "LEFT JOIN Genre g ON mg.GenreID = g.id "
          "WHERE m.ID = ? "
          "GROUP BY m.ID;";
          
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, movie_id);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        cJSON *movie_obj = cJSON_CreateObject();

        cJSON_AddNumberToObject(movie_obj, "id", sqlite3_column_int(stmt, 0));
        cJSON_AddStringToObject(movie_obj, "title", (const char*)sqlite3_column_text(stmt, 1));
        cJSON_AddStringToObject(movie_obj, "director", (const char*)sqlite3_column_text(stmt, 2));
        cJSON_AddNumberToObject(movie_obj, "release_year", sqlite3_column_int(stmt, 3));

        // Parse genres
        cJSON *genres_array = cJSON_CreateArray();
        const char *genres_str = (const char*)sqlite3_column_text(stmt, 4);
        if (genres_str != NULL) {
            char *genres_copy = strdup(genres_str);
            char *token = strtok(genres_copy, ",");
            while (token != NULL) {
                cJSON_AddItemToArray(genres_array, cJSON_CreateString(token));
                token = strtok(NULL, ",");
            }
            free(genres_copy);
        }

        cJSON_AddItemToObject(movie_obj, "genre", genres_array);
        cJSON_AddItemToObject(res, "movie", movie_obj);
    }

    sqlite3_finalize(stmt);

    return successful_update_one(new_fd, res);
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
            return delete_one(new_fd, req, db);
        }

        // POST & PUT
        if(strcmp(req.method,"POST") == 0 || strcmp(req.method,"PUT") == 0){
            cJSON *title = cJSON_GetObjectItem(body, "title");
            cJSON *release_year = cJSON_GetObjectItem(body, "release_year");
            cJSON *director = cJSON_GetObjectItem(body, "director");
            cJSON *genres = cJSON_GetObjectItem(body, "genre");

            if (cJSON_IsString(title) && (title->valuestring != NULL)) { 
                strncpy(req.title, title->valuestring, sizeof(req.title) - 1);
            } else return invalid_request(new_fd, "body.title");
            if (cJSON_IsString(director) && (director->valuestring != NULL)) { 
                strncpy(req.director, director->valuestring, sizeof(req.director) - 1);
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
            } else return invalid_request(new_fd, "body.genre");

            if(strcmp(req.method,"POST") == 0){
                return post_movie(new_fd, req, db);
            } else {
                return update_one(new_fd, req, db);
            }
            
        }

        // GET
        if(strcmp(req.method, "GET") == 0 && strcmp(req.resource, "/movies") == 0){
            return get_all(new_fd, req, db, false);
        }
        if(strcmp(req.method, "GET") == 0 && strcmp(req.resource, "/movies/detail") == 0){
            return get_all(new_fd, req, db, true);
        }
        if(strcmp(req.method, "GET") == 0 && strcmp(req.resource, "/movies/genre") == 0){
            cJSON *query = cJSON_GetObjectItem(body, "query");
            if (cJSON_IsString(query) && (query->valuestring != NULL)) { 
                strncpy(req.query, query->valuestring, sizeof(req.query) - 1);
            } else return invalid_request(new_fd, "body.query");
            return get_by_genre(new_fd, req, db);
        }
        else{
            return get_one(new_fd, req, db);
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
