/* (C) 2008 by Jan Luebbe <jluebbe@debian.org>
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <openbsc/db.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dbi/dbi.h>

dbi_conn conn;

void db__error_func(dbi_conn conn, void* data) {
	const char* msg;
	dbi_conn_error(conn, &msg);
	printf("DBI: %s\n", msg);
}

int db_init() {
	dbi_initialize(NULL);
	conn = dbi_conn_new("sqlite3");
	if (conn==NULL) {
		printf("DB: Failed to create connection.\n");
		return 1;
	}

	dbi_conn_error_handler( conn, db__error_func, NULL );

	/* MySQL
	dbi_conn_set_option(conn, "host", "localhost");
	dbi_conn_set_option(conn, "username", "your_name");
	dbi_conn_set_option(conn, "password", "your_password");
	dbi_conn_set_option(conn, "dbname", "your_dbname");
	dbi_conn_set_option(conn, "encoding", "UTF-8");
	*/

	/* SqLite 3 */
	dbi_conn_set_option(conn, "sqlite3_dbdir", "/tmp");
	dbi_conn_set_option(conn, "dbname", "hlr.sqlite3");

	if (dbi_conn_connect(conn) < 0) {
		return 1;
	}

	return 0;
}

int db_prepare() {
	dbi_result result;
	result = dbi_conn_query(conn,
		"CREATE TABLE IF NOT EXISTS Subscriber ("
		"id INTEGER PRIMARY KEY AUTOINCREMENT, "
		"imsi NUMERIC UNIQUE NOT NULL, "
		"tmsi NUMERIC UNIQUE, "
		"extension TEXT UNIQUE, "
		"lac INTEGER"
		")"
	);
	if (result==NULL) {
		printf("DB: Failed to create Subscriber table.\n");
		return 1;
	}
	dbi_result_free(result);
	result = dbi_conn_query(conn,
		"CREATE TABLE IF NOT EXISTS Equipment ("
		"id INTEGER PRIMARY KEY AUTOINCREMENT, "
		"imei NUMERIC UNIQUE NOT NULL"
		")"
	);
	if (result==NULL) {
		printf("DB: Failed to create Equipment table.\n");
		return 1;
	}
	dbi_result_free(result);
	return 0;
}

int db_fini() {
	dbi_conn_close(conn);
	dbi_shutdown();
	return 0;
}

int db_insert_imei(u_int64_t imei) {
	dbi_result result;
	result = dbi_conn_queryf(conn,
		"INSERT OR IGNORE INTO Equipment "
		"(imei) "
		"VALUES "
		"(%llu) ",
		imei
	);
	if (result==NULL) {
		printf("DB: Failed to create Equipment by IMEI.\n");
		return 1;
	}
	dbi_result_free(result);
	return 0;
}

struct gsm_subscriber* db_create_subscriber(char imsi[GSM_IMSI_LENGTH]) {
	dbi_result result;
	struct gsm_subscriber* subscriber;
	subscriber = malloc(sizeof(*subscriber));
	if (!subscriber)
		return NULL;
	memset(subscriber, 0, sizeof(*subscriber));
	strncpy(subscriber->imsi, imsi, GSM_IMSI_LENGTH-1);
	if (!db_get_subscriber(GSM_SUBSCRIBER_IMSI, subscriber)) {
		return subscriber;
	}
	result = dbi_conn_queryf(conn,
		"INSERT OR IGNORE INTO Subscriber "
		"(imsi) "
		"VALUES "
		"(%s) ",
		imsi
	);
	if (result==NULL) {
		printf("DB: Failed to create Subscriber by IMSI.\n");
	}
	dbi_result_free(result);
	printf("DB: New Subscriber: IMSI %s\n", subscriber->imsi);
	return subscriber;
}

int db__parse_subscriber(dbi_result result, struct gsm_subscriber* subscriber) {
	return 0;
}

int db_get_subscriber(enum gsm_subscriber_field field, struct gsm_subscriber* subscriber) {
	dbi_result result;
	switch (field) {
	case GSM_SUBSCRIBER_IMSI:
		result = dbi_conn_queryf(conn,
			"SELECT * FROM Subscriber "
			"WHERE imsi = %s ",
			subscriber->imsi
		);
		break;
	case GSM_SUBSCRIBER_TMSI:
		result = dbi_conn_queryf(conn,
			"SELECT * FROM Subscriber "
			"WHERE tmsi = %s ",
			subscriber->tmsi
		);
		break;
	default:
		printf("DB: Unknown query selector for Subscriber.\n");
		return 1;
	}
	if (result==NULL) {
		printf("DB: Failed to query Subscriber.\n");
		return 1;
	}
	if (!dbi_result_next_row(result)) {
		printf("DB: Failed to find the Subscriber.\n");
		dbi_result_free(result);
		return 1;
	}
	strncpy(subscriber->imsi, dbi_result_get_string(result, "imsi"), GSM_IMSI_LENGTH);
	strncpy(subscriber->tmsi, dbi_result_get_string(result, "tmsi"), GSM_TMSI_LENGTH);
	// FIXME handle extension
	subscriber->lac = dbi_result_get_uint(result, "lac");
	printf("DB: Found Subscriber: IMSI %s, TMSI %s, LAC %hu\n", subscriber->imsi, subscriber->tmsi, subscriber->lac);
	dbi_result_free(result);
	return 0;
}

int db_set_subscriber(struct gsm_subscriber* subscriber) {
	dbi_result result;
	result = dbi_conn_queryf(conn,
		"UPDATE Subscriber "
		"SET tmsi = %s, lac = %i "
		"WHERE imsi = %s ",
		subscriber->tmsi, subscriber->lac, subscriber->imsi
	);
	if (result==NULL) {
		printf("DB: Failed to update Subscriber (by IMSI).\n");
		return 1;
	}
	dbi_result_free(result);
	return 0;
}

int db_subscriber_alloc_tmsi(struct gsm_subscriber* subscriber) {
	int error;
	dbi_result result=NULL;
	char* tmsi_quoted;
	for (;;) {
		sprintf(subscriber->tmsi, "%i", rand() % 1000000); // FIXME how many nibbles do we want for the tmsi?
		result = dbi_conn_queryf(conn,
			"SELECT * FROM Subscriber "
			"WHERE tmsi = %s ",
			subscriber->tmsi
		);
		if (result==NULL) {
			printf("DB: Failed to query Subscriber.\n");
			return 1;
		}
		printf("%s\n", subscriber->tmsi);
		printf("count %llu\n", dbi_result_get_numrows(result));
		printf("curr %llu\n", dbi_result_get_currow(result));
		printf("next %llu\n", dbi_result_next_row(result));
		printf("count %llu\n", dbi_result_get_numrows(result));
		printf("curr %llu\n", dbi_result_get_currow(result));
		if (dbi_result_get_numrows(result)){
			dbi_result_free(result);
			continue;
		}
		if (!dbi_result_next_row(result)) {
			printf("curr %llu\n", dbi_result_get_currow(result));
			dbi_result_free(result);
			printf("DB: Allocated TMSI %s for IMSI %s.\n", subscriber->tmsi, subscriber->imsi);
			return db_set_subscriber(subscriber);
		}
		dbi_result_free(result);
	}
	return 0;
}

