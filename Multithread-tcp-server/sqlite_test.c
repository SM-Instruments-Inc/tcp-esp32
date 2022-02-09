#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

int main(void)
{
    sqlite3 *db;
    char *err_msg = 0;
    
    int rc = sqlite3_open("test.db", &db);
    
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        
        return 1;
    }
    
    char *sql = "DROP TABLE IF EXISTS Cars;" 
                "CREATE TABLE Gas_Datas(ID INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,data INT,TIME DATETIME DEFAULT (datetime('now','localtime')));";

    char gas[100];
	int i = 1000;
	time_t t = time(NULL);
    struct tm tm= *localtime(&t);
	sprintf(gas, "INSERT INTO Gas_Datas (data) VALUES(%d);",i);
    rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    rc = sqlite3_exec(db, gas, 0, 0, &err_msg);
    
    if (rc != SQLITE_OK )
    {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        
        sqlite3_free(err_msg);        
        sqlite3_close(db);
        
        return 1;
    }
    
    sqlite3_close(db);
    
    return 0;
}