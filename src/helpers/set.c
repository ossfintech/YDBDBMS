/* Copyright (C) 2018-2019 YottaDB, LLC
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

#include <string.h>

#include "octo.h"

#include "helpers.h"

void set(char *new_value, char *global, size_t num_args, ...) {
	va_list args;
	ydb_buffer_t *ret, *buffers;
	ydb_buffer_t z_status, z_status_value;
	int status, i;

	va_start(args, num_args);
	buffers = make_buffers(global, num_args, args);
	va_end(args);

	ret = (ydb_buffer_t*)malloc(sizeof(ydb_buffer_t));
	YDB_MALLOC_BUFFER(ret, MAX_STR_CONST);
	YDB_COPY_STRING_TO_BUFFER(new_value, ret, status);

	status = ydb_set_s(&buffers[0], num_args, &buffers[1], ret);
	YDB_ERROR_CHECK(status, &z_status, &z_status_value);

	free(buffers);
	free(ret->buf_addr);
	free(ret);
}
