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
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "octo.h"
#include "octo_types.h"

int tables_equal(SqlTable *a, SqlTable *b) {
	SqlValue *valA, *valB;
	UNPACK_SQL_STATEMENT(valA, a->tableName, value);
	UNPACK_SQL_STATEMENT(valB, b->tableName, value);
	if(values_equal(valA, valB) == FALSE)
		return FALSE;
	// But we won't test columns, as that would be somewhat expensive
	return TRUE;
}
