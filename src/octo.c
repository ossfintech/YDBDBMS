/* Copyright (C) 2018 YottaDB, LLC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <assert.h>
#include <string.h>

#include <libyottadb.h>
#include <gtmxc_types.h>

#include "octo.h"
#include "octo_types.h"
#include "physical_plan.h"
#include "parser.h"
#include "lexer.h"

extern int yydebug;

int no_more() {
	return 0;
}

int main(int argc, char **argv)
{
	int c, error = 0, status;
	int done;
	SqlValue *value;
	SqlTable *table, *t_table;
	SqlStatement *tmp_statement;

	octo_init();

	inputFile = NULL;
	cur_input_more = &no_more;

	/* Parse input parameters */
	while (1)
	{
		static struct option long_options[] =
		{
			{"verbose", optional_argument, NULL, 'v'},
			{"dry-run", no_argument, NULL, 'd'},
			{"input-file", required_argument, NULL, 'f'},
			{0, 0, 0, 0}
		};
		int option_index = 0;

		c = getopt_long(argc, argv, "vdf:t:", long_options, &option_index);
		if(c == -1)
			break;

		switch(c)
		{
		case 0:
			if(long_options[option_index].flag != 0)
				break;
			break;
		case 'v':
			if(optarg) {
				c = atoi(optarg);
				if(c > FATAL || c < TRACE) {
					ERROR(CUSTOM_ERROR, "Invalid value specified for --verbose");
					return 1;
				}
				config->record_error_level = FATAL - c;
			} else {
				config->record_error_level = config->record_error_level > TRACE
				                             ? config->record_error_level - 1 : config->record_error_level;
			}
			break;
		case 'd':
			config->dry_run = 1;
			break;
		case 'f':
			assert(inputFile == NULL);
			inputFile = fopen(optarg, "r");
			if (inputFile == NULL)
			{
				FATAL(ERR_FILE_NOT_FOUND, optarg);
			}
			break;
		default:
			ERROR(CUSTOM_ERROR, "Uknown argument");
			return 1;
		}
	}

	TRACE(CUSTOM_ERROR, "Octo started");

	/* Load the existing tables */

	yydebug = config->record_error_level == TRACE && FALSE;
	cur_input_more = &readline_get_more;
	if (inputFile == NULL)
		inputFile = stdin;
	cur_input_index = 0;
	memset(input_buffer_combined, 0, MAX_STR_CONST);
	do {
		//if(readline_getc(inputFile, input_buffer_combined, MAX_STR_CONST) == -1)
		//	break;
		//cur_input_index = cur_input_max;
		//cur_input_index = 0;
		//input_buffer_combined[cur_input_index] = '\0';
		if(run_query(input_buffer_combined, &print_temporary_table, NULL) == 0) {
			if(eof_hit)
				break;
		}
	} while(!feof(inputFile));
	if(definedTables != NULL) {
		SQL_STATEMENT(tmp_statement, table_STATEMENT);
		tmp_statement->v.table = definedTables;
		cleanup_sql_statement(tmp_statement);
	}
	return error;
}
